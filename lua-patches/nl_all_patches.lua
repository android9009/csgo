--[[============================================================================
    nl_all_patches.lua  —  все патчи Neverlose из репозитория
                            android9009/My-luas в одном скрипте

    В репозитории NL-патч ровно один (второй NL-патч = этот же файл):

      * PAW Backtrack           Patches/2026-11-02_15-31-42-127.lua
        Расширение окна бэктрака внутри самого чита + аккуратная подстройка
        sv_maxunlag, чтобы Fake Latency продолжал работать поверх патча.

        Что внутри:
          - окно бэктрака (immediate в lagcomp-проверке): 8 тиков сток -> слайдер
          - "Aggressive" (выключено по умолчанию):
              Keep valid records        (очистка записей -> пустой ret)
              No extrapolation          (should_extrapolate -> return 0)
              Records valid on fakeduck (-> return 1)
          - sv_maxunlag поднимается только вверх, ровно на окно + вашу
            исходящую задержку, и возвращается обратно только если там всё ещё
            наше значение.

    Плюс раздел 9.5 — патчи из Luas/nl/viera utils.lua (группа
    Rage -> NL Backtrack Fixes):

      * Preserve valid records   (0F -> 00 у jne в скане записей)
      * Early defensive activation (dec edi -> NOP)
      * Skip break distance check  (два вызова -> B0 00 90 90 90)
      * Defensive on backtrack     (disp32 у jz -> свой стаб на VirtualAlloc)

    Сайты viera находятся СКАНОМ байтов по образу чита, а не по числам.
    Не перенесены (и почему): extrapolation primary и record selection —
    это не байтовые патчи, а подмена кода стабами на 1.4 КБ, которую нельзя
    проверить обратным чтением.

    Что изменено по сравнению с оригиналом:

      * База модуля чита определяется как и раньше (скан приватной исполняемой
        памяти через VirtualQuery, затем GetModuleHandleA("csgo.dll" /
        "client.dll")), но кэшируется, проверяется по ВСЕМ известным символам
        сразу, и результат печатается в консоль.
      * Каждый payload строится из ЖИВЫХ байт функции (её собственный ret
        C3/C2 imm16), поэтому стек вызывающего не разъезжается никогда.
        Операнд-слот ищется по уникальному совпадению: два совпадения — отказ,
        а не "запишем в первое".
      * Перед записью проверяется, что страница исполняемая и не под
        PAGE_GUARD; после записи байты читаются обратно.
      * Частичный "arm" откатывается целиком, чтобы рейджбот не остался
        наполовину пропатченным.
      * Добавлены: кнопка "Revert now", паник-хоткей, кнопка "Diagnose"
        (печатает базу и байты всех сайтов), ограничение числа попыток и
        внятные сообщения вместо молчаливого отказа.
      * Восстановление: выключение чекбокса, кнопка, паник-хоткей, shutdown.
============================================================================]]

local ffi = require("ffi")
local bit = require("bit")

local NAME = "paw-bt"

local function log(fmt, ...)
    local ok, text = pcall(string.format, fmt, ...)
    print(("[%s] %s"):format(NAME, ok and text or tostring(fmt)))
end

local function warn(fmt, ...)
    log("!! " .. tostring(fmt), ...)
end

--[[----------------------------------------------------------------------------
    1. Настройки и карта символов
------------------------------------------------------------------------------]]

local WINDOW_TICKS   = 14       -- по умолчанию 14 тиков ~ 218 мс
local WINDOW_DEFAULT = 8        -- сток: 8 тиков (по нему ищется операнд)
local UNLAG_HEADROOM = 0.05     -- запас сверх окна + латенси
local UNLAG_CEILING  = 1.0      -- выше секунды не пишем никогда
local UNLAG_RECHECK  = 16       -- net update между перепроверками
local RETRY_TICKS    = 64       -- net update между попытками arm
local MAX_ATTEMPTS   = 8        -- дальше молчим, а не спамим

local IDA_BASE = 0x41000000     -- база, под которую сняты VA

local SYM = {
    -- рабочие сайты
    lagrecord_check_lagcomp_window = { va = 0x41400bf0, size = 484  },
    player_records_cleanup         = { va = 0x414441c0, size = 919  },
    should_extrapolate             = { va = 0x413f6db0, size = 33   },
    backtrack_valid_while_fakeduck = { va = 0x414961e0, size = 555  },
    -- только для верификации базы
    player_records_update          = { va = 0x4144b12e, size = 5750 },
    lagrecord_is_expired           = { va = 0x413f6d50, size = 65   },
    select_lagrecord               = { va = 0x4148f180, size = 1700 },
    validate_backtrack_window      = { va = 0x4148ff1d, size = 613  },
    backtrack_valid_check          = { va = 0x414ab5d0, size = 297  },
    backtrack_scan                 = { va = 0x41495850, size = 1071 },
    ragebot_backtrack_apply        = { va = 0x414c0920, size = 236  },
    adjust_extrap_ticks            = { va = 0x413341e0, size = 250  },
}

local SYM_COUNT = 0
for _ in pairs(SYM) do SYM_COUNT = SYM_COUNT + 1 end

--[[----------------------------------------------------------------------------
    2. Win32
------------------------------------------------------------------------------]]

ffi.cdef[[
    typedef struct {
        unsigned int BaseAddress, AllocationBase, AllocationProtect, RegionSize;
        unsigned int State, Protect, Type;
    } PAW_MBI;
    int          __stdcall VirtualProtect(void* addr, unsigned int size, unsigned int new_prot, unsigned int* old_prot);
    unsigned int __stdcall VirtualQuery(const void* addr, PAW_MBI* info, unsigned int size);
    void*        __stdcall GetModuleHandleA(const char* name);
    void*        __stdcall VirtualAlloc(void* addr, unsigned int size, unsigned int kind, unsigned int protect);
    int          __stdcall VirtualFree(void* addr, unsigned int size, unsigned int kind);
    int          __stdcall FlushInstructionCache(void* process, const void* addr, unsigned int size);
    void*        __stdcall GetCurrentProcess(void);
]]

