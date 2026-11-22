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
    unsigned int __stdcall GetCurrentThreadId(void);
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
local viera_group        -- forward: группа меню, раздел 9.5
local viera2_restore_all -- forward: раздел 9.6
local viera2_poll        -- forward: раздел 9.6
local persistence_restore_all -- forward: раздел 9.7
local persistence_poll   -- forward: раздел 9.7
local rage_restore_all   -- forward: раздел 9.8
local rage_poll          -- forward: раздел 9.8
local viera_switch       -- forward: там же
local viera2_switch      -- forward: раздел 9.6

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
    if viera2_restore_all ~= nil then viera2_restore_all() end
    if persistence_restore_all ~= nil then persistence_restore_all() end
    if rage_restore_all ~= nil then rage_restore_all() end
    if ui ~= nil and viera_switch ~= nil then
        for _, element in ipairs({ viera_switch.preserve, viera_switch.early,
                                   viera_switch.break_dist, viera_switch.defensive }) do
            pcall(function() element:set(false) end)
        end
        if viera2_switch ~= nil then
            for _, element in ipairs({ viera2_switch.extrap, viera2_switch.rs, viera2_switch.fb }) do
                pcall(function() element:set(false) end)
            end
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

    Остальные патчи того же файла перенесены в разделе 9.6: стаб
    extrapolation primary и пещеры выбора записи (там же — почему это
    отдельный раздел).
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

viera_group = ui.create("Rage", "NL Backtrack Fixes")

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
    9.6 Патчи viera utils: экстраполяция и выбор записей (полный перенос)

    Здесь перенесено то, что в viera utils.lua тоже патчит память, но не
    сводится к одному байту:

      * Extrapolation primary — в вызов should_extrapolate ставится свой стаб:
        если breaklc == 0 или hittable != 0 — вызывается настоящая функция,
        иначе сразу возвращается 1. Соглашение вызова не меняется: вместо
        call на то же место стоит jmp на стаб, стаб делает call и jmp назад,
        поэтому возврат уходит ровно тому, кто вызвал.
        (В оригинале функция установки нигде не вызывается — мёртвый код;
        здесь она за отдельным переключателем, по умолчанию выключена.)

      * Custom record selection / fallback — рукописные пещеры выбора записи:
        cave (главный путь select_lagrecord, 39 байт сайта -> jmp),
        стаб entindex (перехват GetEntityIndex, 5 байт),
        cave2 (fallback-выбор записи + патч fb_sig, 6 байт),
        cave3 (animfix thirdperson_recoil, 16 байт сайта -> jmp).
        Пещеры заполняются теми же байтами, что в оригинале; адреса
        (g_cmd_ctx, g_entindex, g_is_firing, g_tpr_arr и т.д.) считаются от
        реально найденных сайтов, а не зашиты числами.

    Отличие от оригинала только в обвязке: перед каждой записью проверяются
    байты сайта, каждая запись подтверждается обратным чтением, при отказе
    пещера освобождается, а не остаётся висеть аллоцированной.
------------------------------------------------------------------------------]]

local RS_SIMTIME   = 0x24          -- lag_record_t.simulation_time
local RING_RECORDS = 0x04          -- lag_ringbuf_t.records
local RING_CAPACITY, RING_HEAD, RING_COUNT = 0x08, 0x0C, 0x10
local MAX_RING_INDEX = 64
local RING_STRIDE    = 0x10940

local ZERO_260 = {}
for index = 1, 65 * 4 do ZERO_260[index] = 0 end

local function vread_u8(addr)
    local ok, value = pcall(function() return tonumber(ffi.cast("uint8_t*", addr)[0]) end)
    if ok then return value end
    return nil
end

local function vread_i8(addr)
    local ok, value = pcall(function() return tonumber(ffi.cast("int8_t*", addr)[0]) end)
    if ok then return value end
    return nil
end

local function vread_u32(addr)
    local ok, value = pcall(function() return tonumber(ffi.cast("uint32_t*", addr)[0]) end)
    if ok then return value end
    return nil
end

local function vread_i32(addr)
    local ok, value = pcall(function() return tonumber(ffi.cast("int32_t*", addr)[0]) end)
    if ok then return value end
    return nil
end

local function vread_f32(addr)
    local ok, value = pcall(function() return tonumber(ffi.cast("float*", addr)[0]) end)
    if ok then return value end
    return nil
end

local function vwrite_f32(addr, value)
    local ok = pcall(function() ffi.cast("float*", addr)[0] = value end)
    return ok
end

local function vflush(addr, size)
    pcall(function() C.FlushInstructionCache(C.GetCurrentProcess(), ffi.cast("void*", addr), size) end)
end

