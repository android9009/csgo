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

// Layout of this file:
//   write_line()        one log line -> session log file (+ own console when no injector is attached)
//   symbol helpers      function name / source line for addresses inside airflow_shell.dll
//   report_exception()  readable crash report: reason, place, last shell frame, registers, stack
//   save_crash_log()    copies the session log to C:\dev\csgo\Bins\crash_<date>_<time>.txt
//   start()             console or injector attach, log rotation, exception/CRT/abort handlers
namespace debug_console {
namespace {
HANDLE console_output = nullptr;
bool injector_attached = false;
HMODULE self_module = nullptr;
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

// DbgHelp is single threaded; crash reports from two threads must not interleave inside it.
SRWLOCK symbol_lock = SRWLOCK_INIT;
HANDLE symbol_process = nullptr;
bool symbols_attempted = false;
bool symbols_ready = false;

SRWLOCK crash_log_lock = SRWLOCK_INIT;
char crash_log_path[MAX_PATH] = "";

void ensure_log_directory() {
    for (const wchar_t* directory : debug_session::directory_chain)
        CreateDirectoryW(directory, nullptr);
}

void write_line(const char* message) {
    if (!message)
        return;

    SYSTEMTIME time{};
    GetLocalTime(&time);
    char line[2300];
    const int length = _snprintf_s(line, _TRUNCATE, "[%02u:%02u:%02u.%03u] %s %5lu] %s\r\n",
        time.wHour, time.wMinute, time.wSecond, time.wMilliseconds, debug_session::shell_tag,
        GetCurrentThreadId(), message);
    if (length < 0)
        return;

    if (console_output && console_output != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteConsoleA(console_output, line, static_cast<DWORD>(length), &written, nullptr);
    }
    OutputDebugStringA(line);

    // Opened and closed per line on purpose: everything written before a crash is already in the
    // file, and the injector can read (or rotate) the log at any moment.
    HANDLE file = CreateFileW(debug_session::log_path, FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
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

// Plain-language cause of an exception, e.g. "tried to read memory at 0x00000010 - null pointer".
void describe_reason(const EXCEPTION_RECORD* record, char* text, std::size_t capacity) {
    switch (record->ExceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_IN_PAGE_ERROR:
        if (record->NumberParameters >= 2) {
            const ULONG_PTR kind = record->ExceptionInformation[0];
            const ULONG_PTR target = record->ExceptionInformation[1];
            const char* operation = kind == 0 ? "read" : kind == 1 ? "write to" : "execute";
            const char* hint =
                kind == 8 ? " - a call or jump through an invalid function pointer" :
                target < 0x10000 ? " - null pointer: an object pointer was null and a field of it was used" :
                " - invalid or already freed pointer";
            _snprintf_s(text, capacity, _TRUNCATE, "tried to %s memory at 0x%08lX%s", operation,
                static_cast<unsigned long>(target), hint);
        } else {
            _snprintf_s(text, capacity, _TRUNCATE, "invalid memory access");
        }
        return;
    case EXCEPTION_STACK_OVERFLOW:
        _snprintf_s(text, capacity, _TRUNCATE,
            "stack overflow - endless recursion or a very large local array");
        return;
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        _snprintf_s(text, capacity, _TRUNCATE, "integer division by zero");
        return;
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
        _snprintf_s(text, capacity, _TRUNCATE,
            "the CPU ran bytes that are not valid code - a hook or pointer led to a wrong address");
        return;
    case 0x80000003:
        _snprintf_s(text, capacity, _TRUNCATE, "breakpoint (int3) - usually a failed assert");
        return;
    case 0xC0000374:
        _snprintf_s(text, capacity, _TRUNCATE,
            "heap corruption - a write past the end of an allocation or a double free");
        return;
    case 0xC0000409:
        _snprintf_s(text, capacity, _TRUNCATE,
            "fast fail - stack buffer overrun check, invalid CRT argument or std::terminate");
        return;
    default:
        _snprintf_s(text, capacity, _TRUNCATE, "see the exception code");
        return;
    }
}

struct address_owner {
    HMODULE module = nullptr;
    char name[MAX_PATH] = "unknown";
    DWORD_PTR offset = 0;
};

address_owner owner_of(const void* address) {
    address_owner owner;
    MEMORY_BASIC_INFORMATION memory{};
    if (!address || !VirtualQuery(address, &memory, sizeof(memory)) || !memory.AllocationBase)
        return owner;
    owner.module = static_cast<HMODULE>(memory.AllocationBase);
    owner.offset = reinterpret_cast<DWORD_PTR>(address) -
        reinterpret_cast<DWORD_PTR>(memory.AllocationBase);
    char path[MAX_PATH];
    if (GetModuleFileNameA(owner.module, path, MAX_PATH)) {
        const char* slash = std::strrchr(path, '\\');
        strncpy_s(owner.name, sizeof(owner.name), slash ? slash + 1 : path, _TRUNCATE);
    }
    return owner;
}

// Loads the PDB of airflow_shell.dll only, on first use. A private process handle keeps this
// DbgHelp session separate from any session the game opened for itself. Caller holds symbol_lock.
bool prepare_symbols() {
    if (symbols_attempted)
        return symbols_ready;
    symbols_attempted = true;
    if (!self_module)
        return false;

    char path[MAX_PATH];
    const DWORD path_length = GetModuleFileNameA(self_module, path, MAX_PATH);
    if (!path_length || path_length >= MAX_PATH)
        return false;
    char directory[MAX_PATH];
    strncpy_s(directory, sizeof(directory), path, _TRUNCATE);
    if (char* slash = std::strrchr(directory, '\\'))
        *slash = '\0';

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(self_module);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        reinterpret_cast<const BYTE*>(self_module) + dos->e_lfanew);

    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(),
            &symbol_process, 0, FALSE, DUPLICATE_SAME_ACCESS))
        return false;
    SymSetOptions(SymGetOptions() | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_FAIL_CRITICAL_ERRORS);
    if (!SymInitialize(symbol_process, directory, FALSE))
        return false;
    if (!SymLoadModuleEx(symbol_process, nullptr, path, nullptr,
            static_cast<DWORD64>(reinterpret_cast<DWORD_PTR>(self_module)),
            nt->OptionalHeader.SizeOfImage, nullptr, 0))
        return false;
    symbols_ready = true;
    return true;
}

// "function+0x12 (file.cpp:345)" for an address inside airflow_shell.dll; empty if unavailable.
void symbolize(const void* address, char* text, std::size_t capacity) {
    text[0] = '\0';
    AcquireSRWLockExclusive(&symbol_lock);
    if (prepare_symbols()) {
        alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO) + 256]{};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(storage);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        const DWORD64 target = static_cast<DWORD64>(reinterpret_cast<DWORD_PTR>(address));
        DWORD64 displacement = 0;
        if (SymFromAddr(symbol_process, target, &displacement, symbol)) {
            IMAGEHLP_LINE64 line{};
            line.SizeOfStruct = sizeof(line);
            DWORD line_displacement = 0;
            if (SymGetLineFromAddr64(symbol_process, target, &line_displacement, &line) && line.FileName) {
                const char* file = std::strrchr(line.FileName, '\\');
                _snprintf_s(text, capacity, _TRUNCATE, "%s+0x%lX (%s:%lu)", symbol->Name,
                    static_cast<unsigned long>(displacement), file ? file + 1 : line.FileName,
                    line.LineNumber);
            } else {
                _snprintf_s(text, capacity, _TRUNCATE, "%s+0x%lX", symbol->Name,
                    static_cast<unsigned long>(displacement));
            }
        }
    }
    ReleaseSRWLockExclusive(&symbol_lock);
}