local C             = ffi.C
local MEM_COMMIT    = 0x1000
local MEM_PRIVATE   = 0x20000
local PAGE_EXECANY  = 0xF0
local PAGE_GUARD    = 0x100
local PAGE_NOACCESS = 0x01
local PAGE_RWX      = 0x40
local MEM_COMMIT_RESERVE = 0x3000
local MEM_RELEASE   = 0x8000

--[[----------------------------------------------------------------------------
    3. База модуля чита
------------------------------------------------------------------------------]]

local module_base = nil

local function region_info(addr)
    local mbi = ffi.new("PAW_MBI")
    if C.VirtualQuery(ffi.cast("void*", addr), mbi, ffi.sizeof("PAW_MBI")) == 0 then return nil end
    return mbi
end

local function is_exec(addr)
    local mbi = region_info(addr)
    if mbi == nil then return false end
    local protect = tonumber(mbi.Protect)
    return mbi.State == MEM_COMMIT
       and bit.band(protect, PAGE_EXECANY) ~= 0
       and bit.band(protect, PAGE_GUARD) == 0
       and bit.band(protect, PAGE_NOACCESS) == 0
end

local function verify_base(base)
    local hits = 0
    for _, symbol in pairs(SYM) do
        if is_exec(base + (symbol.va - IDA_BASE)) then hits = hits + 1 end
    end
    return hits == SYM_COUNT, hits, SYM_COUNT
end

