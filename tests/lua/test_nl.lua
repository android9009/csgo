--[[============================================================================
    tests/lua/test_nl.lua — прогон lua-patches/nl_all_patches.lua против
    фейкового Neverlose (mock/harness.lua).

    Проверяется: поиск базы модуля (в том числе по ДРУГОМУ адресу, чтобы
    доказать отсутствие хардкода), arm всех четырёх сайтов, движение слайдера,
    sv_maxunlag (поднятие и возврат), aggressive-набор, паник-хоткей и shutdown.
============================================================================]]

local M = {}

local function new_report() return { passed = 0, failures = {} } end

local function check(report, condition, text)
    if condition then
        report.passed = report.passed + 1
    else
        report.failures[#report.failures + 1] = text
    end
end

local IDA_BASE = 0x41000000

local SYM = {
    lagrecord_check_lagcomp_window = { va = 0x41400bf0, size = 484, ret_at_end = false },
    player_records_cleanup         = { va = 0x414441c0, size = 919  },
    should_extrapolate             = { va = 0x413f6db0, size = 33   },
    backtrack_valid_while_fakeduck = { va = 0x414961e0, size = 555  },
    player_records_update          = { va = 0x4144b12e, size = 5750 },
    lagrecord_is_expired           = { va = 0x413f6d50, size = 65   },
    select_lagrecord               = { va = 0x4148f180, size = 1700 },
    validate_backtrack_window      = { va = 0x4148ff1d, size = 613  },
    backtrack_valid_check          = { va = 0x414ab5d0, size = 297  },
    backtrack_scan                 = { va = 0x41495850, size = 1071 },
    ragebot_backtrack_apply        = { va = 0x414c0920, size = 236  },
    adjust_extrap_ticks            = { va = 0x413341e0, size = 250  },
}

local function build_module(H, base)
    H.add_region(base, 0x600000, H.const.PAGE_EXECUTE_READ, H.const.MEM_PRIVATE, false)
    for name, symbol in pairs(SYM) do
        local entry = base + (symbol.va - IDA_BASE)
        -- тело функции: nop'ы, чтобы поиск immediate был однозначным
        H.fill(entry, symbol.size, 0x90)
        if name ~= "lagrecord_check_lagcomp_window" then
            H.mem[entry + symbol.size - 1] = 0xC3      -- терминальный ret функции
        end
   	end
    -- окно: единственный push imm8 со значением стока
    local window_entry = base + (SYM.lagrecord_check_lagcomp_window.va - IDA_BASE)
    H.write_bytes(window_entry + 0x40, { 0x6A, 0x08 })
    return window_entry
end

local function load_script(H, path, options)
    H.install_globals("nl", options or {})
    local chunk = assert(loadfile(path))
    chunk()
end

-- 1. Полный цикл на «обычной» базе
function M.run_cycle(H, path)
    local report = new_report()
    H.reset()
    local base = 0x2C000000
    build_module(H, base)
    load_script(H, path, { sv_maxunlag = 0.2 })

    local window_entry = base + (SYM.lagrecord_check_lagcomp_window.va - IDA_BASE)
    local enabled = H.find_item("Backtrack Patch")
    check(report, enabled ~= nil, "переключатель Backtrack Patch создан")

    -- включаем: arm происходит по тику
    H.fire(enabled, true)
    H.fire_event("net_update_end")

    check(report, H.log_contains("база модуля 0x2C000000"), "база модуля найдена сканом")
    check(report, H.read_byte(window_entry + 0x41) == 14, "окно записано как 14 тиков")

    local unlag = H.cvar.sv_maxunlag:float()
    check(report, math.abs(unlag - 0.29875) < 0.0005,
          string.format("sv_maxunlag = %.5f (ожидалось 0.29875)", unlag))
    check(report, H.log_contains("включено: окно 14 тиков"), "включение залогировано")

    -- слайдер: переписать по месту
    local window = H.find_item("Window")
    H.fire(window, 10)
    check(report, H.read_byte(window_entry + 0x41) == 10, "слайдер переписал окно на 10")

    -- aggressive: три поведенческих патча
    local aggressive = H.find_item("Aggressive")
    H.fire(aggressive, true)
    local cleanup = base + (SYM.player_records_cleanup.va - IDA_BASE)
    local extrap  = base + (SYM.should_extrapolate.va - IDA_BASE)
    local fakeduck = base + (SYM.backtrack_valid_while_fakeduck.va - IDA_BASE)
    check(report, H.read_byte(cleanup) == 0xC3, "cleanup нейтрализован (ret)")
    check(report, H.read_hex(extrap, 6) == "B8 00 00 00 00 C3", "should_extrapolate -> return 0")
    check(report, H.read_hex(fakeduck, 6) == "B8 01 00 00 00 C3", "fakeduck -> return 1")

    -- выключение вернёт всё и sv_maxunlag
    H.fire(enabled, false)
    check(report, H.read_byte(cleanup) == 0x90, "cleanup восстановлен")
    check(report, H.read_byte(extrap) == 0x90, "extrapolation восстановлен")
    check(report, H.read_byte(window_entry + 0x41) == 8, "окно вернулось к стоку (8)")
    check(report, math.abs(H.cvar.sv_maxunlag:float() - 0.2) < 0.0005, "sv_maxunlag возвращён 0.2")

    return report
end

-- 2. Другая база модуля: база не захардкожена
function M.run_other_base(H, path)
    local report = new_report()
    H.reset()
    local base = 0x1A400000
    build_module(H, base)
    load_script(H, path, { sv_maxunlag = 0.35 })

    local window_entry = base + (SYM.lagrecord_check_lagcomp_window.va - IDA_BASE)
    local enabled = H.find_item("Backtrack Patch")
    H.fire(enabled, true)
    H.fire_event("net_update_end")

    check(report, H.log_contains("база модуля 0x1A400000"), "база 0x1A400000 найдена")
    check(report, H.read_byte(window_entry + 0x41) == 14, "окно записано на другой базе")

    -- sv_maxunlag уже выше нужного: не понижаем
    check(report, math.abs(H.cvar.sv_maxunlag:float() - 0.35) < 0.0005,
          "sv_maxunlag не понижается, если уже больше нужного")

    H.fire(enabled, false)
    return report
end

-- 3. Паник-хоткей и shutdown
function M.run_panic(H, path)
    local report = new_report()
    H.reset()
    local base = 0x2C000000
    build_module(H, base)
    load_script(H, path, { sv_maxunlag = 0.2 })

    local window_entry = base + (SYM.lagrecord_check_lagcomp_window.va - IDA_BASE)
    local enabled = H.find_item("Backtrack Patch")
    H.fire(enabled, true)
    H.fire_event("net_update_end")
    check(report, H.read_byte(window_entry + 0x41) == 14, "патч включён перед паникой")

    local panic = H.find_item("Panic revert")
    H.fire(panic, true)
    H.fire_event("net_update_end")
    check(report, H.read_byte(window_entry + 0x41) == 8, "паник-хоткей снял патч")
    check(report, math.abs(H.cvar.sv_maxunlag:float() - 0.2) < 0.0005, "паник вернул sv_maxunlag")
    check(report, H.log_contains("паник-хоткей"), "паника залогирована")

    -- shutdown
    H.fire(enabled, true)
    H.fire_event("net_update_end")
    H.fire_event("shutdown")
    check(report, H.read_byte(window_entry + 0x41) == 8, "shutdown снял патч")

    return report
end

-- 4. Нет базы: скрипт не пишет ничего и говорит об этом
function M.run_missing_base(H, path)
    local report = new_report()
    H.reset()
    load_script(H, path, { sv_maxunlag = 0.2 })

    local enabled = H.find_item("Backtrack Patch")
    H.fire(enabled, true)
    -- 8 попыток по 64 тика между ними, как в скрипте
    for _ = 1, 8 * 64 + 8 do H.fire_event("net_update_end") end

    check(report, math.abs(H.cvar.sv_maxunlag:float() - 0.2) < 0.0005,
          "без базы sv_maxunlag не тронут")
    check(report, H.log_contains("включить не удалось: база модуля не определена"),
          "первый отказ объяснён")
    check(report, H.log_contains("не удалось включить за 8 попыток"), "итоговый отказ объяснён")

    return report
end

-- 5. Патчи viera utils: скан по байтам, включение/выключение, стаб defensive
local VIERA = {
    preserve = {
        bytes = { 0xA9, 0x08, 0x08, 0x00, 0x00, 0x75, 0x0F, 0x43 },
        delta = 6, patched = { 0x00 }, name = "Preserve valid records (viera)",
    },
    early = {
        bytes = { 0x4F, 0x3B, 0x3D, 0x11, 0x22, 0x33, 0x44,
                  0x0F, 0x8F, 0x55, 0x66, 0x77, 0x88, 0x8B, 0x45 },
        delta = 0, patched = { 0x90 }, name = "Early defensive activation (viera)",
    },
    break_a = {
        bytes = { 0x49, 0x21, 0xD9, 0x8B, 0x0C, 0x88,
                  0xE8, 0x11, 0x22, 0x33, 0x44, 0x84, 0xC0, 0x0F, 0x84 },
        delta = 6, patched = { 0xB0, 0x00, 0x90, 0x90, 0x90 },
        name = "Skip break distance A (viera)",
    },
    break_b = {
        bytes = { 0x48, 0x21, 0xD8, 0x8B, 0x0C, 0x81,
                  0xE8, 0x11, 0x22, 0x33, 0x44, 0x84, 0xC0, 0x74 },
        delta = 6, patched = { 0xB0, 0x00, 0x90, 0x90, 0x90 },
        name = "Skip break distance B (viera)",
    },
    defensive = {
        bytes = { 0x80, 0x3D, 0x11, 0x22, 0x33, 0x44, 0x00,
                  0x0F, 0x84, 0x55, 0x66, 0x77, 0x88,
                  0x83, 0x3D, 0x21, 0x32, 0x43, 0x54, 0x00,
                  0x0F, 0x8E, 0x65, 0x76, 0x87, 0x98,
                  0xB3, 0x01 },
        delta = 9, name = "Defensive on backtrack (viera)",
    },
}

local VIERA_AT = 0x500000      -- смещение внутри образа модуля

local function plant_viera(H, base)
    for key, entry in pairs(VIERA) do
        entry.site = base + VIERA_AT + (entry.delta or 0)
        local start = base + VIERA_AT
        H.write_bytes(start, entry.bytes)
        VIERA_AT_OFFSETS = VIERA_AT_OFFSETS or {}
        VIERA[key].pattern_addr = start
        -- каждый следующий шаблон кладём на 0x100 дальше
        VIERA_AT = VIERA_AT + 0x100
    end
end

local function viera_restore_offset(key) return VIERA[key].pattern_addr end

function M.run_viera(H, path)
    local report = new_report()
    H.reset()
    VIERA_AT = 0x500000
    local base = 0x2C000000
    build_module(H, base)
    plant_viera(H, base)
    load_script(H, path, { sv_maxunlag = 0.2 })

    local read = H.find_item("Read sites")
    check(report, read ~= nil, "кнопка Read sites у viera создана")

    -- 1. Preserve valid records: 0F -> 00
    local preserve = H.find_item("Preserve valid records")
    local preserve_site = VIERA.preserve.pattern_addr + 6
    check(report, H.read_byte(preserve_site) == 0x0F, "исходный байт jne на месте")
    H.fire(preserve, true)
    check(report, H.read_byte(preserve_site) == 0x00, "Preserve valid records: 0F -> 00")
    check(report, H.log_contains("Preserve valid records (viera): включён"), "включение залогировано")
    H.fire(preserve, false)
    check(report, H.read_byte(preserve_site) == 0x0F, "Preserve valid records: байт возвращён")

    -- 2. Early defensive: dec edi -> NOP
    local early = H.find_item("Early defensive activation")
    local early_site = VIERA.early.pattern_addr
    H.fire(early, true)
    check(report, H.read_byte(early_site) == 0x90, "Early defensive: dec edi -> NOP")
    H.fire(early, false)
    check(report, H.read_byte(early_site) == 0x4F, "Early defensive: возвращён dec edi")

    -- 3. Skip break distance: два сайта, оба возвращаются
    local break_dist = H.find_item("Skip break distance check")
    H.fire(break_dist, true)
    check(report, H.read_hex(VIERA.break_a.pattern_addr + 6, 5) == "B0 00 90 90 90",
          "break distance A: вызов -> mov al,0 + nop")
    check(report, H.read_hex(VIERA.break_b.pattern_addr + 6, 5) == "B0 00 90 90 90",
          "break distance B: вызов -> mov al,0 + nop")
    H.fire(break_dist, false)
    check(report, H.read_byte(VIERA.break_a.pattern_addr + 6) == 0xE8, "break distance A возвращён")
    check(report, H.read_byte(VIERA.break_b.pattern_addr + 6) == 0xE8, "break distance B возвращён")

    -- 4. Defensive on backtrack: disp32 -> стаб, стаб корректен, VirtualFree
    local defensive = H.find_item("Defensive on backtrack")
    local jz_disp = VIERA.defensive.pattern_addr + 9      -- сам disp32 внутри 0F 84
    local original_disp = H.read_u32(jz_disp)
    H.fire(defensive, true)
    local stub = jz_disp + 4 + H.read_i32(jz_disp)      -- rel32 считаем от конца jz
    check(report, stub >= 0x72000000, "disp32 переписан на стаб")
    check(report, H.read_u32(jz_disp) ~= original_disp, "disp32 больше не указывает на старое место")
    check(report, H.read_hex(stub, 4) == "C6 46 1A 01", "стаб: mov byte [esi+0x1A], 1")
    check(report, H.read_byte(stub + 4) == 0xE9, "стаб: jmp обратно")
    local jump = H.read_i32(stub + 5)
    check(report, stub + 9 + jump == jz_disp + 4, "стаб возвращается ровно за jz")
    H.fire(defensive, false)
    check(report, H.read_u32(jz_disp) == original_disp, "disp32 возвращён")
    check(report, H.region_of(stub) == nil, "стаб освобождён через VirtualFree")

    -- 5. Всё включено -> shutdown возвращает всё
    H.fire(preserve, true)
    H.fire(early, true)
    H.fire(break_dist, true)
    H.fire(defensive, true)
    H.fire_event("shutdown")
    check(report, H.read_byte(preserve_site) == 0x0F, "shutdown: preserve возвращён")
    check(report, H.read_byte(early_site) == 0x4F, "shutdown: early возвращён")
    check(report, H.read_byte(VIERA.break_a.pattern_addr + 6) == 0xE8, "shutdown: break distance снят")
    check(report, H.read_u32(jz_disp) == original_disp, "shutdown: jz возвращён")

    return report
end

-- 6. viera: другой билд — шаблоны не находятся, ничего не пишется
function M.run_viera_missing_pattern(H, path)
    local report = new_report()
    H.reset()
    local base = 0x2C000000
    build_module(H, base)          -- без шаблонов viera
    load_script(H, path, { sv_maxunlag = 0.2 })

    local preserve = H.find_item("Preserve valid records")
    H.fire(preserve, true)
    check(report, preserve.value == false, "без шаблона переключатель гаснет")
    check(report, H.log_contains("шаблон не найден"), "отказ объяснён")

    -- после появления модуля скрипт не пишет в него сам: только по галочке
    local read = H.find_item("Read sites")
    H.press(read)
    check(report, H.log_contains("шаблон не найден"), "Read sites тоже видит отсутствие шаблонов")

    return report
end

-- 7. viera: база не захардкожена (другой адрес образа)
function M.run_viera_other_base(H, path)
    local report = new_report()
    H.reset()
    VIERA_AT = 0x500000
    local base = 0x1A400000
    build_module(H, base)
    plant_viera(H, base)
    load_script(H, path, { sv_maxunlag = 0.2 })

    local preserve = H.find_item("Preserve valid records")
    H.fire(preserve, true)
    check(report, H.read_byte(VIERA.preserve.pattern_addr + 6) == 0x00,
          "шаблон найден и на другой базе модуля")
    H.fire(preserve, false)

    return report
end

-- 8. viera: экстраполяция и пещеры выбора записи (полный перенос)

local W = 0xCC

local VIERA2 = {
    rs        = 0x520000,
    eix       = 0x521000,
    fb        = 0x522000,
    fn_valid  = 0x523000,
    fn_expired= 0x524000,
    addr_ret  = 0x525000,
    af        = 0x526000,
    ring_pat  = 0x527000,
    table     = 0x530000,
}

local function plant_viera2(H, base)
    local function at(offset) return base + offset end

    -- select_lagrecord: 28-байтовый шаблон + хвост, из которого берутся
    -- смещения addr_527/addr_7E2 (байты 33 и 35)
    H.write_bytes(at(VIERA2.rs), {
        0xF6, 0x81, 0xB0, 0x00, 0x00, 0x00, 0x08,
        0xF3, 0x0F, 0x10, 0x0C, 0x24,
        0x0F, 0x85, 0x11, 0x22, 0x33, 0x44,
        0x8B, 0x04, 0x87,
        0x80, 0xB8, 0xA4, 0x00, 0x00, 0x00, 0x00,
        0x8B, 0x0C, 0x24, 0x85, 0xC9,       -- +28..+32
        0x10,                              -- +33: rel8 для 74 xx
        0x74,                              -- +34
        0x20, 0x00, 0x00, 0x00,            -- +35..+38: rel32 для 0F 85
        0x90,                              -- +39: за патчем
    })
    -- адрес 527 = site + 34 + 0x10, addr_7E2 = site + 39 + 0x20
    local addr_7e2 = at(VIERA2.rs) + 39 + 0x20
    H.write_bytes(addr_7e2 + 8, { 0x11, 0x22, 0x33, 0x44 })   -- g_cmd_ctx (заглушка)

    -- GetEntityIndex: шаблон 8D 59 08 ... 83 F8 41; патчится +11
    H.write_bytes(at(VIERA2.eix), {
        0x8D, 0x59, 0x08, 0x8B, 0x41, 0x08, 0x89, 0xD9, 0xFF, 0x50, 0x28,
        0x83, 0xF8, 0x41,
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,   -- jb +5 -> ...
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
        0x90, 0x90, 0x90, 0x90, 0x90,
    })

    -- fallback: g_pdc читается из операнда mov ecx,[g_pdc]
    H.write_bytes(at(VIERA2.fb), {
        0x8B, 0x0D, 0x55, 0x66, 0x77, 0x88,
        0x8D, 0x87, 0xA4, 0x00, 0x00, 0x00,
        0xF3, 0x0F, 0x10, 0x87, 0x94, 0x00, 0x00, 0x00,
    })

    H.write_bytes(at(VIERA2.fn_valid), {
        0x83, 0x79, 0x10, 0x02, 0x73, 0x03, 0x31, 0xC0, 0xC3, 0x56, 0x8B, 0x41, 0x0C,
    })
    H.write_bytes(at(VIERA2.fn_expired), {
        0x56, 0xA1, 0x11, 0x22, 0x33, 0x44, 0x89, 0xC2, 0xC1, 0xEA, 0x1F, 0x01, 0xC2, 0xD1, 0xFA,
    })
    H.write_bytes(at(VIERA2.addr_ret), {
        0xFF, 0x74, 0x24, 0x08, 0xFF, 0x74, 0x24, 0x0C, 0xE8, 0x11, 0x22, 0x33, 0x44,
        0x89, 0xC6, 0x8B, 0x4C, 0x24, 0x0C, 0x31, 0xE1,
    })
    H.write_bytes(at(VIERA2.af), {
        0xF3, 0x0F, 0x10, 0x92, 0x70, 0x03, 0x00, 0x00,
        0xF3, 0x0F, 0x11, 0x90, 0x80, 0x01, 0x00, 0x00,
    })
    -- mov [eax+disp],imm32 ; mov eax,[ring_table_pointer]
    H.write_bytes(at(VIERA2.ring_pat), {
        0x89, 0x86, 0xB0, 0x00, 0x00, 0x00,
        0xA1, 0x00, 0x00, 0x00, 0x00,
        0x89, 0x86, 0xAC, 0x00, 0x00, 0x00,
    })
    H.write_u32(at(VIERA2.ring_pat) + 7, at(VIERA2.table) - 4)   -- указатель на таблицу

    -- кольцо игрока 1: таблица + index * 0x10940
    local ring = at(VIERA2.table) + 1 * 0x10940
    local records = at(VIERA2.table) + 0x2000
    local record_new = at(VIERA2.table) + 0x3000
    local record_old = at(VIERA2.table) + 0x3100
    H.write_u32(at(VIERA2.table) - 4, at(VIERA2.table))
    H.write_u32(ring + 0x04, records)
    H.write_i32(ring + 0x08, 8)          -- capacity
    H.write_i32(ring + 0x0C, 0)          -- head
    H.write_i32(ring + 0x10, 2)          -- count
    H.write_u32(records + 0, record_new)
    H.write_u32(records + 4, record_old)
    H.write_f32(record_new + 0x24, 10.0)  -- simulation_time
    H.write_f32(record_old + 0x24, 5.0)
    return ring, records
end

local function viera2_player(H, index, tpr, last_shot)
    local address = 0x5F0000
    H.write_f32(address + 0x4A0, tpr or 0)      -- m_flThirdpersonRecoil
    local player = { [0] = address, alive = true }
    player.get_index = function() return index end
    player.is_alive = function() return true end
    player.get_player_weapon = function() return { m_fLastShotTime = last_shot or 0 } end
    return player
end

function M.run_viera_extrapolation(H, path)
    local report = new_report()
    H.reset()
    VIERA_AT = 0x500000
    local base = 0x2C000000
    build_module(H, base)
    plant_viera2(H, base)

    -- вызов should_extrapolate: E8 rel32 -> куда-то
    local call_site = base + 0x528000
    H.write_bytes(call_site, { 0xE8, 0x00, 0x00, 0x00, 0x00, 0x84, 0xC0, 0x74, 0x05, 0x89, 0xD9, 0xFF, 0x74, 0x24, 0x24 })
    H.write_i32(call_site + 1, 0x1000)      -- цель вызова
    -- глобалы breaklc / hittable в cmp-операндах
    local glob_site = base + 0x529000
    H.write_bytes(glob_site, {
        0xB1, 0x01, 0x31, 0xC0,
        0x80, 0x3D, 0x40, 0x00, 0x53, 0x2C, 0x00,
        0x0F, 0x85, 0x11, 0x22, 0x33, 0x44,
        0x80, 0x3D, 0x44, 0x00, 0x53, 0x2C, 0x00,
        0x0F, 0x84,
    })
    -- viera: первый cmp (+6) — hittable, второй (+19) — breaklc; стаб
    -- проверяет breaklc первым
    local hittable = 0x2C530040
    local breaklc  = 0x2C530044

    load_script(H, path, { sv_maxunlag = 0.2 })

    local switch = H.find_item("Extrapolation primary")
    check(report, switch ~= nil, "переключатель Extrapolation primary создан")
    H.fire(switch, true)

    check(report, H.read_byte(call_site) == 0xE9, "call -> jmp на стаб")
    local stub = call_site + 5 + H.read_i32(call_site + 1)
    check(report, stub >= 0x72000000, "стаб лежит в своей пещере")
    check(report, H.read_byte(stub) == 0x83 and H.read_byte(stub + 1) == 0x3D, "стаб: cmp dword [...]")
    check(report, H.read_u32(stub + 2) == breaklc, "стаб: первым проверяется breaklc")
    check(report, H.read_hex(stub + 7, 2) == "74 10", "стаб: jz на реальный вызов")
    check(report, H.read_u32(stub + 11) == hittable, "стаб: вторым hittable")
    check(report, H.read_hex(stub + 16, 2) == "75 07", "стаб: jnz на реальный вызов")
    check(report, H.read_hex(stub + 18, 7) == "B8 01 00 00 00 EB 05", "стаб: return 1 без вызова")
    check(report, H.read_byte(stub + 25) == 0xE8, "стаб: настоящий call")
    check(report, H.read_i32(stub + 26) == (call_site + 5 + 0x1000) - (stub + 30),
          "стаб: call ведёт в исходную функцию")
    check(report, H.read_byte(stub + 30) == 0xE9, "стаб: jmp назад")
    check(report, H.read_i32(stub + 31) == (call_site + 5) - (stub + 35),
          "стаб: возврат ровно за вызов")

    H.fire(switch, false)
    check(report, H.read_byte(call_site) == 0xE8, "call возвращён")
    check(report, H.read_i32(call_site + 1) == 0x1000, "rel32 вызова восстановлен")
    check(report, H.region_of(stub) == nil, "стаб освобождён")

    return report
end

function M.run_viera_record_selection(H, path)
    local report = new_report()
    H.reset()
    VIERA_AT = 0x500000
    local base = 0x2C000000
    build_module(H, base)
    local ring, records = plant_viera2(H, base)

    H.netvars = nil
    load_script(H, path, { sv_maxunlag = 0.2, netvars = { m_flThirdpersonRecoil = 0x4A0 } })

    local site = base + VIERA2.rs
    local eix_site = base + VIERA2.eix + 11
    local af_site = base + VIERA2.af
    local fb_site = base + VIERA2.fb

    local switch = H.find_item("Custom record selection")
    check(report, switch ~= nil, "переключатель Custom record selection создан")
    H.fire(switch, true)

    check(report, H.read_byte(site) == 0xE9, "select_lagrecord -> jmp в пещеру")
    local cave = site + 5 + H.read_i32(site + 1)
    check(report, cave >= 0x72000000, "пещера выделена")
    check(report, H.read_hex(site + 5, 34) == string.rep("90 ", 33) .. "90", "хвост сайта залит NOP")
    check(report, H.read_hex(cave, 4) == "56 57 52 51", "cave1: сохранение регистров")
    check(report, H.read_hex(cave + 0x15A, 16) == "00 00 00 41 00 00 34 43 00 00 34 C3 00 00 B4 43",
          "cave1: константы 8/180/-180/360 на месте")

    -- адреса внутри пещеры считаются от найденных сайтов
    local addr_7e2 = site + 39 + 0x20
    local g_cmd_ctx = H.read_u32(addr_7e2 + 8)
    check(report, H.read_u32(cave + 0x19) == g_cmd_ctx, "cave1: g_cmd_ctx из кода чита")
    local addr_ret = base + VIERA2.addr_ret + 15
    check(report, H.read_i32(cave + 0x12E) == addr_ret - (cave + 0x132),
          "cave1: возврат в исходную функцию")
    local addr_527 = site + 34 + 0x10
    check(report, H.read_i32(cave + 0x151) == addr_527 - (cave + 0x155),
          "cave1: ветка 527")
    check(report, H.read_i32(cave + 0x156) == addr_7e2 - (cave + 0x15A),
          "cave1: ветка 7E2")

    -- entindex-стаб
    check(report, H.read_byte(eix_site) == 0xE9, "GetEntityIndex перехвачен")
    local eix_stub = eix_site + 5 + H.read_i32(eix_site + 1)
    check(report, eix_stub == cave + 0x272, "стаб entindex внутри пещеры")
    check(report, H.read_hex(eix_stub, 3) == "A3 6E 02", "стаб: mov [g_entindex],eax")
    check(report, H.read_hex(eix_stub + 5, 3) == "83 F8 41", "стаб: cmp eax,41h как в оригинале")

    -- cave2 и animfix
    local cave2 = cave + 0x290
    check(report, H.read_hex(cave2, 4) == "51 52 8B C3", "cave2: начало")
    check(report, H.read_i32(cave2 + 0x102) == (fb_site + 6) - (cave2 + 0x106),
          "cave2: возврат на fb")
    check(report, H.read_byte(af_site) == 0xE9, "animfix thirdperson_recoil -> jmp")
    local cave3 = af_site + 5 + H.read_i32(af_site + 1)
    check(report, cave3 == cave + 0x16A, "cave3 внутри пещеры")
    check(report, H.read_u32(cave3 + 0x3F) == 0x4A0, "cave3: смещение thirdperson_recoil")
    check(report, H.read_hex(af_site + 5, 11) == string.rep("90 ", 10) .. "90", "animfix: хвост NOP")

    -- fb: отдельный переключатель
    local fb = H.find_item("Custom fallback selection")
    H.fire(fb, true)
    check(report, H.read_byte(fb_site) == 0xE9, "fb -> jmp в cave2")
    check(report, H.read_byte(fb_site + 5) == 0x90, "fb: шестой байт NOP")
    H.fire(fb, false)
    check(report, H.read_byte(fb_site) == 0x8B, "fb возвращён")

    -- таблицы для пещер: is_firing / tpr_arr
    local g_is_firing = cave + 0x3B0
    local g_tpr_arr = cave + 0x4B8
    H.entity.players = { viera2_player(H, 1, 12.5, 7.5) }
    H.fire_event("createmove")
    check(report, math.abs(H.read_f32(g_is_firing + 4) - 1.0) < 0.001,
          "пещеры: игрок 1 помечен как стреляющий")
    check(report, math.abs(H.read_f32(g_tpr_arr + 4) - 12.5) < 0.001,
          "пещеры: thirdperson_recoil прочитан")

    -- без выстрела: 0
    H.entity.players = { viera2_player(H, 1, 3.0, 0.0) }
    H.fire_event("createmove")
    check(report, math.abs(H.read_f32(g_is_firing + 4)) < 0.001, "пещеры: без выстрела 0")
    check(report, math.abs(H.read_f32(g_tpr_arr + 4) - 3.0) < 0.001, "пещеры: recoil всё равно пишется")

    -- снятие: всё возвращается, пещера освобождается
    H.fire(switch, false)
    check(report, H.read_byte(site) == 0xF6, "select_lagrecord восстановлен")
    check(report, H.read_byte(eix_site) == 0x83, "GetEntityIndex восстановлен")
    check(report, H.read_byte(af_site) == 0xF3, "animfix восстановлен")
    check(report, H.region_of(cave) == nil, "пещера освобождена через VirtualFree")

    return report
end

-- 9. viera: пещеры без нужных шаблонов не включаются
function M.run_viera_record_missing(H, path)
    local report = new_report()
    H.reset()
    local base = 0x2C000000
    build_module(H, base)
    load_script(H, path, { sv_maxunlag = 0.2, netvars = { m_flThirdpersonRecoil = 0x4A0 } })

    local rs = H.find_item("Custom record selection")
    H.fire(rs, true)
    check(report, rs.value == false, "без шаблонов переключатель гаснет")
    check(report, H.log_contains("НЕ включён"), "отказ залогирован")

    local extrap = H.find_item("Extrapolation primary")
    H.fire(extrap, true)
    check(report, extrap.value == false, "экстраполяция без шаблона тоже не включается")

    return report
end

-- 10. persistence: model hook в studiorender.dll (пещеры + управляющий блок)

local PERSIST = {
    rva = 0x536A6,
    pe  = 0x80,
}

local function plant_studiorender(H, base, prologue, rva)
    H.add_region(base, 0x200000, H.const.PAGE_EXECUTE_READ, H.const.MEM_IMAGE, false)
    H.write_u32(base + 0x3C, PERSIST.pe)
    H.write_u32(base + PERSIST.pe, 0x4550)          -- 'PE\0\0'
    rva = rva or PERSIST.rva
    H.write_bytes(base + rva, prologue or { 0x83, 0xEC, 0x54, 0x53, 0x56 })
    H.module_handles["studiorender.dll"] = base
    return base + rva
end

-- VirtualFree в моке только помечает регион, поэтому «сколько живых пещер»
-- считаем по регионам, а не по журналу выделений.
local function live_allocations(H)
    local live = 0
    for _, base in ipairs(H.allocations) do
        if H.region_of(base) ~= nil then live = live + 1 end
    end
    return live
end

local function count_log(H, fragment)
    local total = 0
    for _, list in ipairs({ H.log_lines, H.error_lines }) do
        for _, line in ipairs(list) do
            if line:find(fragment, 1, true) then total = total + 1 end
        end
    end
    return total
end

function M.run_persistence_model_hook(H, path)
    local report = new_report()
    H.reset()
    local base = 0x2D000000
    local entry = plant_studiorender(H, base)
    load_script(H, path, { sv_maxunlag = 0.2 })

    local switch = H.find_item("Model hook (persistence)")
    local rva_input = H.find_item("studiorender RVA (hex)")
    check(report, switch ~= nil, "переключатель Model hook (persistence) создан")
    check(report, rva_input ~= nil, "поле RVA создано")
    check(report, rva_input ~= nil and rva_input.value == "0x536A6", "RVA по умолчанию 0x536A6")
    check(report, switch ~= nil and switch.value == false, "хук по умолчанию выключен")
    check(report, H.read_hex(entry, 5) == "83 EC 54 53 56", "до включения entry не тронут")
    check(report, live_allocations(H) == 0, "до включения пещера не выделялась")

    H.fire(switch, true)

    check(report, H.read_byte(entry) == 0xE9, "entry: 5-байтовый E9")
    local cave = entry + 5 + H.read_i32(entry + 1)
    check(report, cave >= 0x72000000, "пещера выделена через VirtualAlloc")
    local region = H.region_of(cave)
    check(report, region ~= nil and region.protect == H.const.PAGE_EXECUTE_READWRITE,
          "пещера PAGE_EXECUTE_READWRITE")
    check(report, live_allocations(H) == 1, "выделение ровно одно")
    check(report, H.log_contains("persistence model hook: entry"), "установка залогирована")

    -- пещера 1: начало и абсолютные адреса управляющего блока
    check(report, H.read_hex(cave, 3) == "9C 50 A1", "cave1: pushfd / push eax / mov eax,[..]")
    local state = cave + 256
    check(report, H.read_u32(cave + 3)  == state,     "cave1: слот активности")
    check(report, H.read_u32(cave + 23) == state + 4, "cave1: thread id")
    check(report, H.read_u32(cave + 49) == state + 8, "cave1: указатель модели")
    check(report, H.read_u32(cave + 69) == state + 16, "cave1: флаг «не подменять цвет»")
    check(report, H.read_u32(cave + 82) == state + 20, "cave1: сохранённый указатель")
    check(report, H.read_u32(cave + 93)  == state + 40, "cave1: сохранённый цвет 0")
    check(report, H.read_u32(cave + 104) == state + 44, "cave1: сохранённый цвет 1")
    check(report, H.read_u32(cave + 115) == state + 48, "cave1: сохранённый цвет 2")
    check(report, H.read_u32(cave + 126) == state + 52, "cave1: сохранённый цвет 3")
    check(report, H.read_u32(cave + 131) == state + 24, "cave1: свой цвет 0")
    check(report, H.read_u32(cave + 142) == state + 28, "cave1: свой цвет 1")
    check(report, H.read_u32(cave + 153) == state + 32, "cave1: свой цвет 2")
    check(report, H.read_u32(cave + 164) == state + 36, "cave1: свой цвет 3")
    check(report, H.read_u32(cave + 176) == state + 16, "cave1: счётчик тронутых моделей")

    -- вызов пещеры 2 и ветка «цвет трогать нельзя»
    check(report, H.read_byte(cave + 59) == 0xE8, "cave1: A1 переделан в call")
    check(report, H.read_i32(cave + 60) == 0x80, "cave1: call ведёт на +192")
    check(report, cave + 59 + 5 + H.read_i32(cave + 60) == cave + 192, "cave1: цель call = пещера 2")
    check(report, H.read_i32(cave + 76) == 94, "cave1: jne +94")
    check(report, cave + 76 + 4 + 94 == cave + 174, "cave1: jne попадает на инкремент")
    check(report, H.read_byte(cave + 174) == 0xFF, "cave1: инкремент счётчика на месте")

    -- реплика пролога и возврат
    check(report, H.read_hex(cave + 182, 5) == "83 EC 54 53 56", "cave1: пролог скопирован")
    check(report, H.read_byte(cave + 187) == 0xE9, "cave1: jmp назад")
    check(report, H.read_i32(cave + 188) == (entry + 5) - (cave + 192), "cave1: rel32 на entry+5")

    -- пещера 2: копирование костей
    check(report, H.read_hex(cave + 192, 6) == "51 56 57 8B 75 10", "cave2: начало и mov esi,[ebp+10]")
    check(report, H.read_u32(cave + 192 + 12) == state + 56, "cave2: буфер костей")
    check(report, H.read_u32(cave + 192 + 22) == state + 60, "cave2: размер")
    check(report, H.read_u32(cave + 192 + 30) == state + 64, "cave2: счётчик копирований")
    check(report, H.read_u32(cave + 192 + 38) == state + 12, "cave2: указатель вызова")
    check(report, H.read_byte(cave + 192 + 26) == 0xF3 and H.read_byte(cave + 192 + 27) == 0xA5,
          "cave2: rep movsd")
    check(report, H.read_byte(cave + 192 + 49) == 0xC3, "cave2: возврат через ret")

    -- управляющий блок: тем же API, что у оригинала
    local api = NL_PERSISTENCE_MODEL
    check(report, api ~= nil, "управляющий блок доступен снаружи")
    check(report, api ~= nil and api:available() == true, "хук отчитывается как установленный")

    api:set_color(0.25, 0.5, 0.75, 1.0)
    check(report, math.abs(H.read_f32(state + 24) - 0.25) < 1e-6, "цвет: r")
    check(report, math.abs(H.read_f32(state + 28) - 0.5) < 1e-6,  "цвет: g")
    check(report, math.abs(H.read_f32(state + 32) - 0.75) < 1e-6, "цвет: b")
    check(report, math.abs(H.read_f32(state + 36) - 1.0) < 1e-6,  "цвет: a")

    api:activate(0x2B000000, 0x2B001000)
    check(report, H.read_u32(state) == 1, "блок активен")
    check(report, H.read_u32(state + 4) == H.thread_id, "thread id проставлен")
    check(report, H.read_u32(state + 8) == 0x2B000000, "указатель модели проставлен")
    check(report, H.read_u32(state + 12) == 0x2B001000, "указатель вызова проставлен")

    api:set_capture(0x2C000000, 0xB40)
    check(report, H.read_u32(state + 56) == 0x2C000000, "буфер костей записан")
    check(report, H.read_u32(state + 60) == 0xB40, "размер буфера записан")
    check(report, api:captured() == 0, "счётчик сброшен")
    H.write_u32(state + 64, 3)
    check(report, api:captured() == 3, "счётчик копирований читается обратно")

    api:skip_color(true)
    check(report, H.read_u32(state + 16) == 1, "флаг «не подменять цвет» выставлен")
    api:deactivate()
    check(report, H.read_u32(state) == 0, "блок деактивирован")

    -- повторное включение не выделяет вторую пещеру
    H.fire(switch, true)
    check(report, live_allocations(H) == 1, "повторное включение без нового VirtualAlloc")
    check(report, H.read_i32(entry + 1) == cave - (entry + 5), "entry указывает на ту же пещеру")
    for _ = 1, 3 do H.fire_event("net_update_end") end
    check(report, live_allocations(H) == 1, "тики не переустанавливают хук")

    -- выключение: байт в байт и освобождение
    H.fire(switch, false)
    check(report, H.read_hex(entry, 5) == "83 EC 54 53 56", "entry восстановлен байт в байт")
    check(report, H.region_of(cave) == nil, "пещера освобождена через VirtualFree")
    check(report, live_allocations(H) == 0, "живых пещер не осталось")
    check(report, H.log_contains("снят, entry возвращён"), "снятие залогировано")
    check(report, api:available() == false, "хук больше не активен")
    local ok = api:activate(1, 2)
    check(report, ok == false, "activate после снятия отказывает")

    return report
end

function M.run_persistence_rejections(H, path)
    local report = new_report()

    -- 1. модуля нет
    H.reset()
    load_script(H, path, { sv_maxunlag = 0.2 })
    local switch = H.find_item("Model hook (persistence)")
    H.fire(switch, true)
    check(report, switch.value == false, "без модуля переключатель гаснет")
    check(report, H.log_contains("studiorender.dll не загружен"), "отказ: модуль не загружен")
    check(report, live_allocations(H) == 0, "без модуля пещера не выделена")
    for _ = 1, 4 do H.fire_event("net_update_end") end
    check(report, count_log(H, "studiorender.dll не загружен") == 1,
          "после отказа тики не спамят в лог")

    -- 2. чужой пролог
    H.reset()
    local base = 0x2D000000
    local entry = plant_studiorender(H, base, { 0x48, 0x83, 0xEC, 0x28, 0x90 })
    load_script(H, path, { sv_maxunlag = 0.2 })
    switch = H.find_item("Model hook (persistence)")
    H.fire(switch, true)
    check(report, switch.value == false, "при чужом прологе переключатель гаснет")
    check(report, H.log_contains("пролог не совпал"), "отказ: пролог не совпал")
    check(report, H.read_hex(entry, 5) == "48 83 EC 28 90", "чужой entry не тронут")
    check(report, live_allocations(H) == 0, "при отказе пещера не выделена")

    -- 3. не PE-образ
    H.reset()
    base = 0x2D000000
    entry = plant_studiorender(H, base)
    H.write_u32(base + 0x3C, 0x5000)               -- e_lfanew вне 64..4096
    load_script(H, path, { sv_maxunlag = 0.2 })
    switch = H.find_item("Model hook (persistence)")
    H.fire(switch, true)
    check(report, switch.value == false, "при кривом e_lfanew переключатель гаснет")
    check(report, H.log_contains("не PE-образ"), "отказ: e_lfanew")
    check(report, H.read_hex(entry, 5) == "83 EC 54 53 56", "entry не тронут")

    -- 4. подпись PE не та
    H.reset()
    base = 0x2D000000
    entry = plant_studiorender(H, base)
    H.write_u32(base + PERSIST.pe, 0x1234)
    load_script(H, path, { sv_maxunlag = 0.2 })
    switch = H.find_item("Model hook (persistence)")
    H.fire(switch, true)
    check(report, H.log_contains("нет подписи PE"), "отказ: подпись PE")

    -- 5. кривой ввод RVA и переезд на другой RVA
    H.reset()
    base = 0x2D000000
    entry = plant_studiorender(H, base)
    local second = 0x60000
    H.write_bytes(base + second, { 0x83, 0xEC, 0x54, 0x53, 0x56 })
    load_script(H, path, { sv_maxunlag = 0.2 })
    switch = H.find_item("Model hook (persistence)")
    local rva_input = H.find_item("studiorender RVA (hex)")
    H.fire(rva_input, "не rva")
    check(report, H.log_contains("не похоже на RVA"), "кривой RVA отвергнут")
    H.fire(rva_input, "0x60000")
    check(report, not H.log_contains("не похоже на RVA 0x60000"), "нормальный RVA принят")
    H.fire(switch, true)
    check(report, H.read_byte(base + second) == 0xE9, "хук встал по новому RVA")
    check(report, H.read_hex(entry, 5) == "83 EC 54 53 56", "старый entry не тронут")
    H.fire(switch, false)
    check(report, H.read_hex(base + second, 5) == "83 EC 54 53 56", "новый RVA восстановлен")

    -- 6. переезд RVA при уже включённом хуке
    H.reset()
    base = 0x2D000000
    entry = plant_studiorender(H, base)
    H.write_bytes(base + second, { 0x83, 0xEC, 0x54, 0x53, 0x56 })
    load_script(H, path, { sv_maxunlag = 0.2 })
    switch = H.find_item("Model hook (persistence)")
    rva_input = H.find_item("studiorender RVA (hex)")
    H.fire(switch, true)
    check(report, H.read_byte(entry) == 0xE9, "хук стоит на RVA по умолчанию")
    H.fire(rva_input, "0x60000")
    check(report, H.read_hex(entry, 5) == "83 EC 54 53 56", "старый entry возвращён при переезде")
    check(report, H.read_byte(base + second) == 0xE9, "хук перенесён на новый RVA")
    check(report, live_allocations(H) == 1, "переезд не оставляет вторую пещеру")

    return report
end

return M