// "client.dll+0x003A1F20" or, inside the shell, "airflow_shell.dll+0x0001A2B0 function (file:line)".
void describe_address(const void* address, char* text, std::size_t capacity, bool* in_shell) {
    const address_owner owner = owner_of(address);
    const bool shell = owner.module && owner.module == self_module;
    if (in_shell)
        *in_shell = shell;
    char symbol[400] = "";
    if (shell)
        symbolize(address, symbol, sizeof(symbol));
    _snprintf_s(text, capacity, _TRUNCATE, "%s+0x%08lX%s%s", owner.name,
        static_cast<unsigned long>(owner.offset), symbol[0] ? " " : "", symbol);
}

void write_minidump(EXCEPTION_POINTERS* exception) {
    ensure_log_directory();
    HANDLE file = CreateFileW(debug_session::minidump_path, GENERIC_WRITE,
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

    const auto dump_type = static_cast<MINIDUMP_TYPE>(MiniDumpNormal |
        MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithFullMemoryInfo);
    const BOOL dumped = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
        dump_type, exception_info_pointer, nullptr, nullptr);
    const DWORD error = dumped ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (dumped)
        log("minidump: C:\\dev\\csgo\\Bins\\crash_last.dmp.txt");
    else
        logf("minidump: MiniDumpWriteDump failed, error=%lu", error);
}

