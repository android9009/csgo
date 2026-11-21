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

return M