local function le32_into(target, value)
    value = math.floor(tonumber(value) or 0) % 0x100000000
    target[#target + 1] = value % 0x100
    target[#target + 1] = math.floor(value / 0x100) % 0x100
    target[#target + 1] = math.floor(value / 0x10000) % 0x100
    target[#target + 1] = math.floor(value / 0x1000000) % 0x100
end

local function copy_bytes(source)
    local out = {}
    for index = 1, #source do out[index] = source[index] end
    return out
end

local function patch_le32(bytes, offset, value)
    value = math.floor(tonumber(value) or 0) % 0x100000000
    bytes[offset + 1] = value % 0x100
    bytes[offset + 2] = math.floor(value / 0x100) % 0x100
    bytes[offset + 3] = math.floor(value / 0x10000) % 0x100
    bytes[offset + 4] = math.floor(value / 0x1000000) % 0x100
end

local function executable_bytes(addr, count)
    local mbi = region_info(addr)
    if mbi == nil then return false end
    if mbi.State ~= MEM_COMMIT then return false end
    local protect = tonumber(mbi.Protect)
    return bit.band(protect, PAGE_EXECANY) ~= 0
       and bit.band(protect, PAGE_GUARD) == 0
       and bit.band(protect, PAGE_NOACCESS) == 0
       and tonumber(mbi.RegionSize) >= count
end

local function readable(addr)
    local mbi = region_info(addr)
    if mbi == nil then return false end
    if mbi.State ~= MEM_COMMIT then return false end
    return bit.band(tonumber(mbi.Protect), 0x101) == 0
end

--[[--------------------------- экстраполяция -------------------------------]]

local extrap_state = { applied = false, site = nil, stub = nil, address = nil, saved = nil }

local function extrap_find_site()
    if extrap_state.site ~= nil then return extrap_state.site end
    local hit = scan_module({ 0xE8, PAT_WILD, PAT_WILD, PAT_WILD, PAT_WILD,
                              0x84, 0xC0, 0x74, PAT_WILD,
                              0x89, 0xD9, 0xFF, 0x74, 0x24, 0x24 })
    if hit == nil then return nil end
    extrap_state.site = hit
    return hit
end

-- Глобалы breaklc / hittable лежат в операндах cmp прямо в коде чита, поэтому
-- их не надо ни искать отдельно, ни зашивать числом.
local function extrap_find_globals()
    local hit = scan_module({ 0xB1, 0x01, 0x31, 0xC0,
                              0x80, 0x3D, PAT_WILD, PAT_WILD, PAT_WILD, PAT_WILD, 0x00,
                              0x0F, 0x85, PAT_WILD, PAT_WILD, PAT_WILD, PAT_WILD,
                              0x80, 0x3D, PAT_WILD, PAT_WILD, PAT_WILD, PAT_WILD, 0x00,
                              0x0F, 0x84 })
    if hit == nil then return nil end
    local hittable = vread_u32(hit + 6)
    local breaklc  = vread_u32(hit + 19)
    if hittable == nil or breaklc == nil then return nil end
    return hittable, breaklc
end

local function extrap_install()
    if extrap_state.applied then return true end

    local site = extrap_find_site()
    if site == nil then return false, "шаблон вызова should_extrapolate не найден" end
    if not executable_bytes(site, 5) then return false, "сайт не исполняемый" end

    local opcode = vread_u8(site)
    if opcode ~= 0xE8 then
        return false, ("по сайту не call, а 0x%02X — другой билд"):format(opcode or 0)
    end

    local hittable, breaklc = extrap_find_globals()
    if hittable == nil then return false, "шаблон глобалов breaklc/hittable не найден" end

    local target = site + 5 + (vread_i32(site + 1) or 0)

    local stub = C.VirtualAlloc(nil, 96, MEM_COMMIT_RESERVE, PAGE_RWX)
    if stub == nil then return false, "VirtualAlloc под стаб не сработал" end
    local address = tonumber(ffi.cast("uintptr_t", stub))

    local code = { 0x83, 0x3D }
    le32_into(code, breaklc)
    code[#code + 1] = 0x00            -- cmp dword [breaklc], 0
    code[#code + 1] = 0x74
    code[#code + 1] = 0x10            -- jz do_real_call
    code[#code + 1] = 0x83
    code[#code + 1] = 0x3D
    le32_into(code, hittable)
    code[#code + 1] = 0x00            -- cmp dword [hittable], 0
    code[#code + 1] = 0x75
    code[#code + 1] = 0x07            -- jnz do_real_call
    for _, byte in ipairs({ 0xB8, 0x01, 0x00, 0x00, 0x00, 0xEB, 0x05 }) do
        code[#code + 1] = byte        -- mov eax,1 ; jmp ret_jmp
    end
    code[#code + 1] = 0xE8            -- call should_extrapolate
    le32_into(code, target - (address + 30))
    code[#code + 1] = 0xE9            -- jmp назад
    le32_into(code, (site + 5) - (address + 35))

    if not raw_write(address, code) then
        C.VirtualFree(stub, 0, MEM_RELEASE)
        return false, "не удалось записать стаб"
    end
    vflush(address, #code)

    local saved = snapshot(site, 5)
    local patch = { 0xE9 }
    le32_into(patch, address - (site + 5))
    if not raw_write(site, patch) then
        C.VirtualFree(stub, 0, MEM_RELEASE)
        return false, "не удалось переписать call на стаб"
    end
    vflush(site, 5)

    extrap_state.applied, extrap_state.stub, extrap_state.address = true, stub, address
    extrap_state.saved = saved
    log("Extrapolation primary: стаб @0x%08X, сайт 0x%08X", address, site)
    return true
end

local function extrap_restore()
    if not extrap_state.applied then return true end
    local ok = true
    if extrap_state.saved ~= nil then
        ok = raw_write(extrap_state.site, extrap_state.saved) == true
        vflush(extrap_state.site, 5)
    end
    if extrap_state.stub ~= nil then C.VirtualFree(extrap_state.stub, 0, MEM_RELEASE) end
    extrap_state.applied, extrap_state.stub, extrap_state.address, extrap_state.saved = false, nil, nil, nil
    log("Extrapolation primary: снят, call возвращён")
    return ok
end

--[[--------------------------- выбор записей --------------------------------]]

local rs = {
    applied = false,
    cave = nil,
    cave_block = nil,
    sites = {},
    saved = {},
    is_firing = nil,
    tpr_arr = nil,
    thirdperson_recoil = nil,
    table_ptr = nil,
    fb_applied = false,
    fb_saved = nil,
    af_applied = false,
    af_saved = nil,
    eix_applied = false,
    eix_saved = nil,
}

local function rs_resolve()
    local sites = rs.sites
    if sites.rs ~= nil then return true end

    sites.rs = scan_module({ 0xF6, 0x81, 0xB0, 0x00, 0x00, 0x00, 0x08,
                             0xF3, 0x0F, 0x10, 0x0C, 0x24,
                             0x0F, 0x85, PAT_WILD, PAT_WILD, PAT_WILD, PAT_WILD,
                             0x8B, 0x04, 0x87,
                             0x80, 0xB8, 0xA4, 0x00, 0x00, 0x00, 0x00 })

    local eix = scan_module({ 0x8D, 0x59, 0x08, 0x8B, 0x41, 0x08, 0x89, 0xD9,
                              0xFF, 0x50, 0x28, 0x83, 0xF8, 0x41 })
    if eix ~= nil then sites.rs_eix = eix + 11 end

    sites.fb = scan_module({ 0x8B, 0x0D, PAT_WILD, PAT_WILD, PAT_WILD, PAT_WILD,
                             0x8D, 0x87, 0xA4, 0x00, 0x00, 0x00,
                             0xF3, 0x0F, 0x10, 0x87, 0x94, 0x00, 0x00, 0x00 })

    sites.fn_valid = scan_module({ 0x83, 0x79, 0x10, 0x02, 0x73, 0x03, 0x31, 0xC0,
                                   0xC3, 0x56, 0x8B, 0x41, 0x0C })

    sites.fn_expired = scan_module({ 0x56, 0xA1, PAT_WILD, PAT_WILD, PAT_WILD, PAT_WILD,
                                     0x89, 0xC2, 0xC1, 0xEA, 0x1F, 0x01, 0xC2, 0xD1, 0xFA })

    local ret = scan_module({ 0xFF, 0x74, 0x24, PAT_WILD, 0xFF, 0x74, 0x24, PAT_WILD,
                              0xE8, PAT_WILD, PAT_WILD, PAT_WILD, PAT_WILD,
                              0x89, 0xC6, 0x8B, 0x4C, 0x24, 0x0C, 0x31, 0xE1 })
    if ret ~= nil then sites.addr_ret = ret + 15 end

    sites.af = scan_module({ 0xF3, 0x0F, 0x10, 0x92, 0x70, 0x03, 0x00, 0x00,
                             0xF3, 0x0F, 0x11, 0x90, 0x80, 0x01, 0x00, 0x00 })

    return sites.rs ~= nil
end

-- Указатель на таблицу колец записей: берётся из mov [eax+disp],imm32 в коде.
local function rs_ring_table_pointer()
    if rs.table_ptr ~= nil then return rs.table_ptr end
    local hit = scan_module({ 0x89, 0x86, 0xB0, 0x00, 0x00, 0x00,
                              0xA1, PAT_WILD, PAT_WILD, PAT_WILD, PAT_WILD,
                              0x89, 0x86, 0xAC, 0x00, 0x00, 0x00 })
    if hit == nil then return nil end
    rs.table_ptr = vread_u32(hit + 7)
    return rs.table_ptr
end

local function rs_ring_table()
    local pointer = rs_ring_table_pointer()
    if pointer == nil then return nil end
    local table_base = vread_u32(pointer)
    if table_base == nil or table_base == 0 then return nil end
    return table_base
end

local function rs_ring(index)
    if index == nil or index < 0 or index > MAX_RING_INDEX then return nil end
    local table_base = rs_ring_table()
    if table_base == nil then return nil end
    local ring = table_base + index * RING_STRIDE
    local records  = vread_u32(ring + RING_RECORDS)
    local capacity = vread_i32(ring + RING_CAPACITY)
    local count    = vread_i32(ring + RING_COUNT)
    if records == nil or records == 0 or capacity == nil or capacity <= 0 or count == nil then
        return nil
    end
    return ring, records, capacity, count
end

local function rs_record(ring, records, capacity, count, offset)
    if count <= 0 or offset < 0 or offset >= count then return nil end
    local head = vread_i32(ring + RING_HEAD)
    if head == nil then return nil end
    local slot = bit.band(head + offset, capacity - 1)
    local record = vread_u32(records + slot * 4)
    if record == nil or record == 0 or not readable(record) then return nil end
    return record
end

-- Таблица «стрелял ли игрок» и thirdperson_recoil для пещер: заполняется каждый
-- createmove, ровно как в оригинале (иначе пещеры читают мусор).
local function rs_fill_tables()
    if not rs.applied or rs.is_firing == nil then return end
    if rs.tpr_arr ~= nil then raw_write(rs.tpr_arr, ZERO_260) end
    raw_write(rs.is_firing, ZERO_260)

    if entity == nil or entity.get_players == nil then return end
    local ok, players = pcall(entity.get_players, true)
    if not ok or type(players) ~= "table" then return end

    for _, player in ipairs(players) do
        local alive = false
        if entity.is_alive ~= nil then
            local ok_alive, value = pcall(entity.is_alive, player)
            alive = ok_alive and value == true
        end
        if alive then
            local ok_index, index = pcall(function() return player:get_index() end)
            if ok_index and type(index) == "number" and index >= 0 and index < 65 then
                local ring, records, capacity, count = rs_ring(index)
                if ring ~= nil then
                    local newest = rs_record(ring, records, capacity, count, 0)
                    if newest ~= nil then
                        local oldest = count > 1 and rs_record(ring, records, capacity, count, count - 1) or nil

                        local last_shot = 0
                        local ok_weapon, weapon = pcall(function() return player:get_player_weapon() end)
                        if ok_weapon and weapon ~= nil then
                            local ok_shot, value = pcall(function() return weapon.m_fLastShotTime end)
                            if ok_shot and type(value) == "number" then last_shot = value end
                        end

                        local newest_time = vread_f32(newest + RS_SIMTIME) or 0
                        local oldest_time = oldest ~= nil and vread_f32(oldest + RS_SIMTIME) or nil
                        local firing = oldest_time ~= nil
                                   and last_shot > 0
                                   and newest_time >= last_shot
                                   and last_shot > oldest_time

                        vwrite_f32(rs.is_firing + index * 4, firing and 1.0 or 0.0)

                        if rs.tpr_arr ~= nil and rs.thirdperson_recoil ~= nil then
                            local ok_tpr, value = pcall(function()
                                return ffi.cast("float*",
                                    ffi.cast("uintptr_t", player[0]) + rs.thirdperson_recoil)[0]
                            end)
                            vwrite_f32(rs.tpr_arr + index * 4, ok_tpr and value or 0)
                        end
                    end
                end
            end
        end
    end
end

local function rs_cave()
    if rs.cave ~= nil then return rs.cave end
    local block = C.VirtualAlloc(nil, 4096, MEM_COMMIT_RESERVE, PAGE_RWX)
    if block == nil then return nil end
    rs.cave = tonumber(ffi.cast("uintptr_t", block))
    rs.cave_block = block
    return rs.cave
end

local function rs_free_cave()
    if rs.cave_block ~= nil then C.VirtualFree(rs.cave_block, 0, MEM_RELEASE) end
    rs.cave, rs.cave_block = nil, nil
end

local function rs_install()
    if rs.applied then return true end
    if not rs_resolve() then return false, "шаблоны выбора записей не найдены" end

    local sites = rs.sites
    if sites.fn_valid == nil or sites.fn_expired == nil or sites.addr_ret == nil then
        return false, "нет fn_valid / fn_expired / addr_ret — другой билд"
    end
    if vread_u8(sites.rs) == 0xE9 then return false, "сайт уже пропатчен (не нами)" end
    if not executable_bytes(sites.rs, 39) then return false, "сайт не исполняемый" end

    local cave = rs_cave()
    if cave == nil then return false, "VirtualAlloc под пещеры не сработал" end

    local site = sites.rs
    local addr_527 = site + 34 + (vread_i8(site + 33) or 0)
    local addr_7e2 = site + 39 + (vread_i32(site + 35) or 0)
    local g_cmd_ctx = vread_u32(addr_7e2 + 8)
    if g_cmd_ctx == nil then return false, "не удалось прочитать g_cmd_ctx" end

    local d_8f, d_180, d_n180, d_360 = cave + 0x15A, cave + 0x15E, cave + 0x162, cave + 0x166
    local g_entindex = cave + 0x26E
    local g_is_firing = cave + 0x3B0
    local g_tpr_arr   = cave + 0x4B8

    -- ===== cave1: основной путь выбора записи =====
    local code = copy_bytes({
        0x56, 0x57, 0x52, 0x51, 0x8B, 0x74, 0x24, 0x18, 0x8B, 0x46, 0x0C, 0x03,
        0x46, 0x10, 0x48, 0x8B, 0x72, 0x08, 0x4E, 0x21, 0xF0, 0x8B, 0x34, 0x87,
        0xA1, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x80, 0x6C, 0x01, 0x00, 0x00, 0x3B,
        0x81, 0x84, 0x00, 0x00, 0x00, 0x0F, 0x84, 0x03, 0x01, 0x00, 0x00, 0xA1,
        0x00, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x10, 0x04, 0x85, 0x00, 0x00, 0x00,
        0x00, 0x0F, 0x57, 0xC9, 0x0F, 0x2E, 0xC1, 0x0F, 0x87, 0xA4, 0x00, 0x00,
        0x00, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x8B, 0x0C, 0x24, 0xF3, 0x0F,
        0x10, 0x01, 0xF3, 0x0F, 0x5C, 0x06, 0xF3, 0x0F, 0x59, 0xC0, 0xF3, 0x0F,
        0x10, 0x49, 0x04, 0xF3, 0x0F, 0x5C, 0x4E, 0x04, 0xF3, 0x0F, 0x59, 0xC9,
        0xF3, 0x0F, 0x58, 0xC1, 0xF3, 0x0F, 0x10, 0x49, 0x08, 0xF3, 0x0F, 0x5C,
        0x4E, 0x08, 0xF3, 0x0F, 0x59, 0xC9, 0xF3, 0x0F, 0x58, 0xC1, 0xF3, 0x0F,
        0x51, 0xC0, 0x0F, 0x2E, 0x05, 0x00, 0x00, 0x00, 0x00, 0x73, 0x46, 0xF3,
        0x0F, 0x10, 0x41, 0x68, 0xF3, 0x0F, 0x5C, 0x46, 0x68, 0x0F, 0x2E, 0x05,
        0x00, 0x00, 0x00, 0x00, 0x76, 0x08, 0xF3, 0x0F, 0x5C, 0x05, 0x00, 0x00,
        0x00, 0x00, 0xF3, 0x0F, 0x10, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x2E,
        0xC8, 0x76, 0x08, 0xF3, 0x0F, 0x58, 0x05, 0x00, 0x00, 0x00, 0x00, 0x0F,
        0x57, 0xC9, 0xF3, 0x0F, 0x5C, 0xC8, 0xF3, 0x0F, 0x5F, 0xC1, 0x0F, 0x2E,
        0x05, 0x00, 0x00, 0x00, 0x00, 0x77, 0x02, 0xEB, 0x03, 0x8B, 0x34, 0x24,
        0x8B, 0x4C, 0x24, 0x18, 0xB8, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xD0, 0x84,
        0xC0, 0x74, 0x0F, 0x8B, 0xCE, 0xB8, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xD0,
        0x84, 0xC0, 0x75, 0x02, 0xEB, 0x1C, 0x8B, 0x54, 0x24, 0x04, 0x8B, 0x7C,
        0x24, 0x08, 0x8B, 0x44, 0x24, 0x18, 0x8B, 0x48, 0x0C, 0x03, 0x48, 0x10,
        0x49, 0x8B, 0x42, 0x08, 0x48, 0x21, 0xC1, 0x8B, 0x34, 0x8F, 0x83, 0xC4,
        0x10, 0xE9, 0x00, 0x00, 0x00, 0x00, 0x59, 0x5A, 0x5F, 0x5E, 0x8B, 0x42,
        0x08, 0x48, 0x21, 0xD8, 0xF3, 0x0F, 0x10, 0x0C, 0x24, 0x8B, 0x04, 0x87,
        0x80, 0xB8, 0xA4, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x7C, 0x24, 0x08, 0x0F,
        0x85, 0x00, 0x00, 0x00, 0x00, 0xE9, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x41, 0x00, 0x00, 0x34, 0x43, 0x00, 0x00, 0x34, 0xC3, 0x00, 0x00,
        0xB4, 0x43,
    })
    local function fix(offset, value)
        patch_le32(code, offset, value)
    end
    fix(0x19,  g_cmd_ctx)
    fix(0x30,  g_entindex)
    fix(0x39,  g_is_firing)
    fix(0xA1,  d_8f)
    fix(0xB4,  d_180)
    fix(0xBE,  d_360)
    fix(0xC6,  d_n180)
    fix(0xD3,  d_360)
    fix(0xE5,  d_8f)
    fix(0xF5,  sites.fn_valid)
    fix(0x102, sites.fn_expired)
    fix(0x12E, sites.addr_ret - (cave + 0x132))
    fix(0x151, addr_527 - (cave + 0x155))
    fix(0x156, addr_7e2 - (cave + 0x15A))

    if not raw_write(cave, code) then
        rs_free_cave()
        return false, "не удалось записать пещеру выбора записи"
    end
    if #code ~= 0x16A then
        log("  cave1: %d байт вместо 0x16A — таблица байтов изменена", #code)
    end

    -- ===== стаб entindex (перехват GetEntityIndex) =====
    if sites.rs_eix ~= nil then
        local eix_stub = cave + 0x272
        local eix_site = sites.rs_eix
        local eix = copy_bytes({
        0xA3, 0x00, 0x00, 0x00, 0x00, 0x83, 0xF8, 0x41, 0x72, 0x05, 0xE9, 0x00,
        0x00, 0x00, 0x00, 0xE9, 0x00, 0x00, 0x00, 0x00,
        })
        patch_le32(eix, 0x01, g_entindex)
        patch_le32(eix, 0x0B, (eix_site + 5) - (eix_stub + 0x0F))
        patch_le32(eix, 0x10, (eix_site + 5 + 0x26) - (eix_stub + 0x14))

        if raw_write(eix_stub, eix) then
            if vread_u8(eix_site) ~= 0xE9 then
                local patched = { 0xE9 }
                le32_into(patched, eix_stub - (eix_site + 5))
                local saved = snapshot(eix_site, 5)
                if raw_write(eix_site, patched) then
                    rs.eix_saved, rs.eix_applied = saved, true
                    vflush(eix_site, 5)
                end
            end
        end
    end

    -- ===== cave2: fallback-выбор записи =====
    local cave2 = cave + 0x290
    local cave2_code = copy_bytes({
        0x51, 0x52, 0x8B, 0xC3, 0x8B, 0x55, 0x08, 0x4A, 0x21, 0xD0, 0x8B, 0x55,
        0x04, 0x8B, 0x14, 0x82, 0x52, 0xA1, 0x00, 0x00, 0x00, 0x00, 0xF3, 0x0F,
        0x10, 0x04, 0x85, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x57, 0xC9, 0x0F, 0x2E,
        0xC1, 0x0F, 0x87, 0x9E, 0x00, 0x00, 0x00, 0x90, 0x90, 0x90, 0x90, 0x90,
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
        0x90, 0x90, 0x90, 0x8B, 0x0C, 0x24, 0xF3, 0x0F, 0x10, 0x01, 0xF3, 0x0F,
        0x5C, 0x07, 0xF3, 0x0F, 0x59, 0xC0, 0xF3, 0x0F, 0x10, 0x49, 0x04, 0xF3,
        0x0F, 0x5C, 0x4F, 0x04, 0xF3, 0x0F, 0x59, 0xC9, 0xF3, 0x0F, 0x58, 0xC1,
        0xF3, 0x0F, 0x10, 0x49, 0x08, 0xF3, 0x0F, 0x5C, 0x4F, 0x08, 0xF3, 0x0F,
        0x59, 0xC9, 0xF3, 0x0F, 0x58, 0xC1, 0xF3, 0x0F, 0x51, 0xC0, 0x0F, 0x2E,
        0x05, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x83, 0x46, 0x00, 0x00, 0x00, 0xF3,
        0x0F, 0x10, 0x41, 0x68, 0xF3, 0x0F, 0x5C, 0x47, 0x68, 0x0F, 0x2E, 0x05,
        0x00, 0x00, 0x00, 0x00, 0x76, 0x08, 0xF3, 0x0F, 0x5C, 0x05, 0x00, 0x00,
        0x00, 0x00, 0xF3, 0x0F, 0x10, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x2E,
        0xC8, 0x76, 0x08, 0xF3, 0x0F, 0x58, 0x05, 0x00, 0x00, 0x00, 0x00, 0x0F,
        0x57, 0xC9, 0xF3, 0x0F, 0x5C, 0xC8, 0xF3, 0x0F, 0x5F, 0xC1, 0x0F, 0x2E,
        0x05, 0x00, 0x00, 0x00, 0x00, 0x77, 0x02, 0xEB, 0x2F, 0x8B, 0xCD, 0xB8,
        0x00, 0x00, 0x00, 0x00, 0xFF, 0xD0, 0x84, 0xC0, 0x74, 0x17, 0x8B, 0xCF,
        0xB8, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xD0, 0x84, 0xC0, 0x75, 0x0A, 0x8B,
        0xF7, 0x83, 0xC4, 0x0C, 0xE9, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x34, 0x24,
        0x83, 0xC4, 0x0C, 0xE9, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC4, 0x0C, 0x8B,
        0x0D, 0x00, 0x00, 0x00, 0x00, 0xE9, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x41, 0x00, 0x00, 0x34, 0x43, 0x00, 0x00, 0x34, 0xC3, 0x00, 0x00,
        0xB4, 0x43,
    })
    local g_pdc = sites.fb ~= nil and (vread_u32(sites.fb + 2) or 0) or 0
    local function fix2(offset, value)
        patch_le32(cave2_code, offset, value)
    end
    fix2(0x12,  g_entindex)
    fix2(0x1B,  g_is_firing)
    fix2(0x79,  cave2 + 0x106)
    fix2(0x90,  cave2 + 0x10A)
    fix2(0x9A,  cave2 + 0x112)
    fix2(0xA2,  cave2 + 0x10E)
    fix2(0xAF,  cave2 + 0x112)
    fix2(0xC1,  cave2 + 0x106)
    fix2(0xCC,  sites.fn_valid)
    fix2(0xD9,  sites.fn_expired)
    fix2(0xE9,  sites.addr_ret - (cave2 + 0xED))
    fix2(0xF4,  sites.addr_ret - (cave2 + 0xF8))
    fix2(0xFD,  g_pdc)
    fix2(0x102, sites.fb ~= nil and ((sites.fb + 6) - (cave2 + 0x106)) or 0)

    if not raw_write(cave2, cave2_code) then
        rs_free_cave()
        return false, "не удалось записать cave2"
    end

    -- ===== главный патч: 39 байт сайта =====
    local main = { 0xE9 }
    le32_into(main, cave - (site + 5))
    for _ = 5, 38 do main[#main + 1] = 0x90 end
    local main_saved = snapshot(site, 39)
    if not raw_write(site, main) then
        rs_free_cave()
        return false, "не удалось переписать select_lagrecord"
    end
    rs.saved.rs = main_saved
    vflush(cave, 0x400)
    vflush(site, 39)

    -- ===== cave3: animfix thirdperson_recoil =====
    rs.thirdperson_recoil = nil
    if utils ~= nil and utils.get_netvar_offset ~= nil then
        local ok, offset = pcall(utils.get_netvar_offset, "DT_CSPlayer", "m_flThirdpersonRecoil")
        if ok and type(offset) == "number" and offset > 0 then
            rs.thirdperson_recoil = offset
        end
    end

    if sites.af ~= nil and rs.thirdperson_recoil ~= nil and vread_u8(sites.af) ~= 0xE9 then
        local cave3 = cave + 0x16A
        local af_site = sites.af
        local af_ret = af_site + 16
        local c3 = copy_bytes({
        0xF3, 0x0F, 0x10, 0x92, 0x70, 0x03, 0x00, 0x00, 0xF3, 0x0F, 0x11, 0x90,
        0x80, 0x01, 0x00, 0x00, 0x51, 0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00, 0xF3,
        0x0F, 0x10, 0x14, 0x8D, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x57, 0xDB, 0x0F,
        0x2E, 0xD3, 0x76, 0x1B, 0xF3, 0x0F, 0x10, 0x14, 0x8D, 0x00, 0x00, 0x00,
        0x00, 0x8B, 0x8E, 0xE0, 0x02, 0x00, 0x00, 0x85, 0xC9, 0x74, 0x08, 0xF3,
        0x0F, 0x11, 0x91, 0x00, 0x00, 0x00, 0x00, 0x59, 0xE9, 0x00, 0x00, 0x00,
        0x00,
        })
        local function fix3(offset, value)
            patch_le32(c3, offset, value)
        end
        fix3(0x13, g_entindex)
        fix3(0x1C, g_is_firing)
        fix3(0x2D, g_tpr_arr)
        fix3(0x3F, rs.thirdperson_recoil)
        fix3(0x45, af_ret - (cave3 + 0x49))

        if raw_write(cave3, c3) then
            local patched = { 0xE9 }
            le32_into(patched, cave3 - (af_site + 5))
            for _ = 5, 15 do patched[#patched + 1] = 0x90 end
            local saved = snapshot(af_site, 16)
            if raw_write(af_site, patched) then
                rs.af_saved, rs.af_applied = saved, true
                vflush(af_site, 16)
            end
        end
    end

    rs.is_firing = g_is_firing
    rs.tpr_arr   = g_tpr_arr
    rs.applied = true
    log("Custom record selection: пещеры @0x%08X, сайт 0x%08X, entindex 0x%08X, animfix 0x%08X",
        cave, site, sites.rs_eix or 0, rs.af_applied and sites.af or 0)
    return true
end

local function rs_fb_install()
    if rs.fb_applied then return true end
    if not rs.applied then return false, "сначала нужен главный патч выбора записи" end
    local site = rs.sites.fb
    if site == nil then return false, "шаблон fb не найден" end
    if vread_u8(site) == 0xE9 then return false, "fb уже пропатчен (не нами)" end
    if not executable_bytes(site, 6) then return false, "fb сайт не исполняемый" end

    local cave2 = rs.cave + 0x290
    local patched = { 0xE9 }
    le32_into(patched, cave2 - (site + 5))
    patched[#patched + 1] = 0x90

    local saved = snapshot(site, 6)
    if not raw_write(site, patched) then return false, "не удалось переписать fb" end
    vflush(site, 6)
    rs.fb_saved, rs.fb_applied = saved, true
    log("Custom fallback selection: fb 0x%08X -> cave2 0x%08X", site, cave2)
    return true
end

local function rs_fb_restore()
    if not rs.fb_applied then return true end
    local ok = true
    if rs.fb_saved ~= nil then ok = raw_write(rs.sites.fb, rs.fb_saved) == true end
    vflush(rs.sites.fb, 6)
    rs.fb_saved, rs.fb_applied = nil, false
    return ok
end

local function rs_restore()
    if not rs.applied then return true end
    rs_fb_restore()

    if rs.af_applied and rs.af_saved ~= nil then
        raw_write(rs.sites.af, rs.af_saved)
        vflush(rs.sites.af, 16)
        rs.af_saved, rs.af_applied = nil, false
    end

    if rs.eix_applied and rs.eix_saved ~= nil then
        raw_write(rs.sites.rs_eix, rs.eix_saved)
        vflush(rs.sites.rs_eix, 5)
        rs.eix_saved, rs.eix_applied = nil, false
    end

    if rs.saved.rs ~= nil then
        raw_write(rs.sites.rs, rs.saved.rs)
        vflush(rs.sites.rs, 39)
        rs.saved.rs = nil
    end

    rs.is_firing, rs.tpr_arr = nil, nil
    rs.applied = false
    rs_free_cave()
    log("Custom record selection: снят, пещеры освобождены")
    return true
end

--[[--------------------------- меню -----------------------------------------]]

local viera2_group = viera_group or ui.create("Rage", "NL Backtrack Fixes")

viera2_switch = {
    extrap = viera2_group:switch("Extrapolation primary", false),
    rs     = viera2_group:switch("Custom record selection", false),
    fb     = viera2_group:switch("Custom fallback selection", false),
}

viera2_switch.extrap:tooltip("В вызов should_extrapolate ставится стаб: breaklc == 0 или hittable != 0 — " ..
                             "настоящая функция, иначе сразу return 1. В оригинале стаб нигде не включался.")
viera2_switch.rs:tooltip("Подмена select_lagrecord своей пещерой: приоритет записей по выстрелу, дистанции и углу.")
viera2_switch.fb:tooltip("Fallback-выбор записи через cave2. Требует включённого Custom record selection.")

local function viera2_update()
    if ui.get(viera2_switch.extrap) == true then
        local ok, why = extrap_install()
        if not ok then
            warn("Extrapolation primary: НЕ включён — %s", tostring(why))
            ui.set(viera2_switch.extrap, false)
        end
    elseif extrap_state.applied then
        extrap_restore()
    end

    if ui.get(viera2_switch.rs) == true then
        local ok, why = rs_install()
        if not ok then
            warn("Custom record selection: НЕ включён — %s", tostring(why))
            ui.set(viera2_switch.rs, false)
            ui.set(viera2_switch.fb, false)
        end
    elseif rs.applied then
        rs_restore()
        ui.set(viera2_switch.fb, false)
    end

    if ui.get(viera2_switch.fb) == true then
        local ok, why = rs_fb_install()
        if not ok then
            warn("Custom fallback selection: НЕ включён — %s", tostring(why))
            ui.set(viera2_switch.fb, false)
        end
    elseif rs.fb_applied then
        rs_fb_restore()
    end
end

function viera2_restore_all()
    extrap_restore()
    rs_restore()
end

function viera2_poll()
    if not (ui.get(viera2_switch.extrap) == true or ui.get(viera2_switch.rs) == true
            or ui.get(viera2_switch.fb) == true) then
        return
    end
    if viera_module_base() == nil then return end
    viera2_update()
end

for _, element in ipairs({ viera2_switch.extrap, viera2_switch.rs, viera2_switch.fb }) do
    element:set_callback(function()
        if viera_module_base() == nil then
            warn("%s: база модуля ещё не найдена — попробую позже", "viera")
        end
        viera2_update()
    end, true)
end

if events ~= nil and events.createmove ~= nil then
    events.createmove:set(function() rs_fill_tables() end)
end

--[[----------------------------------------------------------------------------
    9.7 Патчи persistence: model hook (studiorender.dll)

    В Luas/nl/persistence.lua единственный патч памяти — 5-байтовый inline-хук
    на функции отрисовки модели в studiorender.dll. Он состоит из трёх частей,
    и все три перенесены здесь байт в байт:

      * пещера 1 (192 байта) — код хука: проверяет флаг активности, thread id
        (fs:[0x24]) и указатель модели, при совпадении подменяет цвет модели
        ([ecx+0x240..0x24C]), сохраняя оригинал, и возвращается на entry+5;
      * пещера 2 (50 байт, лежит в пещере по +0xC0) — вызывается из пещеры 1:
        копирует матрицы (rep movsd) в буфер из управляющего блока и считает
        копирования;
      * сам хук: entry (studiorender.dll + RVA 0x536A6), 5 байт
        `83 EC 54 53 56` -> `E9 <пещера>`.

    Управляющий блок (смещения — как в оригинале, от пещеры + 256):
        +0  активность        +4  thread id
        +8  указатель модели  +12 указатель вызова
        +16 «не подменять цвет»
        +20 сохранённый указатель
        +24..+36 свой цвет (4 dword)   +40..+52 сохранённый цвет
        +56 буфер костей      +60 размер     +64 счётчик копирований

    ВАЖНО, и это не отговорка: пещера срабатывает, только если управляющий
    блок заполнен ТЕМ ЖЕ thread id и ТЕМ ЖЕ указателем модели, с которыми
    persistence сам вызывал отрисовку. Его draw pass — это движок отрисовки
    на 176k строк, он не переносится. Поэтому:
      * по умолчанию хук ВЫКЛЮЧЕН и ничего не пишет;
      * при включении он ставится байт в байт, но сам по себе ничего не рисует:
        блок надо заполнять самому через PersistenceModel:activate(...),
        как это делал оригинал.
    Ничего не выдумано: нет условий — хук не делает ничего.
------------------------------------------------------------------------------]]

local PERSISTENCE_RVA_DEFAULT = 0x536A6
local PERSISTENCE_PROLOGUE = { 0x83, 0xEC, 0x54, 0x53, 0x56 }
local PERSISTENCE_MODULE = "studiorender.dll"
local PERSISTENCE_CAVE_SIZE = 512

local persistence_model = {
    applied = false,
    cave = nil,
    cave_block = nil,
    entry = nil,
    saved = nil,
    rva = PERSISTENCE_RVA_DEFAULT,
    failed = false,
    state = nil,
    color = nil,
    saved_color = nil,
}

local function write_u32(addr, value)
    local bytes = {}
    le32_into(bytes, value)
    return raw_write(addr, bytes)
end

local function persistence_target()
    local handle = C.GetModuleHandleA(PERSISTENCE_MODULE)
    if handle == nil then return nil, "модуль " .. PERSISTENCE_MODULE .. " не загружен" end
    local base = tonumber(ffi.cast("uintptr_t", handle))
    if base == 0 then return nil, "GetModuleHandleA вернул 0" end

    local pe_offset = vread_u32(base + 0x3C)
    if pe_offset == nil then return nil, "не удалось прочитать e_lfanew" end
    if pe_offset < 64 or pe_offset > 4096 then
        return nil, ("e_lfanew = 0x%X вне 64..4096 — это не PE-образ"):format(pe_offset)
    end

    local header = vread_u32(base + pe_offset)
    if header ~= 0x4550 then
        return nil, ("нет подписи PE (0x%08X)"):format(header or 0)
    end

    local entry = base + persistence_model.rva
    if not executable_bytes(entry, 5) then
        return nil, ("entry 0x%08X не исполняемый"):format(entry)
    end

    for index = 1, #PERSISTENCE_PROLOGUE do
        if vread_u8(entry + index - 1) ~= PERSISTENCE_PROLOGUE[index] then
            return nil, ("пролог не совпал (%s): RVA 0x%X не от этой сборки"):format(
                hexdump(entry, 5), persistence_model.rva)
        end
    end

    return entry
end

-- Тот же порядок, что в оригинале: код пещеры -> абсолютные адреса
-- управляющего блока -> пещера 2 -> jmp назад на entry+5.
local function persistence_build_caves(cave, entry)
    local code = copy_bytes({
        0x9C, 0x50, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x85, 0xC0, 0x0F, 0x84, 0xA5,
        0x00, 0x00, 0x00, 0x64, 0xA1, 0x24, 0x00, 0x00, 0x00, 0x3B, 0x05, 0x00,
        0x00, 0x00, 0x00, 0x0F, 0x85, 0x93, 0x00, 0x00, 0x00, 0x8B, 0x45, 0x0C,
        0x85, 0xC0, 0x0F, 0x84, 0x88, 0x00, 0x00, 0x00, 0x8B, 0x40, 0x18, 0x3B,
        0x05, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x85, 0x79, 0x00, 0x00, 0x00, 0xA1,
        0x00, 0x00, 0x00, 0x00, 0x89, 0x45, 0x10, 0x83, 0x3D, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x0F, 0x85, 0x32, 0x00, 0x00, 0x00, 0x89, 0x0D, 0x00, 0x00,
        0x00, 0x00, 0x8B, 0x81, 0x40, 0x02, 0x00, 0x00, 0xA3, 0x00, 0x00, 0x00,
        0x00, 0x8B, 0x81, 0x44, 0x02, 0x00, 0x00, 0xA3, 0x00, 0x00, 0x00, 0x00,
        0x8B, 0x81, 0x48, 0x02, 0x00, 0x00, 0xA3, 0x00, 0x00, 0x00, 0x00, 0x8B,
        0x81, 0x4C, 0x02, 0x00, 0x00, 0xA3, 0x00, 0x00, 0x00, 0x00, 0xA1, 0x00,
        0x00, 0x00, 0x00, 0x89, 0x81, 0x40, 0x02, 0x00, 0x00, 0xA1, 0x00, 0x00,
        0x00, 0x00, 0x89, 0x81, 0x44, 0x02, 0x00, 0x00, 0xA1, 0x00, 0x00, 0x00,
        0x00, 0x89, 0x81, 0x48, 0x02, 0x00, 0x00, 0xA1, 0x00, 0x00, 0x00, 0x00,
        0x89, 0x81, 0x4C, 0x02, 0x00, 0x00, 0xFF, 0x05, 0x00, 0x00, 0x00, 0x00,
        0x58, 0x9D, 0x83, 0xEC, 0x54, 0x53, 0x56, 0xE9, 0x00, 0x00, 0x00, 0x00,
    })

    local state = cave + 256
    patch_le32(code, 0x03, state)          -- флаг активности
    patch_le32(code, 0x17, state + 4)      -- thread id
    patch_le32(code, 0x31, state + 8)      -- указатель модели
    patch_le32(code, 0x45, state + 16)     -- «не подменять цвет»
    patch_le32(code, 0x52, state + 20)     -- сохранённый указатель
    patch_le32(code, 0x5D, state + 40)     -- сохранённый цвет 0..3
    patch_le32(code, 0x68, state + 44)
    patch_le32(code, 0x73, state + 48)
    patch_le32(code, 0x7E, state + 52)
    patch_le32(code, 0x83, state + 24)     -- свой цвет 0..3
    patch_le32(code, 0x8E, state + 28)
    patch_le32(code, 0x99, state + 32)
    patch_le32(code, 0xA4, state + 36)
    patch_le32(code, 0xB0, state + 16)     -- счётчик «сколько моделей тронули»

    -- +59: call в пещеру 2 (+192); +74: jne на +174, когда цвет трогать нельзя
    code[0x3B + 1] = 0xE8
    patch_le32(code, 0x3C, 0x80)           -- 59 + 5 + 128 = 192
    patch_le32(code, 0x4C, 94)             -- 80 + 94 = 174
    patch_le32(code, 0xBC, (entry + 5) - (cave + 192))

    local blob2 = copy_bytes({
        0x51, 0x56, 0x57, 0x8B, 0x75, 0x10, 0x85, 0xF6, 0x74, 0x18, 0x8B, 0x3D,
        0x00, 0x00, 0x00, 0x00, 0x85, 0xFF, 0x74, 0x0E, 0x8B, 0x0D, 0x00, 0x00,
        0x00, 0x00, 0xF3, 0xA5, 0xFF, 0x05, 0x00, 0x00, 0x00, 0x00, 0x5F, 0x5E,
        0x59, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x85, 0xC0, 0x75, 0x03, 0x8B, 0x45,
        0x10, 0xC3,
    })
    patch_le32(blob2, 12, state + 56)      -- буфер костей
    patch_le32(blob2, 22, state + 60)      -- размер
    patch_le32(blob2, 30, state + 64)      -- счётчик копирований
    patch_le32(blob2, 38, state + 12)      -- указатель вызова

    return code, blob2
end

local function persistence_install()
    if persistence_model.applied then return true end

    local entry, why = persistence_target()
    if entry == nil then return false, why end

    local block = C.VirtualAlloc(nil, PERSISTENCE_CAVE_SIZE, MEM_COMMIT_RESERVE, PAGE_RWX)
    if block == nil then return false, "VirtualAlloc под пещеру не сработал" end
    local cave = tonumber(ffi.cast("uintptr_t", block))

    local code, blob2 = persistence_build_caves(cave, entry)
    if not raw_write(cave, code) then
        C.VirtualFree(block, 0, MEM_RELEASE)
        return false, "не удалось записать пещеру"
    end
    if not raw_write(cave + 192, blob2) then
        C.VirtualFree(block, 0, MEM_RELEASE)
        return false, "не удалось записать пещеру 2"
    end
    vflush(cave, 256)

    local saved = snapshot(entry, 5)
    local patch = { 0xE9 }
    le32_into(patch, cave - (entry + 5))
    if not raw_write(entry, patch) then
        C.VirtualFree(block, 0, MEM_RELEASE)
        return false, "не удалось переписать entry"
    end
    vflush(entry, 5)

    persistence_model.applied = true
    persistence_model.cave, persistence_model.cave_block = cave, block
    persistence_model.entry, persistence_model.saved = entry, saved
    persistence_model.state = cave + 256
    persistence_model.color = cave + 280
    persistence_model.saved_color = cave + 296
    log("persistence model hook: entry 0x%08X -> пещера 0x%08X (state 0x%08X)",
        entry, cave, persistence_model.state)
    return true
end

local function persistence_restore()
    if not persistence_model.applied then return true end
    local ok = true
    if persistence_model.saved ~= nil then
        ok = raw_write(persistence_model.entry, persistence_model.saved) == true
        vflush(persistence_model.entry, 5)
    end
    if persistence_model.cave_block ~= nil then
        C.VirtualFree(persistence_model.cave_block, 0, MEM_RELEASE)
    end
    persistence_model.applied = false
    persistence_model.cave, persistence_model.cave_block = nil, nil
    persistence_model.entry, persistence_model.saved = nil, nil
    persistence_model.state, persistence_model.color, persistence_model.saved_color = nil, nil, nil
    log("persistence model hook: снят, entry возвращён")
    return ok
end

--[[--------------------------- управляющий блок ----------------------------]]

local PersistenceModel = {}

function PersistenceModel:available()
    return persistence_model.applied
end

function PersistenceModel:activate(model_pointer, call_pointer)
    if not persistence_model.applied then return false, "хук не установлен" end
    local thread = 0
    if C.GetCurrentThreadId ~= nil then
        local ok, value = pcall(C.GetCurrentThreadId)
        if ok then thread = tonumber(value) or 0 end
    end
    write_u32(persistence_model.state + 4, thread)
    write_u32(persistence_model.state + 8, model_pointer or 0)
    write_u32(persistence_model.state + 12, call_pointer or 0)
    write_u32(persistence_model.state + 56, 0)
    write_u32(persistence_model.state + 60, 0)
    write_u32(persistence_model.state + 64, 0)
    write_u32(persistence_model.state, 1)
    return true
end

function PersistenceModel:deactivate()
    if not persistence_model.applied then return end
    write_u32(persistence_model.state, 0)
end

function PersistenceModel:set_color(r, g, b, a)
    if not persistence_model.applied then return false, "хук не установлен" end
    local color = persistence_model.color
    vwrite_f32(color + 0, r or 1)
    vwrite_f32(color + 4, g or 1)
    vwrite_f32(color + 8, b or 1)
    vwrite_f32(color + 12, a or 1)
    return true
end

function PersistenceModel:skip_color(skip)
    if not persistence_model.applied then return end
    write_u32(persistence_model.state + 16, skip and 1 or 0)
end

function PersistenceModel:set_capture(buffer, size)
    if not persistence_model.applied then return false, "хук не установлен" end
    write_u32(persistence_model.state + 56, buffer or 0)
    write_u32(persistence_model.state + 60, size or 0)
    write_u32(persistence_model.state + 64, 0)
    return true
end

function PersistenceModel:captured()
    if not persistence_model.applied then return 0 end
    return vread_u32(persistence_model.state + 64) or 0
end

--[[--------------------------- меню -----------------------------------------]]

local persistence_switch = viera_group:switch("Model hook (persistence)", false)
persistence_switch:tooltip("Ставит тот же 5-байтовый хук на studiorender.dll, что в persistence, " ..
                           "с теми же пещерами и релокациями. Сам по себе он НИЧЕГО не рисует: " ..
                           "оригинал заполнял управляющий блок из своего draw pass. Включайте, " ..
                           "только если дёргаете PersistenceModel:activate(...) сами.")

local persistence_rva = viera_group:input("studiorender RVA (hex)", "0x536A6")
persistence_rva:tooltip("RVA функции отрисовки модели. Перед установкой проверяется пролог " ..
                        "(83 EC 54 53 56): не совпал — хук не ставится.")

local function persistence_update()
    if ui.get(persistence_switch) == true then
        local ok, why = persistence_install()
        if not ok then
            persistence_model.failed = true
            warn("persistence model hook: НЕ включён — %s", tostring(why))
            ui.set(persistence_switch, false)
        end
    elseif persistence_model.applied then
        persistence_restore()
    end
end

function persistence_poll()
    if ui.get(persistence_switch) ~= true then return end
    if persistence_model.applied or persistence_model.failed then return end
    persistence_update()
end

persistence_rva:set_callback(function(element)
    local text = element:get()
    local parsed = tonumber(text)
    if parsed == nil and type(text) == "string" then
        parsed = tonumber(text:match("^0[xX](%x+)$") or "", 16)
    end
    if parsed == nil or parsed < 0x1000 or parsed > 0x2000000 then
        warn("studiorender RVA: \"%s\" не похоже на RVA, остаётся 0x%X",
             tostring(text), persistence_model.rva)
        return
    end
    persistence_model.rva = parsed
    persistence_model.failed = false
    if persistence_model.applied then
        persistence_restore()
        if ui.get(persistence_switch) == true then persistence_update() end
    end
end, true)

persistence_switch:set_callback(function()
    if ui.get(persistence_switch) == true then persistence_model.failed = false end
    persistence_update()
end, true)

function persistence_restore_all()
    persistence_restore()
end

-- Управляющий блок нужен снаружи: без него пещера ничего не рисует, и
-- заполнять его должен тот, кто вызывает отрисовку (в оригинале — его draw pass).
NL_PERSISTENCE_MODEL = PersistenceModel

--[[----------------------------------------------------------------------------
    9.8 Патчи persistence: rage native fixes (neverlose.dll)

    В Luas/nl/persistence.lua это таблица `patches` из шести записей: каждая —
    шаблон с джокерами и пара «оригинал → замена». Сканируется neverlose.dll,
    то есть патчится собственная память чита, а не модуль игры. Здесь перенесены
    пять записей с фиксированной заменой (у шестой, continuity-гейта, замена
    строится пещерой в рантайме — она в разделе 9.10):

      * record builder expiry      — F3 0F 5B C0 0F 5B C0 (cvtss2sd, два раза)
                                     → 7x90: убирает пересчёт срока записи;
      * record iterator expiry     — те же 7 байт перед F3 0F 10 4E 3C;
      * layer scale 0.75 / 1.25    — F3 0F 59 0D <disp32> (mulss xmm1,[..])
                                     → 8x90: множитель слоя анимации не читается;
      * layer forced weight 1.0    — F3 0F 10 0D <disp32> (movss xmm1,[..])
                                     → 8x90: вес слоя не читается (остаётся 1.0).

    Оригинал берёт ПЕРВОЕ совпадение и не проверяет, что байты по адресу —
    именно ожидаемые. Здесь перед записью сверяются `original`, после записи
    байты читаются обратно, при расхождении возвращается снапшот. Набор
    применяется целиком или никак: один неподошедший шаблон откатывает уже
    записанные. У F3 0F 59 0D / F3 0F 10 0D сверяются только байты опкода:
    disp32 — абсолютный адрес константы в neverlose.dll, он свой на каждой
    сборке, поэтому от него зависит только поиск.
------------------------------------------------------------------------------]]

local RAGE_MODULE = "neverlose.dll"

local function rage_pattern(text)
    local out = {}
    for token in text:gmatch("%S+") do
        if token == "?" or token == "??" then
            out[#out + 1] = PAT_WILD
        else
            out[#out + 1] = tonumber(token, 16) or PAT_WILD
        end
    end
    return out
end

local function rage_bytes(text)
    local out = {}
    for byte in text:gmatch("%x%x") do out[#out + 1] = tonumber(byte, 16) end
    return out
end

local function rage_nops(count)
    local out = {}
    for index = 1, count do out[index] = 0x90 end
    return out
end

-- Те же шесть записей, что `patches[1..6]` в persistence, в исходном порядке.
local rage_patches = {
    {
        name = "record builder expiry",
        pattern = "F3 0F 5B C0 0F 5B C0 69 C6 40 09 01 00",
        original = rage_bytes("F3 0F 5B C0 0F 5B C0"),
        replacement = rage_nops(7),
    },
    {
        name = "record iterator expiry",
        pattern = "F3 0F 5B C0 0F 5B C0 F3 0F 10 4E 3C",
        original = rage_bytes("F3 0F 5B C0 0F 5B C0"),
        replacement = rage_nops(7),
    },
    {
        name = "layer scale 0.75",
        pattern = "F3 0F 59 0D ? ? ? ? E9 95 01 00 00",
        original = rage_bytes("F3 0F 59 0D"),
        replacement = rage_nops(8),
    },
    {
        name = "layer scale 1.25",
        pattern = "F3 0F 59 0D ? ? ? ? E9 D0 00 00 00",
        original = rage_bytes("F3 0F 59 0D"),
        replacement = rage_nops(8),
    },
    {
        name = "layer forced weight 1.0",
        pattern = "F3 0F 10 0D ? ? ? ? E9 C3 00 00 00",
        original = rage_bytes("F3 0F 10 0D"),
        replacement = rage_nops(8),
    },
}

for _, patch in ipairs(rage_patches) do
    patch.parsed = rage_pattern(patch.pattern)
    patch.length = #patch.replacement
    patch.address, patch.applied, patch.error = nil, false, nil
end

local rage_state = { base = nil, applied = false, failed = false }

local function rage_base()
    if rage_state.base ~= nil then return rage_state.base end

    local handle = C.GetModuleHandleA(RAGE_MODULE)
    if handle == nil then return nil end
    local base = tonumber(ffi.cast("uintptr_t", handle))
    if base == nil or base == 0 then return nil end

    if vread_u32(base) ~= 0x00905A4D then return nil end            -- 'MZ'
    local pe_offset = vread_u32(base + 0x3C)
    if pe_offset == nil or pe_offset < 64 or pe_offset > 4096 then return nil end
    if vread_u32(base + pe_offset) ~= 0x4550 then return nil end    -- 'PE'

    rage_state.base = base
    return base
end

local function rage_locate(patch)
    local base = rage_base()
    if base == nil then return nil, "модуль " .. RAGE_MODULE .. " не загружен или это не PE-образ" end

    local buffer = module_bytes(base)
    if buffer == nil then return nil, "образ " .. RAGE_MODULE .. " не читается" end

    local index, count = find_in_buffer(buffer, patch.parsed)
    if index == nil then return nil, "шаблон не найден" end
    if count > 1 then return nil, ("шаблон встречается минимум %d раз — неоднозначно"):format(count) end

    local address = module_address(base, index)
    if not executable_bytes(address, patch.length) then
        return nil, ("адрес 0x%08X не исполняемый"):format(address)
    end
    return address
end

local function rage_apply(patch)
    if patch.applied then return true end

    local address, why = rage_locate(patch)
    if address == nil then
        patch.address, patch.error = nil, why
        return false, why
    end
    patch.address = address

    local current = {}
    for index = 1, patch.length do
        local byte = vread_u8(address + index - 1)
        if byte == nil then
            patch.error = "не читаются байты по адресу"
            return false, patch.error
        end
        current[index] = byte
    end

    local already = true
    local fresh = true
    for index = 1, patch.length do
        if current[index] ~= patch.replacement[index] then already = false end
        if index <= #patch.original and current[index] ~= patch.original[index] then fresh = false end
    end
    if already then
        patch.applied, patch.error = true, nil
        return true
    end
    if not fresh then
        patch.error = ("байты по адресу не совпали (%s)"):format(hexdump(address, patch.length))
        return false, patch.error
    end

    local saved = snapshot(address, patch.length)
    if not raw_write(address, patch.replacement) then
        patch.error = "не удалось записать"
        return false, patch.error
    end
    vflush(address, patch.length)

    for index = 1, patch.length do
        if vread_u8(address + index - 1) ~= patch.replacement[index] then
            raw_write(address, saved)
            vflush(address, patch.length)
            patch.error = "обратное чтение не совпало — запись откачена"
            return false, patch.error
        end
    end

    patch.saved, patch.applied, patch.error = saved, true, nil
    return true
end

local function rage_restore(patch)
    if not patch.applied then return true end
    local ok = true
    if patch.saved ~= nil and patch.address ~= nil then
        ok = raw_write(patch.address, patch.saved) == true
        vflush(patch.address, patch.length)
        patch.saved = nil
    end
    patch.applied = false
    return ok
end

local function rage_restore_all()
    local ok = true
    for _, patch in ipairs(rage_patches) do
        if not rage_restore(patch) then ok = false end
    end
    rage_state.applied = false
    return ok
end

--[[--------------------------- меню -----------------------------------------]]

local rage_group = ui.create("Rage", "NL Rage Fixes")

local rage_switch = rage_group:switch("Rage native fixes (neverlose.dll)", false)
rage_switch:tooltip("Пять байтовых патчей из Luas/nl/persistence.lua (rage native fixes): " ..
                    "срок жизни записей бэктрака, множители слоёв анимации 0.75/1.25 и " ..
                    "принудительный вес слоя 1.0. Патчится neverlose.dll, шаблоны с джокерами, " ..
                    "каждый сайт сверяется с ожидаемыми байтами до и после записи.")

local rage_read = rage_group:button("Read rage sites", function()
    local base = rage_base()
    if base == nil then
        warn("rage fixes: %s не загружен (или это не PE-образ)", RAGE_MODULE)
        return
    end
    log("rage fixes: база %s 0x%08X", RAGE_MODULE, base)
    for index, patch in ipairs(rage_patches) do
        local address, why = rage_locate(patch)
        if address == nil then
            log("  %d. %-25s — %s", index, patch.name, tostring(why))
        else
            log("  %d. %-25s — 0x%08X, байты: %s", index, patch.name, address,
                hexdump(address, patch.length))
        end
    end
end)

local function rage_update()
    if ui.get(rage_switch) == true then
        if rage_state.applied then return true end
        local applied = {}
        for _, patch in ipairs(rage_patches) do
            local ok, why = rage_apply(patch)
            if not ok then
                for _, done in ipairs(applied) do rage_restore(done) end
                rage_state.applied, rage_state.failed = false, true
                warn("rage fixes: НЕ включены — %s (%s)", patch.name, tostring(why))
                ui.set(rage_switch, false)
                return false
            end
            applied[#applied + 1] = patch
        end
        rage_state.applied = true
        log("rage fixes: включены, сайтов %d", #applied)
        return true
    end
    if rage_state.applied then
        rage_restore_all()
        log("rage fixes: сняты, байты возвращены")
    end
    return true
end

local function rage_poll()
    if ui.get(rage_switch) ~= true then return end
    if rage_state.applied or rage_state.failed then return end
    rage_update()
end

rage_switch:set_callback(function()
    if ui.get(rage_switch) == true then rage_state.failed = false end
    rage_update()
end, true)

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
    if viera2_poll ~= nil then viera2_poll() end
    if persistence_poll ~= nil then persistence_poll() end
    if rage_poll ~= nil then rage_poll() end

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
