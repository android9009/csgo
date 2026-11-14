#pragma once

// Debug-only contract between the two halves of the diagnostics:
//   - runtime/debug_console.cpp, running inside csgo.exe as part of airflow_shell.dll
//   - injector/injector.cpp, the console the user actually looks at
// Both append to the same session log; the injector prints that log and reacts to the markers
// below, so the strings must stay identical on both sides. Nothing here is used in Release.
namespace debug_session {
inline constexpr const wchar_t* directory_chain[] = {
    L"C:\\dev", L"C:\\dev\\csgo", L"C:\\dev\\csgo\\Bins"
};
inline constexpr wchar_t log_path[] = L"C:\\dev\\csgo\\Bins\\debug.log";
inline constexpr char log_path_a[] = "C:\\dev\\csgo\\Bins\\debug.log";
inline constexpr wchar_t previous_log_path[] = L"C:\\dev\\csgo\\Bins\\debug_prev.log";
inline constexpr wchar_t minidump_path[] = L"C:\\dev\\csgo\\Bins\\crash_last.dmp.txt";
// Text crash logs: <prefix>YYYY-MM-DD_HH-MM-SS.txt
inline constexpr char crash_log_prefix[] = "C:\\dev\\csgo\\Bins\\crash_";

// While this event exists the injector owns the console, so the DLL does not open a second one.
inline constexpr wchar_t injector_event[] = L"Local\\airflow_injector_console";

// Line sources.
inline constexpr char injector_tag[] = "[injector]";
inline constexpr char shell_tag[] = "[shell";

// Severity tags (also drive the console colours).
inline constexpr char ok_tag[] = "[OK]";
inline constexpr char warn_tag[] = "[WARN]";
inline constexpr char fail_tag[] = "[FAIL]";
inline constexpr char crash_tag[] = "[CRASH]";
inline constexpr char first_chance_tag[] = "FIRST-CHANCE";

// Startup milestones reported by the DLL.
inline constexpr char hooks_installed[] = "[OK] D3D9 Present/Reset hooks installed";
inline constexpr char hooks_failed[] = "[FAIL] D3D9 hook initialization failed - the menu cannot render";
inline constexpr char renderer_ready[] = "[OK] renderer initialized (ImGui, fonts, window procedure, config, menu)";
inline constexpr char game_ready[] = "[OK] game interfaces, netvars and hooks are ready";

// "<crash_saved><full path>" - written after a text crash log has been stored.
inline constexpr char crash_saved[] = "crash log saved: ";
} // namespace debug_session