// Copies the session log (everything up to and including the crash report) to a text file of its
// own, so the next session cannot overwrite it. A second fatal report in the same process
// refreshes the same file. The injector reads the path from the marker line.
void save_crash_log() {
    AcquireSRWLockExclusive(&crash_log_lock);
    if (!crash_log_path[0]) {
        SYSTEMTIME time{};
        GetLocalTime(&time);
        _snprintf_s(crash_log_path, _TRUNCATE, "%s%04u-%02u-%02u_%02u-%02u-%02u.txt",
            debug_session::crash_log_prefix, time.wYear, time.wMonth, time.wDay,
            time.wHour, time.wMinute, time.wSecond);
    }
    ensure_log_directory();
    const BOOL copied = CopyFileA(debug_session::log_path_a, crash_log_path, FALSE);
    const DWORD error = copied ? ERROR_SUCCESS : GetLastError();
    ReleaseSRWLockExclusive(&crash_log_lock);

    if (copied)
        logf("%s%s", debug_session::crash_saved, crash_log_path);
    else
        logf("%s crash log was not saved to %s, error=%lu", debug_session::fail_tag,
            crash_log_path, error);
}

// EBP-chain walk. Debug builds of the shell and most game code keep frame pointers, which is
// enough to name the callers. Reports the innermost frame that belongs to airflow_shell.dll.
void log_stack(const CONTEXT* context, const void** first_shell_frame) {
    if (!context)
        return;
    log("  stack (innermost first):");
    const void* instruction = reinterpret_cast<const void*>(context->Eip);
    DWORD_PTR frame = static_cast<DWORD_PTR>(context->Ebp);
    for (int depth = 0; depth < 16 && instruction; ++depth) {
        char place[700];
        bool in_shell = false;
        describe_address(instruction, place, sizeof(place), &in_shell);
        logf("    #%-2d %p %s", depth, instruction, place);
        if (in_shell && first_shell_frame && !*first_shell_frame)
            *first_shell_frame = instruction;

        if (!frame)
            break;
        MEMORY_BASIC_INFORMATION info{};
        const auto* frame_memory = reinterpret_cast<const DWORD_PTR*>(frame);
        if (!VirtualQuery(frame_memory, &info, sizeof(info)) || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0)
            break;
        const DWORD_PTR next_frame = frame_memory[0];
        instruction = reinterpret_cast<const void*>(frame_memory[1]);
        if (next_frame <= frame)
            frame = 0;
        else
            frame = next_frame;
    }
}