-- Модуль чита мэпнут вручную: это приватный коммит с исполняемой защитой, а не
-- MEM_IMAGE, поэтому GetModuleHandleA по нему обычно пуст. Сначала ищем такой
-- регион, потом пробуем обычные имена модулей.
local function resolve_base(force)
    if module_base ~= nil and not force then return module_base end
    module_base = nil

    local mbi  = ffi.new("PAW_MBI")
    local addr = 0x00010000
    local span = {}
    while addr < 0x7FFF0000 do
        if C.VirtualQuery(ffi.cast("void*", addr), mbi, ffi.sizeof("PAW_MBI")) == 0 then break end
        local size = tonumber(mbi.RegionSize)
        if size == 0 then break end
        local protect, kind, alloc = tonumber(mbi.Protect), tonumber(mbi.Type), tonumber(mbi.AllocationBase)
        if mbi.State == MEM_COMMIT and kind == MEM_PRIVATE and alloc ~= 0
           and bit.band(protect, PAGE_EXECANY) ~= 0 and bit.band(protect, PAGE_GUARD) == 0 then
            span[alloc] = (span[alloc] or 0) + size
        end
        addr = tonumber(mbi.BaseAddress) + size
    end

    local candidates = {}
    for alloc, size in pairs(span) do candidates[#candidates + 1] = { base = alloc, size = size } end
    table.sort(candidates, function(a, b) return a.size > b.size end)

    for _, candidate in ipairs(candidates) do
        local ok, hits, total = verify_base(candidate.base)
        if ok then
            module_base = candidate.base
            log("база модуля 0x%08X (приватный exec %.2f МБ, сайтов проверено %d/%d)",
                candidate.base, candidate.size / 1048576, hits, total)
            return module_base
        end
    end

    for _, name in ipairs({ "csgo.dll", "client.dll" }) do
        local handle = C.GetModuleHandleA(name)
        if handle ~= nil then
            local base = tonumber(ffi.cast("uintptr_t", handle))
            local ok, hits, total = verify_base(base)
            if ok then
                module_base = base
                log("база через GetModuleHandleA(\"%s\") 0x%08X, сайтов проверено %d/%d",
                    name, base, hits, total)
                return module_base
            end
        end
    end

    return nil
end

local function entry_of(name)
    local base = resolve_base()
    local symbol = SYM[name]
    if base == nil or symbol == nil then return nil end
    return base + (symbol.va - IDA_BASE), symbol.size
end

--[[----------------------------------------------------------------------------
    4. Память (каждая запись проверяется обратным чтением)
------------------------------------------------------------------------------]]

local function raw_write(addr, bytes)
    local old = ffi.new("unsigned int[1]")
    if C.VirtualProtect(ffi.cast("void*", addr), #bytes, PAGE_RWX, old) == 0 then return false end

    local pointer = ffi.cast("uint8_t*", addr)
    for i = 1, #bytes do pointer[i - 1] = bytes[i] end

    local ignored = ffi.new("unsigned int[1]")
    C.VirtualProtect(ffi.cast("void*", addr), #bytes, old[0], ignored)

    for i = 1, #bytes do
        if tonumber(pointer[i - 1]) ~= bytes[i] then return false end
    end
    return true
end

local function snapshot(addr, count)
    local pointer, saved = ffi.cast("uint8_t*", addr), {}
    for i = 1, count do saved[i] = tonumber(pointer[i - 1]) end
    return saved
end

local function hexdump(addr, count)
    local pointer, out = ffi.cast("uint8_t*", addr), {}
    for i = 0, count - 1 do out[#out + 1] = string.format("%02X", tonumber(pointer[i])) end
    return table.concat(out, " ")
end

--[[----------------------------------------------------------------------------
    5. Payload'ы из живых байт
------------------------------------------------------------------------------]]

-- Собственный терминальный ret функции несёт её очистку стека (C3 или C2 imm16),
-- поэтому ранний возврат, собранный из него, никогда не разбалансирует стек
-- вызывающего.
local function detect_ret(entry, size)
    local pointer = ffi.cast("uint8_t*", entry)
    local i = size - 1
    while i > 0 and (pointer[i] == 0xCC or pointer[i] == 0x90) do i = i - 1 end
    if i >= 2 and pointer[i - 2] == 0xC2 then
        return { 0xC2, tonumber(pointer[i - 1]), tonumber(pointer[i]) }
    end
    if pointer[i] == 0xC3 then return { 0xC3 } end
    return nil
end

local function make_neutralize(entry, size)
    return detect_ret(entry, size)
end

-- mov eax, imm32 ; <собственный ret функции>
local function make_force_return(entry, size, value)
    local ret = detect_ret(entry, size)
    if ret == nil then return nil end
    local payload = {
        0xB8,
        bit.band(value, 0xFF),
        bit.band(bit.rshift(value, 8),  0xFF),
        bit.band(bit.rshift(value, 16), 0xFF),
        bit.band(bit.rshift(value, 24), 0xFF),
    }
    for _, byte in ipairs(ret) do payload[#payload + 1] = byte end
    return payload
end

-- Ищем только формы операнда с известной длиной и требуем единственного
-- совпадения: два кандидата — непонятно, какой именно cmp переписывать, и
-- патч отказывается, печатая пролог функции.
local function find_immediate(entry, size, default)
    local pointer, found, i = ffi.cast("uint8_t*", entry), {}, 0
    while i < size - 1 do
        local op = tonumber(pointer[i])
        if op == 0x6A and tonumber(pointer[i + 1]) == default then
            found[#found + 1] = { off = i + 1, len = 1 }                         -- push imm8
        elseif op == 0x83 and i + 2 < size then
            if bit.rshift(tonumber(pointer[i + 1]), 6) == 3 and tonumber(pointer[i + 2]) == default then
                found[#found + 1] = { off = i + 2, len = 1 }                     -- grp1 r/m32, imm8
            end
        elseif op == 0xC7 and i + 6 < size then
            if bit.rshift(tonumber(pointer[i + 1]), 6) == 3 then
                local value = tonumber(pointer[i + 2]) + tonumber(pointer[i + 3]) * 256
                            + tonumber(pointer[i + 4]) * 65536 + tonumber(pointer[i + 5]) * 16777216
                if value == default then
                    found[#found + 1] = { off = i + 2, len = 4 }                 -- mov r/m32, imm32
                end
            end
        elseif op >= 0xB8 and op <= 0xBF and i + 4 < size then
            local value = tonumber(pointer[i + 1]) + tonumber(pointer[i + 2]) * 256
                        + tonumber(pointer[i + 3]) * 65536 + tonumber(pointer[i + 4]) * 16777216
            if value == default then
                found[#found + 1] = { off = i + 1, len = 4 }                     -- mov r32, imm32
            end
        end
        i = i + 1
    end
    return found
end

local function encode_imm(value, len)
    if len == 1 then return { bit.band(value, 0xFF) } end
    return {
        bit.band(value, 0xFF),
        bit.band(bit.rshift(value, 8),  0xFF),
        bit.band(bit.rshift(value, 16), 0xFF),
        bit.band(bit.rshift(value, 24), 0xFF),
    }
end

--[[----------------------------------------------------------------------------
    6. Патч
------------------------------------------------------------------------------]]

local Patch = {}
Patch.__index = Patch

function Patch.new(def)
    local self = setmetatable(def, Patch)
    self.armed = false
    return self
end

function Patch:log(fmt, ...)
    local ok, text = pcall(string.format, fmt, ...)
    log("%-32s %s", self.note, ok and text or tostring(fmt))
end

function Patch:arm()
    if self.armed then return true end

    local entry, size = entry_of(self.sym)
    if entry == nil then
        self:log("символ %s не найден (база модуля не определена)", tostring(self.sym))
        return false, "база модуля не определена"
    end

    if not is_exec(entry) then
        self:log("адрес 0x%08X не исполняемый, отказ", entry)
        return false, "сайт не исполняемый"
    end

    if self.kind == "immediate" then
        local found = find_immediate(entry, size, self.default)
        if #found ~= 1 then
            self:log("операнд неоднозначен (%d совпадений для %d): %s",
                     #found, self.default, hexdump(entry, 24))
            return false, "операнд неоднозначен"
        end

        local slot = found[1]
        local target = entry + slot.off
        self.applied = target
        self.imm_len = slot.len
        self.saved   = snapshot(target, slot.len)

        if not raw_write(target, encode_imm(self.value, slot.len)) then
            self.applied, self.saved, self.imm_len = nil, nil, nil
            self:log("VirtualProtect или запись не удались")
            return false, "запись не удалась"
        end

        self.armed = true
        self:log("окно %d -> %d @+0x%X", self.default, self.value, slot.off)
        return true
    end

    local payload
    if self.kind == "neutralize" then
        payload = make_neutralize(entry, size)
    else
        payload = make_force_return(entry, size, self.value)
    end

    if payload == nil then
        self:log("в функции нет терминального ret (хвостовой вызов?): %s", hexdump(entry, 24))
        return false, "нет терминального ret"
    end

    self.applied = entry
    self.saved   = snapshot(entry, #payload)
    if not raw_write(entry, payload) then
        self.applied, self.saved = nil, nil
        self:log("VirtualProtect или запись не удались")
        return false, "запись не удалась"
    end

    self.armed = true
    self:log("включён @0x%08X (%d байт)", entry, #payload)
    return true
end

-- Движение слайдера при уже включённом патче: слот найден и сохранён, поэтому
-- это одна запись по месту — без повторного скана и без второго снапшота.
function Patch:set_value(value)
    self.value = value
    if not (self.armed and self.kind == "immediate" and self.applied) then return false end
    if not raw_write(self.applied, encode_imm(value, self.imm_len)) then return false end
    self:log("значение = %d", value)
    return true
end

function Patch:disarm()
    if self.armed and self.saved and self.applied then
        if raw_write(self.applied, self.saved) then
            self:log("байты возвращены")
        else
            self:log("!! вернуть байты не удалось")
        end
    end
    self.armed, self.saved, self.applied, self.imm_len = false, nil, nil, nil
end

--[[----------------------------------------------------------------------------
    7. Сайты
------------------------------------------------------------------------------]]

local window_patch = Patch.new{
    note    = "Окно бэктрака",
    sym     = "lagrecord_check_lagcomp_window",
    kind    = "immediate",
    default = WINDOW_DEFAULT,
    value   = WINDOW_TICKS,
}

-- Окно — это только дальность, оно расширяет то, что рейджбот и так делает.
-- Три патча ниже меняют поведение, а не дальность, и могут конфликтовать с
-- Fake Latency, поэтому по умолчанию выключены.
local extra_patches = {
    Patch.new{
        note = "Держать записи валидными",
        sym  = "player_records_cleanup",
        kind = "neutralize",
    },
    Patch.new{
        note = "Без экстраполяции",
        sym  = "should_extrapolate",
        kind = "force_return",
        value = 0,
    },
    Patch.new{
        note = "Записи валидны в fakeduck",
        sym  = "backtrack_valid_while_fakeduck",
        kind = "force_return",
        value = 1,
    },
}

--[[----------------------------------------------------------------------------
    8. Меню (создаём элементы, коллбэки вешаем ниже, когда функции уже есть)
------------------------------------------------------------------------------]]

local group      = ui.create("Rage", "PAW Backtrack")
local enabled    = group:switch("Backtrack Patch", false)
local window     = group:slider("Window", 1, 16, WINDOW_TICKS, 1)
local aggressive = group:switch("Aggressive", false)

window:tooltip("Насколько глубоко нативный бэктрак может откатываться. 8 — сток, 14 — около 218 мс. " ..
               "sv_maxunlag поднимается так, чтобы покрыть это окно ПЛЮС вашу реальную исходящую " ..
               "задержку, поэтому Fake Latency продолжает работать поверх патча.")
aggressive:tooltip("Три поведенческих патча: не чистить записи, не экстраполировать, считать записи " ..
                   "валидными на fakeduck. Если Fake Latency начинает капризничать — выключите.")

--[[----------------------------------------------------------------------------
    9. Логика включения
------------------------------------------------------------------------------]]

local armed_state   = false     -- окно (как минимум) включено
local armed_extra   = false
local saved_unlag   = nil
local applied_unlag = nil
local attempts      = 0
local countdown     = 0

local teardown_now       -- forward: нужен и меню, и событиям
local diagnose_sites
local viera_restore_all  -- forward: определяется в разделе 9.5
local viera_switch       -- forward: там же

local function tick_interval()
    return tonumber(globals.tickinterval) or 0.015625
end

local function active_patches()
    local list = { window_patch }
    if aggressive:get() then
        for _, patch in ipairs(extra_patches) do list[#list + 1] = patch end
    end
    return list
end

local function revert_writes()
    for index = #extra_patches, 1, -1 do extra_patches[index]:disarm() end
    window_patch:disarm()
    armed_extra = false
end

-- Расширенное окно легально только если сервер откатит настолько же, а Fake
-- Latency тратит тот же бюджет: команда приходит на out_latency позже и ПОТОМ
-- откатывается на окно. Значит нужна их СУММА, а не только окно. Значение
-- никогда не понижается нашей рукой и перечитывается каждый раз, чтобы не
-- затереть то, что поднял другой скрипт.
local function needed_unlag()
    local span = window:get() * tick_interval()
    local out = 0
    local channel = utils.net_channel()
    if channel ~= nil then
        local ok, value = pcall(function() return tonumber(channel.latency[0]) end)
        if ok and value ~= nil then out = value end
    end
    return math.min(span + out + UNLAG_HEADROOM, UNLAG_CEILING)
end

-- sv_maxunlag трогаем только если он вообще нашёлся: без него патч окна
-- продолжает работать, просто Fake Latency не получит запаса.
local unlag = cvar ~= nil and cvar.sv_maxunlag or nil
local unlag_missing_reported = false

local function unlag_get()
    if unlag == nil then return nil end
    local ok, value = pcall(function() return unlag:float() end)
    if not ok then return nil end
    return tonumber(value)
end

local function unlag_set(value)
    if unlag == nil then return false end
    local ok = pcall(function() unlag:float(value, true) end)
    return ok
end

local function apply_unlag()
    if not armed_state then return end
    local current = unlag_get()
    if current == nil then
        if not unlag_missing_reported then
            unlag_missing_reported = true
            warn("sv_maxunlag недоступен — окно работает, запаса для Fake Latency не будет")
        end
        return
    end
    local need = needed_unlag()
    if need > current + 0.001 then
        if unlag_set(need) then applied_unlag = need end
    end
end

local function try_arm()
    if resolve_base() == nil then return false, "база модуля не определена" end

    window_patch.value = window:get()

    local list = active_patches()
    for _, patch in ipairs(list) do
        local armed, why = patch:arm()
        if not armed then
            revert_writes()
            return false, why or patch.note
        end
    end

    saved_unlag = unlag_get()
    armed_state = true
    armed_extra = aggressive:get()
    apply_unlag()
    log("включено: окно %d тиков (%.0f мс), патчей %d, sv_maxunlag %s -> %s",
        window:get(), window:get() * tick_interval() * 1000, #list,
        saved_unlag ~= nil and string.format("%.2f", saved_unlag) or "н/д",
        tostring(unlag_get() or "н/д"))
    return true
end

-- sv_maxunlag отдаём назад, только если там всё ещё наше значение: если Fake
-- Latency или другой скрипт поднял его после нас — оно уже их.
local function teardown()
    revert_writes()
    if armed_state and saved_unlag ~= nil then
        local current = unlag_get()
        if current ~= nil then
            if applied_unlag == nil or math.abs(current - applied_unlag) < 0.002 then
                unlag_set(saved_unlag)
                log("sv_maxunlag возвращён: %.2f", saved_unlag)
            else
                log("sv_maxunlag оставлен как есть (%.2f) — его подняли не мы", current)
            end
        end
    end
    armed_state, saved_unlag, applied_unlag = false, nil, nil
end

function teardown_now(reason)
    if viera_restore_all ~= nil then viera_restore_all() end
    if ui ~= nil and viera_switch ~= nil then
        for _, element in ipairs({ viera_switch.preserve, viera_switch.early,
                                   viera_switch.break_dist, viera_switch.defensive }) do
            pcall(function() element:set(false) end)
        end
    end
    if not armed_state then
        revert_writes()
        return
    end
    teardown()
    pcall(function() enabled:set(false) end)
    log("все патчи сняты (%s)", tostring(reason))
end

-- Диагностика: печатает базу, каждый сайт и первые байты функции. Полезно
-- после обновления чита — видно, тот ли это билд, ДО включения патча.
function diagnose_sites()
    local base = resolve_base(true)
    if base == nil then
        warn("база модуля не найдена — KnownOffsets этого билда не подходят")
        return
    end
    for name, symbol in pairs(SYM) do
        local entry = base + (symbol.va - IDA_BASE)
        log("%-32s 0x%08X  %s", name, entry, hexdump(entry, 8))
    end
end

--[[----------------------------------------------------------------------------
    9.5 Патчи из Luas/nl/viera utils.lua

    В viera utils.lua патчи памяти — это отдельные фичи поверх бэктрака:

      * Preserve valid records — байт 0x0F у jne в скане записей -> 0x00,
        условный переход превращается в "jne $+2", т.е. запись не отбрасывается;
      * Early defensive activation — `dec edi` (0x4F) -> NOP, счётчик тиков
        defensive не уменьшается, режим включается на тик раньше;
      * Skip break distance check — в двух местах вызов
        lagrecord_within_break_distance (E8 rel32) -> B0 00 90 90 90 (mov al,0);
      * Defensive on backtrack — в jz-переходе подменяется disp32 на свой стаб
        (mov byte [esi+0x1A],1 ; jmp обратно), т.е. defensive включается и при
        стрельбе по записям бэктрака.

    Все сайты находятся СКАНОМ по байтам внутри модуля чита: адреса в файле
    жёстко не зашиты, и на другой сборке патч просто не найдётся и откажется.

    НЕ перенесено (осознанно, это не байтовые патчи, а подмена кода):
      * extrapolation primary — стаб на VirtualAlloc, который сам вызывает
        внутренние функции чита с их соглашениями вызова;
      * record selection / aim-snap / ass-bait — рукописная замена
        select_lagrecord пещерой ~1.4 КБ машинного кода.
    Такое нельзя проверить обратным чтением, и на другом билде оно молча
    ломает выбор записей — поэтому остаётся в исходном скрипте, а не здесь.
------------------------------------------------------------------------------]]

-- байт-джокер в шаблонах
local PAT_WILD = 0xCC

local function hexdump_from(bytes)
    local out = {}
    for _, byte in ipairs(bytes or {}) do out[#out + 1] = string.format("%02X", byte) end
    return table.concat(out, " ")
end

local viera_patches = {}
local viera_base = nil
local viera_buffer, viera_buffer_base = nil, nil

local function bytes_to_string(bytes, from, count)
    local out = {}
    for index = from, from + count - 1 do out[#out + 1] = string.char(bytes[index]) end
    return table.concat(out)
end

-- Суммарный размер образа: подряд идущие регионы с одним AllocationBase.
local function module_span(base)
    local mbi = ffi.new("PAW_MBI")
    local addr, total = base, 0
    while true do
        if C.VirtualQuery(ffi.cast("void*", addr), mbi, ffi.sizeof("PAW_MBI")) == 0 then break end
        if tonumber(mbi.AllocationBase) ~= base then break end
        local size = tonumber(mbi.RegionSize)
        if size == nil or size == 0 then break end
        total = total + size
        addr = tonumber(mbi.BaseAddress) + size
    end
    return total
end

-- Образ чита читаем кусками по 256 КБ и склеиваем один раз: скан шаблонов
-- идёт string.find'ом по готовому буферу, а не побайтово из Lua.
local CHUNK = 0x40000
local MAX_MODULE = 0x6000000          -- 96 МБ: больше образа не бывает

local function module_bytes(base)
    if viera_buffer ~= nil and viera_buffer_base == base then return viera_buffer end

    local size = module_span(base)
    if size == 0 or size > MAX_MODULE then return nil end

    local pieces = {}
    local offset = 0
    while offset < size do
        local length = math.min(CHUNK, size - offset)
        local ok, piece = pcall(ffi.string, ffi.cast("const char*", base + offset), length)
        if not ok or piece == nil or #piece ~= length then return nil end
        pieces[#pieces + 1] = piece
        offset = offset + length
    end

    viera_buffer, viera_buffer_base = table.concat(pieces), base
    return viera_buffer
end

-- Буфер нужен только пока хоть один шаблон не найден: после этого держать в
-- памяти копию образа чита незачем.
local function release_buffer_if_done()
    for _, patch in ipairs(viera_patches) do
        if patch.site == nil and not patch.disabled then return end
    end
    viera_buffer, viera_buffer_base = nil, nil
end

-- Поиск шаблона по буферу: берём самый длинный непрерывный кусок без
-- джокеров как "иглу" для string.find (plain), затем сверяем шаблон целиком.
local function find_in_buffer(buffer, pattern, offset)
    offset = offset or 0

    local run_start, best_start, best_len = nil, nil, 0
    for index = 1, #pattern do
        if pattern[index] == PAT_WILD then
            run_start = nil
        else
            if run_start == nil then run_start = index end
            local length = index - run_start + 1
            if length > best_len then best_start, best_len = run_start, length end
        end
    end
    if best_start == nil or best_len < 3 then return nil end

    local needle = bytes_to_string(pattern, best_start, best_len)
    local skip = best_start - 1
    local position, matches = 1, 0
    local first = nil
    while true do
        local found = string.find(buffer, needle, position, true)
        if found == nil then break end
        local candidate = found - skip
        if candidate >= 1 and candidate + #pattern - 1 <= #buffer then
            local ok = true
            for index = 1, #pattern do
                local wanted = pattern[index]
                if wanted ~= PAT_WILD and buffer:byte(candidate + index - 1) ~= wanted then
                    ok = false
                    break
                end
            end
            if ok then
                matches = matches + 1
                if first == nil then first = candidate + offset end
                if matches > 1 then break end
            end
        end
        position = found + 1
    end
    if first == nil then return nil end
    return first, matches
end

local function module_address(base, index)
    if base == nil or index == nil then return nil end
    return base + (index - 1)
end

-- Медленная дорога: если KnownOffsets не подошли, ищем подсказку viera
-- (сигнатура в приватной исполняемой памяти) и берём её AllocationBase.
local function find_exec_pattern(pattern)
    local mbi = ffi.new("PAW_MBI")
    local addr = 0x00010000
    while addr < 0x7FFF0000 do
        if C.VirtualQuery(ffi.cast("void*", addr), mbi, ffi.sizeof("PAW_MBI")) == 0 then break end
        local size  = tonumber(mbi.RegionSize)
        if size == nil or size == 0 then break end
        local protect = tonumber(mbi.Protect)
        if mbi.State == MEM_COMMIT
           and bit.band(protect, PAGE_EXECANY) ~= 0
           and bit.band(protect, PAGE_GUARD) == 0
           and bit.band(protect, PAGE_NOACCESS) == 0
           and size <= 0x4000000 then
            local ok, buffer = pcall(ffi.string, ffi.cast("const char*", tonumber(mbi.BaseAddress)), size)
            if ok and buffer ~= nil then
                local index = find_in_buffer(buffer, pattern)
                if index ~= nil then return tonumber(mbi.BaseAddress) + (index - 1) end
            end
        end
        addr = tonumber(mbi.BaseAddress) + size
    end
    return nil
end

local function viera_module_base(force)
    if viera_base ~= nil and not force then return viera_base end
    viera_base = nil

    local base = resolve_base()
    if base == nil then
        local hint = find_exec_pattern({ 0xF3, 0x0F, 0x5B, 0xCA, 0x0F, 0x5B, 0xC9, 0x0F, 0x2E, 0xCB })
        if hint ~= nil then
            local mbi = region_info(hint)
            if mbi ~= nil then base = tonumber(mbi.AllocationBase) end
        end
    end

    viera_base = base
    if base ~= nil then viera_buffer, viera_buffer_base = nil, nil end
    return viera_base
end

local function scan_module(pattern)
    local base = viera_module_base()
    if base == nil then return nil end
    local buffer = module_bytes(base)
    if buffer == nil then return nil end
    local index, matches = find_in_buffer(buffer, pattern)
    if index == nil then return nil end
    if matches ~= nil and matches > 1 then
        log("  (шаблон встречается %d раз, берём первое совпадение)", matches)
    end
    release_buffer_if_done()
    return module_address(base, index)
end

--[[--------------------------- патч -----------------------------------------]]

local VieraPatch = {}
VieraPatch.__index = VieraPatch

local function new_viera_patch(def)
    def.applied = false
    def.disabled = false
    def.saved = nil
    def.site = nil
    viera_patches[#viera_patches + 1] = def
    return setmetatable(def, VieraPatch)
end

function VieraPatch:payload()
    if self.build ~= nil then return self.build(self) end
    return self.patched
end

function VieraPatch:resolve()
    if self.site ~= nil then return self.site end
    local hit = scan_module(self.pattern)
    if hit == nil then return nil end
    self.site = hit + (self.delta or 0)
    return self.site
end

function VieraPatch:apply()
    if self.applied then return true end
    if self.disabled then return false, "патч отключён после неудачной проверки" end

    local site = self:resolve()
    if site == nil then
        return false, "шаблон не найден в модуле чита (другой билд)"
    end

    if not is_exec(site) then return false, "сайт не исполняемый" end

    -- Стаб (defensive on backtrack) нужен ДО сборки payload: disp32 считается
    -- от адреса пещеры, которой ещё нет.
    if self.setup ~= nil then
        local ok, why = self.setup(self, site)
        if not ok then return false, why end
    end

    local payload = self:payload()
    if payload == nil then
        if self.teardown ~= nil then self.teardown(self, site) end
        return false, "payload не собран"
    end

    local current = snapshot(site, #payload)
    local expect = self.expect
    for index = 1, #payload do
        if expect ~= nil and index <= #expect and expect[index] ~= nil
           and current[index] ~= expect[index] then
            self.disabled = true
            if self.teardown ~= nil then self.teardown(self, site) end
            return false, ("байты не совпали: ждём %s, лежит %s"):format(
                hexdump_from(expect), hexdump(site, #payload))
        end
    end
    local padding = true
    for _, byte in ipairs(current) do
        if byte ~= 0xCC and byte ~= 0x90 then padding = false break end
    end
    if padding then
        self.disabled = true
        if self.teardown ~= nil then self.teardown(self, site) end
        return false, "по адресу padding — похоже, другой билд"
    end

    local written, why = raw_write(site, payload)
    if not written then
        if self.teardown ~= nil then self.teardown(self, site) end
        return false, tostring(why)
    end

    self.saved, self.applied, self.site = current, true, site
    return true
end

function VieraPatch:restore()
    if not self.applied then return true end
    local ok = true
    if self.saved ~= nil then
        local written = raw_write(self.site, self.saved)
        ok = written == true
    end
    if self.teardown ~= nil then self.teardown(self, self.site) end
    self.applied, self.saved = false, nil
    return ok
end

--[[--------------------------- сайты ----------------------------------------]]

local viera_preserve_records = new_viera_patch{
    name    = "Preserve valid records (viera)",
    pattern = { 0xA9, 0x08, 0x08, 0x00, 0x00, 0x75, 0x0F, 0x43 },
    delta   = 6,
    expect  = { 0x0F },
    patched = { 0x00 },
}

local viera_early_defensive = new_viera_patch{
    name    = "Early defensive activation (viera)",
    pattern = { 0x4F, 0x3B, 0x3D, PAT_WILD, PAT_WILD, PAT_WILD, PAT_WILD,
                0x0F, 0x8F, PAT_WILD, PAT_WILD, PAT_WILD, PAT_WILD, 0x8B, 0x45 },
    delta   = 0,
    expect  = { 0x4F },
    patched = { 0x90 },
}

local viera_break_dist_a = new_viera_patch{
    name    = "Skip break distance A (viera)",
    pattern = { 0x49, 0x21, 0xD9, 0x8B, 0x0C, 0x88,
                0xE8, PAT_WILD, PAT_WILD, PAT_WILD, PAT_WILD,
                0x84, 0xC0, 0x0F, 0x84 },
    delta   = 6,
    expect  = { 0xE8 },
    patched = { 0xB0, 0x00, 0x90, 0x90, 0x90 },
}

local viera_break_dist_b = new_viera_patch{
    name    = "Skip break distance B (viera)",
    pattern = { 0x48, 0x21, 0xD8, 0x8B, 0x0C, 0x81,
                0xE8, PAT_WILD, PAT_WILD, PAT_WILD, PAT_WILD,
                0x84, 0xC0, 0x74 },
    delta   = 6,
    expect  = { 0xE8 },
    patched = { 0xB0, 0x00, 0x90, 0x90, 0x90 },
}

-- Defensive on backtrack: jz-переход внутри гейта defensive ведёт в свой стаб.
-- Стаб: mov byte [esi+0x1A], 1 ; jmp обратно за переход.
local viera_defensive_bt = new_viera_patch{
    name    = "Defensive on backtrack (viera)",
    pattern = { 0x80, 0x3D, PAT_WILD, PAT_WILD, PAT_WILD, PAT_WILD, 0x00,
                0x0F, 0x84, PAT_WILD, PAT_WILD, PAT_WILD, PAT_WILD,
                0x83, 0x3D, PAT_WILD, PAT_WILD, PAT_WILD, PAT_WILD, 0x00,
                0x0F, 0x8E, PAT_WILD, PAT_WILD, PAT_WILD, PAT_WILD,
                0xB3, 0x01 },
    delta   = 9,
    stub_size = 32,

    build = function(self)
        local site = self.site
        if site == nil or self.stub == nil then return nil end
        local displacement = self.stub_address - (site + 4)
        return {
            bit.band(displacement, 0xFF),
            bit.band(bit.rshift(displacement, 8), 0xFF),
            bit.band(bit.rshift(displacement, 16), 0xFF),
            bit.band(bit.rshift(displacement, 24), 0xFF),
        }
    end,

    setup = function(self, site)
        local stub = C.VirtualAlloc(nil, self.stub_size, MEM_COMMIT_RESERVE, PAGE_RWX)
        if stub == nil then return false, "VirtualAlloc не выдал память под стаб" end

        local address = tonumber(ffi.cast("uintptr_t", stub))
        local jump_target = site + 4                    -- за jz-переходом
        local stub_bytes = { 0xC6, 0x46, 0x1A, 0x01, 0xE9 }

        local displacement = jump_target - (address + 9)
        stub_bytes[6] = bit.band(displacement, 0xFF)
        stub_bytes[7] = bit.band(bit.rshift(displacement, 8), 0xFF)
        stub_bytes[8] = bit.band(bit.rshift(displacement, 16), 0xFF)
        stub_bytes[9] = bit.band(bit.rshift(displacement, 24), 0xFF)

        local old = ffi.new("unsigned int[1]")
        if C.VirtualProtect(stub, #stub_bytes, PAGE_RWX, old) == 0 then
            C.VirtualFree(stub, 0, MEM_RELEASE)
            return false, "VirtualProtect на стабе не сработал"
        end
        local pointer = ffi.cast("uint8_t*", stub)
        for index = 1, #stub_bytes do pointer[index - 1] = stub_bytes[index] end
        C.FlushInstructionCache(C.GetCurrentProcess(), stub, #stub_bytes)

        self.stub, self.stub_address = stub, address
        return true
    end,

    teardown = function(self)
        if self.stub ~= nil then
            C.VirtualFree(self.stub, 0, MEM_RELEASE)
        end
        self.stub, self.stub_address = nil, nil
    end,
}

--[[--------------------------- меню -----------------------------------------]]

local viera_group = ui.create("Rage", "NL Backtrack Fixes")

viera_switch = {
    preserve   = viera_group:switch("Preserve valid records", false),
    early      = viera_group:switch("Early defensive activation", false),
    break_dist = viera_group:switch("Skip break distance check", false),
    defensive  = viera_group:switch("Defensive on backtrack", false),
}

viera_switch.preserve:tooltip("Не отбрасывать записи, помеченные валидными, при скане бэктрака.")
viera_switch.early:tooltip("Defensive включается на один тик раньше: пропускается decrement счётчика.")
viera_switch.break_dist:tooltip("Убрать вызов lagrecord_within_break_distance в двух местах выбора записи.")
viera_switch.defensive:tooltip("Defensive включается и при стрельбе по записям бэктрака, а не только по текущей позиции.")

local viera_diagnose_button = viera_group:button("Read sites", function()
    local base = viera_module_base(true)
    if base == nil then
        warn("база модуля не найдена — сайты viera проверить нельзя")
        return
    end
    log("модуль чита 0x%08X, образ %.2f МБ", base, module_span(base) / 1048576)
    for _, patch in ipairs(viera_patches) do
        local site = patch:resolve()
        if site == nil then
            log("  %-34s шаблон не найден", patch.name)
        else
            log("  %-34s 0x%08X  %s", patch.name, site, hexdump(site, 8))
        end
    end
end)

--[[--------------------------- состояние -----------------------------------]]

local viera_state = { attempts = 0, cooldown = 0, updated = false }

local function viera_wanted()
    return ui.get(viera_switch.preserve) == true
        or ui.get(viera_switch.early) == true
        or ui.get(viera_switch.break_dist) == true
        or ui.get(viera_switch.defensive) == true
end

local function viera_report(patch, ok, why)
    if ok then
        log("%s: включён @0x%08X", patch.name, patch.site or 0)
    else
        warn("%s: НЕ включён — %s", patch.name, tostring(why))
    end
end

local function viera_update()
    local pairs_list = {
        { viera_switch.preserve,   viera_preserve_records },
        { viera_switch.early,      viera_early_defensive },
        { viera_switch.break_dist, viera_break_dist_a },
        { viera_switch.defensive,  viera_defensive_bt },
    }

    for _, entry in ipairs(pairs_list) do
        local element, patch = entry[1], entry[2]
        if ui.get(element) == true then
            if not patch.applied then
                local ok, why = patch:apply()
                viera_report(patch, ok, why)
                if not ok then ui.set(element, false) end
            end
        elseif patch.applied then
            patch:restore()
            log("%s: снят, байты возвращены", patch.name)
        end
    end

    -- Второй сайт break distance идёт парой к первому.
    local extra = viera_break_dist_b
    if ui.get(viera_switch.break_dist) == true then
        if not extra.applied and viera_break_dist_a.applied then
            local ok, why = extra:apply()
            if not ok then warn("%s: НЕ включён — %s", extra.name, tostring(why)) end
        end
    elseif extra.applied then
        extra:restore()
    end

    viera_state.updated = viera_wanted()
end

function viera_restore_all()
    for index = #viera_patches, 1, -1 do viera_patches[index]:restore() end
    viera_state.attempts, viera_state.cooldown = 0, 0
end

-- Модуль чита может быть ещё не замэплен в момент загрузки скрипта, поэтому
-- включённые галочки дотягиваются по тикам (с ограничением числа попыток).
for _, entry in ipairs({ { viera_switch.preserve }, { viera_switch.early },
                         { viera_switch.break_dist }, { viera_switch.defensive } }) do
    local element = entry[1]
    element:set_callback(function()
        viera_state.attempts, viera_state.cooldown = 0, 0
        viera_update()
    end, true)
end

local function viera_poll()
    if not viera_wanted() then return end
    if viera_preserve_records.applied
       or viera_early_defensive.applied
       or viera_break_dist_a.applied
       or viera_defensive_bt.applied then
        return
    end
    if viera_state.attempts >= 8 then return end
    viera_state.cooldown = viera_state.cooldown - 1
    if viera_state.cooldown > 0 then return end
    viera_state.cooldown = 64
    viera_state.attempts = viera_state.attempts + 1
    viera_update()
end

--[[----------------------------------------------------------------------------
    10. Коллбэки меню
------------------------------------------------------------------------------]]

local revert_button   = group:button("Revert now", function() teardown_now("кнопка") end)
local diagnose_button = group:button("Diagnose", function() diagnose_sites() end)
local panic_hotkey    = group:hotkey("Panic revert", 0)

window:set_callback(function(slider)
    window_patch:set_value(slider:get())
    apply_unlag()
end)

aggressive:set_callback(function(switch)
    if not armed_state or switch:get() == armed_extra then return end
    if switch:get() then
        for _, patch in ipairs(extra_patches) do patch:arm() end
    else
        for index = #extra_patches, 1, -1 do extra_patches[index]:disarm() end
    end
    armed_extra = switch:get()
end)

enabled:set_callback(function(switch)
    attempts, countdown = 0, 0
    if switch:get() then
        if not armed_state then try_arm() end
    else
        teardown()
    end
end, true)

--[[----------------------------------------------------------------------------
    11. Тик и выгрузка
------------------------------------------------------------------------------]]

-- База появляется только когда модуль чита уже мэпнут, поэтому включение при
-- загрузке или из меню может честно не сработать. Повторяем по тикам (curtime
-- сбрасывается между картами, счётчик тиков — нет), потом прекращаем и
-- говорим об этом один раз.
events.net_update_end:set(function()
    -- Паник-хоткей проверяем всегда, даже когда патч выключен.
    if panic_hotkey ~= nil and panic_hotkey:get() and armed_state then
        teardown_now("паник-хоткей")
        return
    end

    -- viera-патчи живут отдельными галочками и не зависят от окна бэктрака.
    if viera_poll ~= nil then viera_poll() end

    if not enabled:get() then return end

    if armed_state then
        countdown = countdown - 1
        if countdown > 0 then return end
        countdown = UNLAG_RECHECK
        apply_unlag()
        return
    end

    if attempts >= MAX_ATTEMPTS then return end

    countdown = countdown - 1
    if countdown > 0 then return end
    countdown = RETRY_TICKS

    attempts = attempts + 1
    local armed, why = try_arm()
    if not armed then
        if attempts == 1 then
            warn("включить не удалось: %s; повторяю каждые %d тиков", tostring(why), RETRY_TICKS)
        end
        if attempts >= MAX_ATTEMPTS then
            warn("не удалось включить за %d попыток (%s): билд рейджбота изменился, ничего не записано",
                 MAX_ATTEMPTS, tostring(why))
        end
    end
end)

events.shutdown:set(function() teardown_now("shutdown") end, true)

log("загружен: %d сайтов в карте, окно по умолчанию %d тиков", SYM_COUNT, WINDOW_TICKS)
