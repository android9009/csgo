#pragma once

// Diagnostics are deliberately Debug-only. Release expands every hook to a no-op and does not
// allocate a console, write logs, install an exception filter, or reference DbgHelp.
#ifdef _DEBUG
namespace debug_console {
void start();
void log(const char* message);
void logf(const char* format, ...);
// `stage` must have static storage duration (string literals are intended). Stages are tracked
// per thread so renderer activity cannot overwrite the crashing game thread's last operation.
void set_stage(const char* stage);
}

#define AIRFLOW_DEBUG_START() ::debug_console::start()
#define AIRFLOW_DEBUG_LOG(message) ::debug_console::log(message)
#define AIRFLOW_DEBUG_LOGF(...) ::debug_console::logf(__VA_ARGS__)
#define AIRFLOW_DEBUG_STAGE(stage) ::debug_console::set_stage(stage)
#else
#define AIRFLOW_DEBUG_START() ((void)0)
#define AIRFLOW_DEBUG_LOG(message) ((void)0)
#define AIRFLOW_DEBUG_LOGF(...) ((void)0)
#define AIRFLOW_DEBUG_STAGE(stage) ((void)0)
#endif