void report_exception(EXCEPTION_POINTERS* exception, bool fatal, bool dump) {
    if (reporting || !exception || !exception->ExceptionRecord)
        return;
    reporting = true;

    const EXCEPTION_RECORD* record = exception->ExceptionRecord;
    const char* stage = current_stage ? current_stage : "unknown";
    const char* tag = fatal ? debug_session::crash_tag : debug_session::first_chance_tag;

    char place[700];
    bool fault_in_shell = false;
    describe_address(record->ExceptionAddress, place, sizeof(place), &fault_in_shell);
    char reason[256];
    describe_reason(record, reason, sizeof(reason));

    if (fatal)
        log("==================== CRASH ====================");
    logf("%s %s (code 0x%08lX)%s", tag, exception_name(record->ExceptionCode),
        record->ExceptionCode,
        fatal ? "" : " - first-chance: the game may still handle it; fatal only if a CRASH report or a process exit follows");
    logf("  reason: %s", reason);
    logf("  where : %s", place);
    logf("  stage : %s (last stage set on this thread)", stage);

    const void* first_shell_frame = fault_in_shell ? record->ExceptionAddress : nullptr;
#if defined(_M_IX86) || defined(__i386__)
    if (exception->ContextRecord) {
        const CONTEXT* context = exception->ContextRecord;
        logf("  registers: EIP=0x%08lX ESP=0x%08lX EBP=0x%08lX EAX=0x%08lX EBX=0x%08lX ECX=0x%08lX EDX=0x%08lX ESI=0x%08lX EDI=0x%08lX",
            context->Eip, context->Esp, context->Ebp, context->Eax, context->Ebx,
            context->Ecx, context->Edx, context->Esi, context->Edi);
        log_stack(context, &first_shell_frame);
    }
#endif

    if (fault_in_shell) {
        logf("  verdict: the fault is inside airflow_shell.dll -> %s", place);
    } else if (first_shell_frame) {
        char caller[700];
        describe_address(first_shell_frame, caller, sizeof(caller), nullptr);
        logf("  verdict: the fault is in game code called from airflow_shell.dll -> %s", caller);
    } else {
        log("  verdict: no airflow_shell.dll frame was found on the stack; the stage above is the best hint");
    }

    if (dump)
        write_minidump(exception);
    if (fatal) {
        log("================================================");
        save_crash_log();
    }
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
    report_exception(exception, false, dump);
    return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI unhandled_exception(EXCEPTION_POINTERS* exception) {
    report_exception(exception, true, true);
    if (previous_filter)
        return previous_filter(exception);
    return EXCEPTION_CONTINUE_SEARCH;
}

int __cdecl crt_report_hook(int report_type, char* message, int* return_value) {
    if (!reporting) {
        reporting = true;
        const char* kind = report_type == _CRT_ASSERT ? "assert failed" :
            report_type == _CRT_ERROR ? "runtime error" : "warning";
        logf("%s CRT %s: %s", report_type == _CRT_WARN ? debug_session::warn_tag :
            debug_session::fail_tag, kind, message ? message : "no message");
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
    log("==================== CRASH ====================");
    logf("%s abort() was called (signal %d)", debug_session::crash_tag, signal_value);
    log("  reason: the C++ runtime stopped the process - failed assert, std::terminate, pure virtual call or an uncaught C++ exception; see the CRT line above if there is one");
    logf("  stage : %s (last stage set on this thread)", stage ? stage : "unknown");
    write_minidump(nullptr);
    log("================================================");
    save_crash_log();
    reporting = false;
}

bool injector_console_present() {
    const HANDLE event = OpenEventW(SYNCHRONIZE, FALSE, debug_session::injector_event);
    if (!event)
        return false;
    CloseHandle(event);
    return true;
}
} // namespace

void start() {
    if (started)
        return;
    started = true;

    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&start), &self_module);
    ensure_log_directory();

    // With airflow_injector.exe running, its console is the single place where the log is shown:
    // the injector has already started this session's debug.log and prints every line of it.
    // Loaded any other way, the DLL keeps its own console and starts a fresh session log itself.
    injector_attached = injector_console_present();
    if (!injector_attached) {
        MoveFileExW(debug_session::log_path, debug_session::previous_log_path,
            MOVEFILE_REPLACE_EXISTING);
        // The worker calls this outside DllMain, so console/CRT initialization is not under loader lock.
        AllocConsole();
        SetConsoleTitleW(L"airflow_shell - Debug diagnostics");
        SetConsoleOutputCP(CP_UTF8);
        console_output = GetStdHandle(STD_OUTPUT_HANDLE);
    }

    previous_filter = SetUnhandledExceptionFilter(&unhandled_exception);
    vectored_registration = AddVectoredExceptionHandler(1, &vectored_exception);
    previous_crt_hook = _CrtSetReportHook(&crt_report_hook);
    std::signal(SIGABRT, &abort_signal);
    current_stage = "runtime worker";

    char module_path[MAX_PATH] = "unknown";
    if (self_module)
        GetModuleFileNameA(self_module, module_path, MAX_PATH);
    logf("airflow_shell.dll (Debug) is running inside csgo.exe, pid=%lu, base=%p, file=%s",
        GetCurrentProcessId(), static_cast<void*>(self_module), module_path);
    logf("debug output: %s; log=C:\\dev\\csgo\\Bins\\debug.log; on a crash: crash_<date>_<time>.txt and crash_last.dmp.txt in the same folder",
        injector_attached ? "injector console" : "own console (injector is not running)");
    logf("%s crash handlers: unhandled filter=yes, vectored=%s, CRT reports=yes, abort=yes",
        vectored_registration ? debug_session::ok_tag : debug_session::warn_tag,
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
