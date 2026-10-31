#include "debug_console.hpp"

#ifdef _DEBUG
#include <Windows.h>
#include <DbgHelp.h>
#include <crtdbg.h>

#include <atomic>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "dbghelp.lib")

namespace debug_console {
namespace {
HANDLE console_output = nullptr;
thread_local const char* current_stage = "debug console not initialized";
LPTOP_LEVEL_EXCEPTION_FILTER previous_filter = nullptr;
PVOID vectored_registration = nullptr;
_CRT_REPORT_HOOK previous_crt_hook = nullptr;
bool started = false;
thread_local bool reporting = false;

// A game can legitimately handle an SEH exception. Coalesce repeated first-chance reports from
// the same instruction so diagnostics do not flood the console or change game timing severely.
std::atomic<DWORD> last_first_chance_code{0};
std::atomic<const void*> last_first_chance_address{nullptr};
std::atomic<ULONGLONG> last_first_chance_tick{0};
std::atomic<bool> first_chance_dump_attempted{false};

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

    CreateDirectoryW(L"C:\\dev\\csgo\\Bins", nullptr);
    HANDLE file = CreateFileW(L"C:\\dev\\csgo\\Bins\\debug.log", FILE_APPEND_DATA,
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
    case 0x80000003: return "BREAKPOINT";
    case 0xC0000374: return "HEAP_CORRUPTION";
    case 0xC0000409: return "FAST_FAIL";
    case 0xC000041D: return "FATAL_USER_CALLBACK_EXCEPTION";
    default: return "EXCEPTION";
    }
}

bool reportable_first_chance(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    case EXCEPTION_DATATYPE_MISALIGNMENT:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_STACK_OVERFLOW:
    case 0x80000003:
    case 0xC0000374:
    case 0xC0000409:
    case 0xC000041D:
        return true;
    default:
        return false;
    }
}

void write_minidump(EXCEPTION_POINTERS* exception) {
    CreateDirectoryW(L"C:\\dev\\csgo\\Bins", nullptr);
    HANDLE file = CreateFileW(L"C:\\dev\\csgo\\Bins\\crash_last.dmp", GENERIC_WRITE,
        FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        logf("minidump: CreateFile failed, error=%lu", GetLastError());
        return;
    }

    MINIDUMP_EXCEPTION_INFORMATION exception_info{};
    MINIDUMP_EXCEPTION_INFORMATION* exception_info_pointer = nullptr;
    if (exception) {
        exception_info.ThreadId = GetCurrentThreadId();
        exception_info.ExceptionPointers = exception;
        exception_info.ClientPointers = TRUE;
        exception_info_pointer = &exception_info;
    }

    const BOOL dumped = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
        MiniDumpNormal, exception_info_pointer, nullptr, nullptr);
    const DWORD error = dumped ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (dumped)
        log("minidump: C:\\dev\\csgo\\Bins\\crash_last.dmp");
    else
        logf("minidump: MiniDumpWriteDump failed, error=%lu", error);
}

void report_exception(EXCEPTION_POINTERS* exception, const char* origin, bool dump) {
    if (reporting || !exception || !exception->ExceptionRecord)
        return;
    reporting = true;

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

    const char* stage = current_stage;
    logf("%s %s code=0x%08lX address=%p module=%s+0x%08lX stage=%s",
        origin, exception_name(record->ExceptionCode), record->ExceptionCode, address, module_name,
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
        logf("registers: EIP=0x%08lX ESP=0x%08lX EBP=0x%08lX EAX=0x%08lX EBX=0x%08lX ECX=0x%08lX EDX=0x%08lX ESI=0x%08lX EDI=0x%08lX",
            context->Eip, context->Esp, context->Ebp, context->Eax, context->Ebx,
            context->Ecx, context->Edx, context->Esi, context->Edi);
    }
#endif

    if (dump)
        write_minidump(exception);
    reporting = false;
}

LONG WINAPI vectored_exception(EXCEPTION_POINTERS* exception) {
    if (!exception || !exception->ExceptionRecord ||
        !reportable_first_chance(exception->ExceptionRecord->ExceptionCode))
        return EXCEPTION_CONTINUE_SEARCH;

    const DWORD code = exception->ExceptionRecord->ExceptionCode;
    const void* address = exception->ExceptionRecord->ExceptionAddress;
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG previous_tick = last_first_chance_tick.load(std::memory_order_relaxed);
    if (last_first_chance_code.load(std::memory_order_relaxed) == code &&
        last_first_chance_address.load(std::memory_order_relaxed) == address &&
        now - previous_tick < 2000)
        return EXCEPTION_CONTINUE_SEARCH;

    last_first_chance_code.store(code, std::memory_order_relaxed);
    last_first_chance_address.store(address, std::memory_order_relaxed);
    last_first_chance_tick.store(now, std::memory_order_relaxed);

    // Some game exits never reach our unhandled filter. Capture the first reportable exception
    // once; a later CRASH report still overwrites the dump with the final exception.
    const bool dump = !reporting && !first_chance_dump_attempted.exchange(true);
    report_exception(exception, "FIRST-CHANCE", dump);
    return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI unhandled_exception(EXCEPTION_POINTERS* exception) {
    report_exception(exception, "CRASH", true);
    if (previous_filter)
        return previous_filter(exception);
    return EXCEPTION_CONTINUE_SEARCH;
}

int __cdecl crt_report_hook(int report_type, char* message, int* return_value) {
    if (!reporting) {
        reporting = true;
        logf("CRT report type=%d: %s", report_type, message ? message : "no message");
        reporting = false;
    }
    if (previous_crt_hook)
        return previous_crt_hook(report_type, message, return_value);
    return FALSE; // preserve the CRT's default report/abort behavior
}

void __cdecl abort_signal(int signal_value) {
    if (reporting)
        return;
    reporting = true;
    const char* stage = current_stage;
    logf("CRASH SIGABRT signal=%d stage=%s", signal_value, stage ? stage : "unknown");
    write_minidump(nullptr);
    reporting = false;
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
    vectored_registration = AddVectoredExceptionHandler(1, &vectored_exception);
    previous_crt_hook = _CrtSetReportHook(&crt_report_hook);
    std::signal(SIGABRT, &abort_signal);
    current_stage = "runtime worker";
    log("debug console ready; log=C:\\dev\\csgo\\Bins\\debug.log; dump=C:\\dev\\csgo\\Bins\\crash_last.dmp");
    logf("debug handlers: vectored=%s; CRT reports and SIGABRT enabled",
        vectored_registration ? "yes" : "no");
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
    current_stage = stage;
}
} // namespace debug_console
#endif
