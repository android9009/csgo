#pragma once
// Compat shim: the ported FATALITY menu tree logs through the old project's
// LOG_* macros. The airflow build has no such backend; keep the menu code
// untouched by absorbing the calls here as no-ops.
inline void fatality_log_noop(const char* /*level*/, const char* /*fmt*/, ...) {}
#define LOG_INFO(...) ::fatality_log_noop("info", __VA_ARGS__)
#define LOG_WARN(...) ::fatality_log_noop("warn", __VA_ARGS__)
#define LOG_ERROR(...) ::fatality_log_noop("error", __VA_ARGS__)
