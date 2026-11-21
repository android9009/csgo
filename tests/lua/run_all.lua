--[[============================================================================
    tests/lua/run_all.lua — прогон всех сценариев и печать отчёта.

    Запуск (из корня репозитория):
        python3 tests/lua/run.py

    Пути передаются снаружи через переменные окружения или аргументы, чтобы
    файл одинаково работал и из python-драйвера, и вручную.
============================================================================]]

-- print перетирается моком (он пишет в лог), поэтому сохраняем настоящий
local out = print

local here = (os.getenv("LUA_TESTS_DIR") or "tests/lua")
local repo = (os.getenv("LUA_REPO_ROOT") or ".")

local harness = assert(loadfile(here .. "/mock/harness.lua"))()
local skeet_tests = assert(loadfile(here .. "/test_skeet.lua"))()
local nl_tests = assert(loadfile(here .. "/test_nl.lua"))()

local skeet_path = repo .. "/lua-patches/skeet_all_patches.lua"
local nl_path    = repo .. "/lua-patches/nl_all_patches.lua"

local scenarios = {
    { name = "skeet / kernel32 path",   fn = function() return skeet_tests.run_kernel32_path(harness, skeet_path) end },
    { name = "skeet / signature gate",  fn = function() return skeet_tests.run_gate_path(harness, skeet_path) end },
    { name = "skeet / rejections",      fn = function() return skeet_tests.run_rejections(harness, skeet_path) end },
    { name = "skeet / menu animation",  fn = function() return skeet_tests.run_menu_animation(harness, skeet_path) end },
    { name = "nl / full cycle",         fn = function() return nl_tests.run_cycle(harness, nl_path) end },
    { name = "nl / other module base",  fn = function() return nl_tests.run_other_base(harness, nl_path) end },
    { name = "nl / panic + shutdown",   fn = function() return nl_tests.run_panic(harness, nl_path) end },
    { name = "nl / missing base",       fn = function() return nl_tests.run_missing_base(harness, nl_path) end },
    { name = "skeet / panchan groups",  fn = function() return skeet_tests.run_panchan(harness, skeet_path) end },
    { name = "skeet / panchan rejects", fn = function() return skeet_tests.run_panchan_rejections(harness, skeet_path) end },
    { name = "skeet / panchan base",    fn = function() return skeet_tests.run_panchan_base(harness, skeet_path) end },
    { name = "nl / viera patches",      fn = function() return nl_tests.run_viera(harness, nl_path) end },
    { name = "nl / viera no pattern",   fn = function() return nl_tests.run_viera_missing_pattern(harness, nl_path) end },
    { name = "nl / viera other base",   fn = function() return nl_tests.run_viera_other_base(harness, nl_path) end },
    { name = "nl / viera extrapolation", fn = function() return nl_tests.run_viera_extrapolation(harness, nl_path) end },
    { name = "nl / viera record select", fn = function() return nl_tests.run_viera_record_selection(harness, nl_path) end },
    { name = "nl / viera records missing", fn = function() return nl_tests.run_viera_record_missing(harness, nl_path) end },
    { name = "nl / persistence model hook", fn = function() return nl_tests.run_persistence_model_hook(harness, nl_path) end },
    { name = "nl / persistence rejects",      fn = function() return nl_tests.run_persistence_rejections(harness, nl_path) end },
}

local total_passed, total_failed = 0, 0

for _, scenario in ipairs(scenarios) do
    local ok, report = pcall(scenario.fn)
    if not ok then
        out(string.format("ERROR %-28s %s", scenario.name, tostring(report)))
        total_failed = total_failed + 1
    else
        total_passed = total_passed + report.passed
        total_failed = total_failed + #report.failures
        out(string.format("%-30s %2d ok, %d fail", scenario.name, report.passed, #report.failures))
        for _, failure in ipairs(report.failures) do
            out("    FAIL: " .. failure)
        end
    end
end

out(string.format("TOTAL %d passed, %d failed", total_passed, total_failed))

return total_failed
