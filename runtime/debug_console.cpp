#include "debug_console.hpp"

#ifdef _DEBUG
#include <Windows.h>
#include <DbgHelp.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "dbghelp.lib")

namespace debug_console {
namespace {
HANDLE console_output = nullptr;
std::atomic<const char*> current_stage{"debug console not initialized"};
LPTOP_LEVEL_EXCEPTION_FILTER previous_filter = nullptr;
bool started = false;

void write_line(const char* message) {
    if (!message)
        return;

    char line[2300];
    const int length = _snprintf_s(line, _TRUNCATE, "[%llu][thread %lu] %s\n",
        static_cast<unsigned long long>(GetTickCount64()), GetCurrentThreadId(), message);
    if (length < 0)
        return;

    if (console_output && console_output != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteConsoleA(console_output, line, static_cast<DWORD>(length), &written, nullptr);
    }
    OutputDebugStringA(line);

    CreateDirectoryW(L"C:\\airflow", nullptr);
    HANDLE file = CreateFileW(L"C:\\airflow\\debug.log", FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    DWORD written = 0;
    WriteFile(file, line, static_cast<DWORD>(length), &written, nullptr);
    CloseHandle(file);
}

const char* exception_name(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "ACCESS_VIOLATION";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_DATATYPE_MISALIGNMENT: return "DATATYPE_MISALIGNMENT";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "ILLEGAL_INSTRUCTION";
    case EXCEPTION_IN_PAGE_ERROR: return "IN_PAGE_ERROR";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "INT_DIVIDE_BY_ZERO";
    case EXCEPTION_PRIV_INSTRUCTION: return "PRIV_INSTRUCTION";
    case EXCEPTION_STACK_OVERFLOW: return "STACK_OVERFLOW";
    default: return "EXCEPTION";
    }
}

void write_minidump(EXCEPTION_POINTERS* exception) {
    CreateDirectoryW(L"C:\\airflow", nullptr);
    HANDLE file = CreateFileW(L"C:\\airflow\\crash_last.dmp", GENERIC_WRITE,
        FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        logf("minidump: CreateFile failed, error=%lu", GetLastError());
        return;
    }

    MINIDUMP_EXCEPTION_INFORMATION exception_info{};
    exception_info.ThreadId = GetCurrentThreadId();
    exception_info.ExceptionPointers = exception;
    exception_info.ClientPointers = TRUE;

    const BOOL dumped = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
        MiniDumpNormal, &exception_info, nullptr, nullptr);
    const DWORD error = dumped ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (dumped)
        log("minidump: C:\\airflow\\crash_last.dmp");
    else
        logf("minidump: MiniDumpWriteDump failed, error=%lu", error);
}

LONG WINAPI unhandled_exception(EXCEPTION_POINTERS* exception) {
    if (!exception || !exception->ExceptionRecord) {
        log("CRASH: invalid EXCEPTION_POINTERS");
        return EXCEPTION_CONTINUE_SEARCH;
    }

    EXCEPTION_RECORD* record = exception->ExceptionRecord;
    const void* address = record->ExceptionAddress;
    char module_path[MAX_PATH] = "unknown";
    const char* module_name = "unknown";
    DWORD_PTR module_offset = 0;

    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(address, &memory, sizeof(memory)) && memory.AllocationBase) {
        HMODULE module = static_cast<HMODULE>(memory.AllocationBase);
        if (GetModuleFileNameA(module, module_path, MAX_PATH)) {
            module_name = std::strrchr(module_path, '\\');
            module_name = module_name ? module_name + 1 : module_path;
        }
        module_offset = reinterpret_cast<DWORD_PTR>(address) -
            reinterpret_cast<DWORD_PTR>(memory.AllocationBase);
    }

    const char* stage = current_stage.load(std::memory_order_relaxed);
    logf("CRASH %s code=0x%08lX address=%p module=%s+0x%08lX stage=%s",
        exception_name(record->ExceptionCode), record->ExceptionCode, address, module_name,
        static_cast<unsigned long>(module_offset), stage ? stage : "unknown");

    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
        const char* operation = record->ExceptionInformation[0] == 0 ? "read" :
            record->ExceptionInformation[0] == 1 ? "write" : "execute";
        logf("access violation: %s address=0x%08lX", operation,
            static_cast<unsigned long>(record->ExceptionInformation[1]));
    }

#if defined(_M_IX86)
    if (exception->ContextRecord) {
        const CONTEXT* context = exception->ContextRecord;
        logf("registers: EIP=0x%08lX ESP=0x%08lX EBP=0x%08lX EAX=0x%08lX ECX=0x%08lX EDX=0x%08lX",
            context->Eip, context->Esp, context->Ebp, context->Eax, context->Ecx, context->Edx);
    }
#endif

    write_minidump(exception);
    if (previous_filter)
        return previous_filter(exception);
    return EXCEPTION_CONTINUE_SEARCH;
}
} // namespace

void start() {
    if (started)
        return;
    started = true;

    // The worker calls this outside DllMain, so console/CRT initialization is not under loader lock.
    AllocConsole();
    SetConsoleTitleW(L"airflow_shell - Debug diagnostics");
    SetConsoleOutputCP(CP_UTF8);
    console_output = GetStdHandle(STD_OUTPUT_HANDLE);
    previous_filter = SetUnhandledExceptionFilter(&unhandled_exception);
    current_stage.store("runtime worker", std::memory_order_relaxed);
    log("debug console ready; log=C:\\airflow\\debug.log; dump=C:\\airflow\\crash_last.dmp");
}

void log(const char* message) {
    write_line(message);
}

void logf(const char* format, ...) {
    if (!format)
        return;
    char message[2048];
    va_list arguments;
    va_start(arguments, format);
    _vsnprintf_s(message, _TRUNCATE, format, arguments);
    va_end(arguments);
    write_line(message);
}

void set_stage(const char* stage) {
    current_stage.store(stage, std::memory_order_relaxed);
}
} // namespace debug_console
#endif
