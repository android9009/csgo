// airflow_injector.exe - Debug-only loader and log console for airflow_shell.dll.
//
// One console for the whole session:
//   1. prints "open csgo" and waits for csgo.exe;
//   2. loads airflow_shell.dll through LoadLibraryW and explains every failure;
//   3. keeps running and prints the DLL's own log (C:\dev\csgo\Bins\debug.log) line by line;
//   4. when csgo.exe exits, tells a normal exit from a crash and, for a crash, stores the whole
//      session log in C:\dev\csgo\Bins\crash_<date>_<time>.txt.
#include <Windows.h>
#include <TlHelp32.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string>
#include <string_view>

#include "../runtime/debug_session.hpp"

namespace {
// ---------------------------------------------------------------------------------------------
// Console + session log
// ---------------------------------------------------------------------------------------------
HANDLE console_output = INVALID_HANDLE_VALUE;
bool console_colors = false;
WORD default_color = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;

// What the DLL has reported so far; filled by inspect_line() while the log is printed.
struct session_state {
    LONGLONG tail_offset = 0;      // bytes of debug.log already printed
    std::string partial;           // unfinished last line
    bool hooks_installed = false;
    bool hooks_failed = false;
    bool renderer_ready = false;
    bool game_ready = false;
    bool shell_spoke = false;      // at least one line came from the DLL
    bool crash_reported = false;   // the DLL wrote a [CRASH] report
    ULONGLONG last_exception_tick = 0;
    std::string crash_log_path;    // crash log already stored by the DLL, if any
} session;

bool contains(std::string_view text, std::string_view part) {
    return text.find(part) != std::string_view::npos;
}

WORD line_color(std::string_view line) {
    if (contains(line, debug_session::crash_tag) || contains(line, debug_session::fail_tag))
        return FOREGROUND_RED | FOREGROUND_INTENSITY;
    if (contains(line, debug_session::warn_tag) || contains(line, debug_session::first_chance_tag))
        return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    if (contains(line, debug_session::ok_tag))
        return FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    return default_color;
}

void print_line(std::string_view line) {
    if (console_colors)
        SetConsoleTextAttribute(console_output, line_color(line));
    DWORD written = 0;
    WriteFile(console_output, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    if (console_colors)
        SetConsoleTextAttribute(console_output, default_color);
    WriteFile(console_output, "\r\n", 2, &written, nullptr);
}

void inspect_line(std::string_view line) {
    // Only lines written by the DLL count; the injector's own lines merely mention the tags.
    if (contains(line, debug_session::injector_tag) || !contains(line, debug_session::shell_tag))
        return;
    session.shell_spoke = true;
    if (contains(line, debug_session::hooks_installed))
        session.hooks_installed = true;
    if (contains(line, debug_session::hooks_failed))
        session.hooks_failed = true;
    if (contains(line, debug_session::renderer_ready))
        session.renderer_ready = true;
    if (contains(line, debug_session::game_ready))
        session.game_ready = true;
    if (contains(line, debug_session::crash_tag))
        session.crash_reported = true;
    if (contains(line, debug_session::first_chance_tag))
        session.last_exception_tick = GetTickCount64();
    const std::size_t saved = line.find(debug_session::crash_saved);
    if (saved != std::string_view::npos)
        session.crash_log_path = std::string(line.substr(saved + std::strlen(debug_session::crash_saved)));
}

void ensure_log_directory() {
    for (const wchar_t* directory : debug_session::directory_chain)
        CreateDirectoryW(directory, nullptr);
}

// Prints everything that was appended to debug.log since the last call - by this injector or by
// the DLL. Printing from the file keeps both sources in one order in the console.
void pump_log() {
    const HANDLE file = CreateFileW(debug_session::log_path, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;

    LARGE_INTEGER size{};
    if (GetFileSizeEx(file, &size)) {
        if (size.QuadPart < session.tail_offset) { // the file was replaced
            session.tail_offset = 0;
            session.partial.clear();
        }
        LARGE_INTEGER position{};
        position.QuadPart = session.tail_offset;
        if (size.QuadPart > session.tail_offset && SetFilePointerEx(file, position, nullptr, FILE_BEGIN)) {
            char buffer[8192];
            DWORD read = 0;
            while (ReadFile(file, buffer, sizeof(buffer), &read, nullptr) && read) {
                session.tail_offset += read;
                session.partial.append(buffer, read);
            }
        }
    }
    CloseHandle(file);

    std::size_t start = 0;
    for (;;) {
        const std::size_t end = session.partial.find('\n', start);
        if (end == std::string::npos)
            break;
        std::string_view line(session.partial.data() + start, end - start);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        print_line(line);
        inspect_line(line);
        start = end + 1;
    }
    session.partial.erase(0, start);
}

// Appends one line to the session log and prints it. If the log file cannot be written the line
// still reaches the console.
void write_entry(const std::string& line) {
    bool stored = false;
    const HANDLE file = CreateFileW(debug_session::log_path, FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        const std::string text = line + "\r\n";
        DWORD written = 0;
        stored = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
            written == text.size();
        CloseHandle(file);
    }
    if (stored)
        pump_log();
    else
        print_line(line);
}

void log(const char* format, ...) {
    char message[1536];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);

    SYSTEMTIME time{};
    GetLocalTime(&time);
    char line[1700];
    std::snprintf(line, sizeof(line), "[%02u:%02u:%02u.%03u] %s %s", time.wHour, time.wMinute,
        time.wSecond, time.wMilliseconds, debug_session::injector_tag, message);
    write_entry(line);
}

std::string narrow(const wchar_t* text) {
    const int length = text ? static_cast<int>(std::wcslen(text)) : 0;
    if (!length)
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, length, result.data(), size, nullptr, nullptr);
    return result;
}

std::string system_message(DWORD error) {
    char* buffer = nullptr;
    const DWORD length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
        reinterpret_cast<char*>(&buffer), 0, nullptr);
    std::string message = length && buffer ? buffer : "unknown Windows error";
    if (buffer)
        LocalFree(buffer);
    while (!message.empty() && (message.back() == '\r' || message.back() == '\n' || message.back() == ' '))
        message.pop_back();
    return message;
}

void log_failure(const char* operation, DWORD error, const char* hint = nullptr) {
    log("%s %s failed: error %lu (%s)%s%s", debug_session::fail_tag, operation,
        static_cast<unsigned long>(error), system_message(error).c_str(), hint ? " - " : "",
        hint ? hint : "");
}

void start_session() {
    console_output = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleTitleW(L"airflow_shell - Debug injector and log");
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (GetConsoleScreenBufferInfo(console_output, &info)) {
        console_colors = true;
        default_color = info.wAttributes;
    }

    // debug.log always holds exactly one session; the previous one is kept as debug_prev.log.
    ensure_log_directory();
    MoveFileExW(debug_session::log_path, debug_session::previous_log_path, MOVEFILE_REPLACE_EXISTING);
    WIN32_FILE_ATTRIBUTE_DATA leftover{};
    if (GetFileAttributesExW(debug_session::log_path, GetFileExInfoStandard, &leftover)) {
        // Rotation failed (file busy): keep appending, but do not replay the old lines.
        session.tail_offset = (static_cast<LONGLONG>(leftover.nFileSizeHigh) << 32) | leftover.nFileSizeLow;
    }
}

// Tells the DLL that this console shows its log, so it does not open a console of its own.
// The null DACL lets csgo.exe open the event even when the injector runs elevated.
HANDLE announce_console() {
    SECURITY_DESCRIPTOR descriptor{};
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    if (InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION) &&
        SetSecurityDescriptorDacl(&descriptor, TRUE, nullptr, FALSE))
        attributes.lpSecurityDescriptor = &descriptor;
    return CreateEventW(&attributes, TRUE, FALSE, debug_session::injector_event);
}

// A console started by double click closes with the program; keep the result readable.
void wait_before_close() {
    DWORD processes[2]{};
    if (GetConsoleProcessList(processes, 2) > 1)
        return; // started from an existing terminal, which stays open
    print_line("");
    print_line("Press Enter to close this window.");
    (void)std::getchar();
}

// ---------------------------------------------------------------------------------------------
// DLL file checks
// ---------------------------------------------------------------------------------------------
bool default_dll_path(wchar_t* path, DWORD capacity) {
    const DWORD length = GetModuleFileNameW(nullptr, path, capacity);
    if (!length || length >= capacity) {
        log_failure("GetModuleFileNameW", length ? ERROR_INSUFFICIENT_BUFFER : GetLastError());
        return false;
    }

    wchar_t* separator = wcsrchr(path, L'\\');
    if (!separator) {
        log("%s cannot determine the injector directory", debug_session::fail_tag);
        return false;
    }

    constexpr wchar_t dll_name[] = L"airflow_shell.dll";
    const std::size_t directory_length = static_cast<std::size_t>(separator - path + 1);
    if (directory_length + std::size(dll_name) > capacity) {
        log("%s the default DLL path is too long", debug_session::fail_tag);
        return false;
    }

    std::memcpy(path + directory_length, dll_name, sizeof(dll_name));
    return true;
}

// The DLL must exist, be a DLL and be 32-bit: csgo.exe is a 32-bit process.
bool check_dll(const wchar_t* path) {
    const std::string shown = narrow(path);
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &data) ||
        (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        log("%s DLL not found: %s", debug_session::fail_tag, shown.c_str());
        log("        build the solution as Debug | x86 first, or pass the full DLL path as an argument");
        return false;
    }

    const HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        log_failure("opening the DLL", GetLastError());
        return false;
    }
    BYTE header[4096]{};
    DWORD read = 0;
    const BOOL read_ok = ReadFile(file, header, sizeof(header), &read, nullptr);
    CloseHandle(file);

    IMAGE_DOS_HEADER dos{};
    IMAGE_FILE_HEADER pe{};
    DWORD signature = 0;
    bool valid = read_ok && read >= sizeof(dos);
    if (valid) {
        std::memcpy(&dos, header, sizeof(dos));
        valid = dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew > 0 &&
            static_cast<std::size_t>(dos.e_lfanew) + sizeof(signature) + sizeof(pe) <= read;
    }
    if (valid) {
        std::memcpy(&signature, header + dos.e_lfanew, sizeof(signature));
        std::memcpy(&pe, header + dos.e_lfanew + sizeof(signature), sizeof(pe));
        valid = signature == IMAGE_NT_SIGNATURE;
    }
    if (!valid) {
        log("%s %s is not a valid Windows DLL (damaged file or unfinished build)",
            debug_session::fail_tag, shown.c_str());
        return false;
    }
    if (pe.Machine != IMAGE_FILE_MACHINE_I386) {
        log("%s the DLL is not 32-bit (machine type 0x%04X); csgo.exe is 32-bit - build Debug | x86",
            debug_session::fail_tag, static_cast<unsigned>(pe.Machine));
        return false;
    }
    if (!(pe.Characteristics & IMAGE_FILE_DLL)) {
        log("%s %s is an executable, not a DLL", debug_session::fail_tag, shown.c_str());
        return false;
    }

    FILETIME local_time{};
    SYSTEMTIME built{};
    FileTimeToLocalFileTime(&data.ftLastWriteTime, &local_time);
    FileTimeToSystemTime(&local_time, &built);
    const ULONGLONG size = (static_cast<ULONGLONG>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    log("%s DLL: %s", debug_session::ok_tag, shown.c_str());
    log("        32-bit, %llu KB, built %04u-%02u-%02u %02u:%02u:%02u", size / 1024,
        built.wYear, built.wMonth, built.wDay, built.wHour, built.wMinute, built.wSecond);
    return true;
}

// After LoadLibraryW returned null inside csgo.exe: map the DLL here without running it and look
// for imported DLLs that Windows cannot find. This is the usual reason for a silent load failure.
void explain_load_failure(const wchar_t* path) {
    const HMODULE image = LoadLibraryExW(path, nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!image) {
        log_failure("mapping the DLL in the injector", GetLastError());
        return;
    }

    const auto* base = reinterpret_cast<const BYTE*>(image);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY& directory =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    int missing = 0;
    if (directory.VirtualAddress && directory.Size) {
        const auto* descriptor =
            reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
        for (; descriptor->Name; ++descriptor) {
            const char* name = reinterpret_cast<const char*>(base + descriptor->Name);
            if (_strnicmp(name, "api-ms-", 7) == 0 || _strnicmp(name, "ext-ms-", 7) == 0)
                continue; // virtual API sets, resolved by the loader itself
            const HMODULE dependency = LoadLibraryExA(name, nullptr, LOAD_LIBRARY_AS_DATAFILE);
            if (dependency) {
                FreeLibrary(dependency);
                continue;
            }
            ++missing;
            log("%s missing dependency: %s (error %lu) - the DLL needs it and Windows cannot find it",
                debug_session::fail_tag, name, static_cast<unsigned long>(GetLastError()));
        }
    }
    FreeLibrary(image);
    if (!missing)
        log("        all imported DLLs were found; the DLL's own DllMain refused to start (app_runtime::start could not create the worker thread) or an antivirus blocked the load");
}

// ---------------------------------------------------------------------------------------------
// Target process
// ---------------------------------------------------------------------------------------------
DWORD find_csgo() {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    DWORD process_id = 0;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"csgo.exe") == 0) {
                process_id = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return process_id;
}

// Base address of a module loaded in the target, 0 if it is not loaded (or not loaded yet).
DWORD_PTR module_base(DWORD process_id, const wchar_t* name) {
    HANDLE snapshot = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 10; ++attempt) {
        snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, process_id);
        if (snapshot != INVALID_HANDLE_VALUE || GetLastError() != ERROR_BAD_LENGTH)
            break;
        Sleep(10); // the module list was changing, documented retry
    }
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    DWORD_PTR base = 0;
    if (Module32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szModule, name) == 0) {
                base = reinterpret_cast<DWORD_PTR>(entry.modBaseAddr);
                break;
            }
        } while (Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return base;
}

const wchar_t* file_name(const wchar_t* path) {
    const wchar_t* separator = wcsrchr(path, L'\\');
    return separator ? separator + 1 : path;
}

struct window_search {
    DWORD process_id = 0;
    HWND window = nullptr;
};

BOOL CALLBACK find_main_window(HWND window, LPARAM parameter) {
    auto* search = reinterpret_cast<window_search*>(parameter);
    DWORD owner = 0;
    GetWindowThreadProcessId(window, &owner);
    if (owner != search->process_id || !IsWindowVisible(window) || GetWindow(window, GW_OWNER))
        return TRUE;
    search->window = window;
    return FALSE;
}

const char* exit_code_meaning(DWORD code) {
    switch (code) {
    case 0: return "normal exit";
    case 1: return "terminated from outside (Task Manager, Steam or TerminateProcess)";
    case 0xC0000005: return "ACCESS_VIOLATION - invalid memory access";
    case 0xC00000FD: return "STACK_OVERFLOW - endless recursion or a huge local array";
    case 0xC0000409: return "FAST_FAIL - buffer overrun check, invalid CRT argument or std::terminate";
    case 0xC0000374: return "HEAP_CORRUPTION - write past an allocation or double free";
    case 0xC0000094: return "INT_DIVIDE_BY_ZERO";
    case 0xC000001D: return "ILLEGAL_INSTRUCTION - execution reached bytes that are not code";
    case 0xC0000096: return "PRIV_INSTRUCTION - execution reached bytes that are not code";
    case 0xC0000006: return "IN_PAGE_ERROR - a file-backed page could not be read";
    case 0xC000013A: return "closed by Ctrl+C or by closing its console";
    case 0xC0000135: return "DLL_NOT_FOUND - a required DLL is missing";
    case 0xC0000142: return "DLL_INIT_FAILED - a DLL failed to initialize";
    case 0x40010004: return "terminated by a debugger";
    case 3: return "abort() - failed assert or std::terminate";
    default: return code >= 0xC0000000u ? "unhandled exception" : "non-zero exit code";
    }
}

// ---------------------------------------------------------------------------------------------
// Session stages
// ---------------------------------------------------------------------------------------------
enum class outcome { exited, failed, done, attached };

// [2/6] Wait until csgo.exe exists. Returns whether the injector had to wait for it.
DWORD wait_for_csgo(bool& waited) {
    DWORD process_id = find_csgo();
    waited = !process_id;
    if (process_id) {
        log("%s [2/6] csgo.exe is already running, pid=%lu", debug_session::ok_tag,
            static_cast<unsigned long>(process_id));
        return process_id;
    }
    log("[2/6] csgo.exe is not running - waiting, start CS:GO now");
    while (!process_id) {
        Sleep(500);
        process_id = find_csgo();
    }
    log("%s [2/6] csgo.exe started, pid=%lu", debug_session::ok_tag,
        static_cast<unsigned long>(process_id));
    return process_id;
}

// [3/6] The game loads its modules for a while after the process appears. serverbrowser.dll is
// one of the last, so its presence means client.dll, engine.dll and the renderer are in place.
bool wait_for_game_modules(DWORD process_id, bool waited) {
    if (module_base(process_id, L"serverbrowser.dll")) {
        log("%s [3/6] the game has finished loading its modules", debug_session::ok_tag);
        if (waited)
            Sleep(2000); // let the main menu start presenting frames
        return true;
    }
    log("[3/6] waiting for the game to finish loading (serverbrowser.dll) ...");
    const ULONGLONG deadline = GetTickCount64() + 180000;
    while (GetTickCount64() < deadline) {
        Sleep(500);
        if (module_base(process_id, L"serverbrowser.dll")) {
            log("%s [3/6] the game has finished loading its modules", debug_session::ok_tag);
            Sleep(2000);
            return true;
        }
        if (find_csgo() != process_id) {
            log("%s [3/6] csgo.exe closed while it was still loading", debug_session::fail_tag);
            return false;
        }
    }
    log("%s [3/6] serverbrowser.dll did not appear in 180 s; injecting anyway, the shell retries its own setup every second",
        debug_session::warn_tag);
    return true;
}

bool process_exited(HANDLE process) {
    return WaitForSingleObject(process, 0) == WAIT_OBJECT_0;
}

// [4/6] Classic LoadLibraryW injection. Every failing Windows call is named with its error.
outcome inject(HANDLE process, DWORD process_id, const wchar_t* dll_path) {
    if (const DWORD_PTR loaded = module_base(process_id, file_name(dll_path))) {
        log("%s [4/6] %s is already loaded in csgo.exe (base=0x%08lX) - not loading it again",
            debug_session::warn_tag, narrow(file_name(dll_path)).c_str(),
            static_cast<unsigned long>(loaded));
        log("        this is the copy from the earlier injection; restart CS:GO to load a new build");
        return outcome::attached;
    }

    log("[4/6] injecting: VirtualAllocEx -> WriteProcessMemory -> CreateRemoteThread(LoadLibraryW)");
    const SIZE_T path_bytes = (std::wcslen(dll_path) + 1) * sizeof(wchar_t);
    void* remote_path = VirtualAllocEx(process, nullptr, path_bytes,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote_path) {
        log_failure("[4/6] VirtualAllocEx", GetLastError());
        return outcome::failed;
    }

    outcome result = outcome::failed;
    HANDLE thread = nullptr;
    bool release_remote_path = true;
    do {
        SIZE_T written = 0;
        if (!WriteProcessMemory(process, remote_path, dll_path, path_bytes, &written) ||
            written != path_bytes) {
            log_failure("[4/6] WriteProcessMemory", GetLastError());
            break;
        }

        const HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        const FARPROC load_library = kernel32 ? GetProcAddress(kernel32, "LoadLibraryW") : nullptr;
        if (!load_library) {
            log_failure("[4/6] GetProcAddress(LoadLibraryW)", GetLastError());
            break;
        }

        thread = CreateRemoteThread(process, nullptr, 0,
            reinterpret_cast<LPTHREAD_START_ROUTINE>(load_library), remote_path, 0, nullptr);
        if (!thread) {
            log_failure("[4/6] CreateRemoteThread", GetLastError(),
                "an antivirus or anti-cheat driver may be blocking remote threads");
            break;
        }

        // Wait for LoadLibraryW while showing whatever the DLL already logs.
        const ULONGLONG deadline = GetTickCount64() + 30000;
        DWORD wait_result = WAIT_TIMEOUT;
        while (GetTickCount64() < deadline) {
            wait_result = WaitForSingleObject(thread, 50);
            pump_log();
            if (wait_result != WAIT_TIMEOUT)
                break;
            if (process_exited(process)) {
                result = outcome::exited;
                release_remote_path = false;
                break;
            }
        }
        if (result == outcome::exited) {
            log("%s [4/6] csgo.exe exited while LoadLibraryW was running", debug_session::fail_tag);
            break;
        }
        if (wait_result != WAIT_OBJECT_0) {
            if (wait_result == WAIT_TIMEOUT) {
                log("%s [4/6] LoadLibraryW did not return in 30 s - the game is frozen or DllMain is blocked",
                    debug_session::fail_tag);
                release_remote_path = false; // the remote thread may still read the path
            } else {
                log_failure("[4/6] WaitForSingleObject", GetLastError());
            }
            break;
        }

        DWORD remote_module = 0;
        if (!GetExitCodeThread(thread, &remote_module)) {
            log_failure("[4/6] GetExitCodeThread", GetLastError());
            break;
        }
        if (!remote_module) {
            log("%s [4/6] LoadLibraryW returned null inside csgo.exe - the DLL was NOT loaded",
                debug_session::fail_tag);
            explain_load_failure(dll_path);
            break;
        }

        log("%s [4/6] injected: LoadLibraryW succeeded, module base=0x%08lX", debug_session::ok_tag,
            static_cast<unsigned long>(remote_module));
        result = outcome::done;
    } while (false);

    if (thread)
        CloseHandle(thread);
    if (release_remote_path)
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
    return result;
}

// [5/6] LoadLibraryW succeeding only means DllMain returned. The real startup happens on the
// DLL's worker thread and on the first rendered frame; follow its log and give a verdict.
outcome follow_startup(HANDLE process) {
    log("[5/6] waiting for the shell to report its startup (lines marked [shell ...] come from the DLL)");
    const ULONGLONG deadline = GetTickCount64() + 20000;
    while (GetTickCount64() < deadline) {
        pump_log();
        if (session.hooks_failed || session.renderer_ready || session.crash_reported)
            break;
        if (WaitForSingleObject(process, 100) == WAIT_OBJECT_0)
            return outcome::exited;
    }

    if (session.crash_reported) {
        log("%s [5/6] the shell crashed during startup - see the [CRASH] report above",
            debug_session::fail_tag);
    } else if (session.hooks_failed) {
        log("%s [5/6] startup failed: the shell could not hook Direct3D 9, the menu will not appear",
            debug_session::fail_tag);
    } else if (session.renderer_ready) {
        log("%s [5/6] the shell started normally: hooks are installed and the menu renderer is running",
            debug_session::ok_tag);
    } else if (session.hooks_installed) {
        log("%s [5/6] hooks are installed but no frame was rendered in 20 s - the game window is minimized or still loading; still watching",
            debug_session::warn_tag);
    } else if (!session.shell_spoke) {
        log("%s [5/6] the DLL is loaded but wrote nothing in 20 s - it is a Release build (no logging), an old copy, or its worker thread never started",
            debug_session::fail_tag);
    } else {
        log("%s [5/6] the shell did not finish its startup in 20 s - the last [shell ...] line shows where it stopped",
            debug_session::fail_tag);
    }
    return outcome::done;
}

// [6/6] Print the log until csgo.exe exits. Also reports a frozen game window, because a freeze
// produces no exception and no log line by itself.
void monitor(HANDLE process, DWORD process_id) {
    log("[6/6] monitoring csgo.exe - keep this window open; the log continues below");
    bool renderer_seen = session.renderer_ready;
    bool game_ready_seen = false;
    window_search search{process_id, nullptr};
    ULONGLONG next_window_check = 0;
    ULONGLONG frozen_since = 0;
    bool frozen_reported = false;

    for (;;) {
        pump_log();
        if (!renderer_seen && session.renderer_ready) {
            renderer_seen = true;
            log("%s the menu renderer is running now", debug_session::ok_tag);
        }
        if (!game_ready_seen && session.game_ready) {
            game_ready_seen = true;
            log("%s all startup stages passed - the shell is fully working", debug_session::ok_tag);
        }
        if (WaitForSingleObject(process, 100) == WAIT_OBJECT_0)
            return;

        const ULONGLONG now = GetTickCount64();
        if (now < next_window_check)
            continue;
        next_window_check = now + 1000;
        if (!search.window || !IsWindow(search.window)) {
            search.window = nullptr;
            EnumWindows(&find_main_window, reinterpret_cast<LPARAM>(&search));
        }
        const bool frozen = search.window && IsHungAppWindow(search.window);
        if (frozen && !frozen_since) {
            frozen_since = now;
        } else if (frozen && !frozen_reported && now - frozen_since >= 5000) {
            frozen_reported = true;
            log("%s csgo.exe has not processed window messages for 5 s - the game is frozen; the last [shell ...] line above is what ran before the freeze",
                debug_session::warn_tag);
        } else if (!frozen && frozen_since) {
            if (frozen_reported)
                log("%s csgo.exe responds again after %.1f s", debug_session::ok_tag,
                    static_cast<double>(now - frozen_since) / 1000.0);
            frozen_since = 0;
            frozen_reported = false;
        }
    }
}

// csgo.exe is gone: decide whether that was a crash and, if so, keep the session log.
int report_exit(HANDLE process) {
    Sleep(200); // the DLL may still be flushing its crash report
    pump_log();

    DWORD exit_code = 0;
    if (!GetExitCodeProcess(process, &exit_code))
        exit_code = 0xFFFFFFFF;
    const ULONGLONG now = GetTickCount64();
    const bool fatal_code = exit_code >= 0xC0000000u && exit_code != 0xFFFFFFFF;
    const bool recent_exception = session.last_exception_tick &&
        now - session.last_exception_tick < 3000;

    if (!session.crash_reported && !fatal_code && !recent_exception && exit_code == 0) {
        log("%s csgo.exe closed normally (exit code 0) - no crash", debug_session::ok_tag);
        return 0;
    }

    if (session.crash_reported) {
        log("%s csgo.exe crashed, exit code 0x%08lX (%s) - the cause is in the [CRASH] report above",
            debug_session::crash_tag, static_cast<unsigned long>(exit_code), exit_code_meaning(exit_code));
    } else if (fatal_code) {
        log("%s csgo.exe crashed, exit code 0x%08lX (%s)", debug_session::crash_tag,
            static_cast<unsigned long>(exit_code), exit_code_meaning(exit_code));
        log("        the shell's crash handler did not run (the game's own handler took the exception); the last %s block and the last [shell ...] line above show what was happening",
            debug_session::first_chance_tag);
    } else if (recent_exception) {
        log("%s csgo.exe exited with code 0x%08lX right after an exception - most likely a crash handled by the game; see the last %s block above",
            debug_session::crash_tag, static_cast<unsigned long>(exit_code), debug_session::first_chance_tag);
    } else {
        log("%s csgo.exe ended with exit code 0x%08lX (%s) - no exception was logged before it",
            debug_session::warn_tag, static_cast<unsigned long>(exit_code), exit_code_meaning(exit_code));
    }

    // Reuse the file the DLL has already written for this crash, so there is one file per crash
    // and it also contains the injector's lines.
    std::string path = session.crash_log_path;
    if (path.empty()) {
        SYSTEMTIME time{};
        GetLocalTime(&time);
        char generated[MAX_PATH];
        std::snprintf(generated, sizeof(generated), "%s%04u-%02u-%02u_%02u-%02u-%02u.txt",
            debug_session::crash_log_prefix, time.wYear, time.wMonth, time.wDay,
            time.wHour, time.wMinute, time.wSecond);
        path = generated;
    }
    log("%s%s", debug_session::crash_saved, path.c_str());
    ensure_log_directory();
    if (!CopyFileA(debug_session::log_path_a, path.c_str(), FALSE))
        log_failure("saving the crash log", GetLastError());
    return 3;
}

int run(int argc, wchar_t** argv) {
    // The required first line. It is also the first line of the session log.
    write_entry("open csgo");
    log("airflow_shell Debug injector; session log: C:\\dev\\csgo\\Bins\\debug.log");

    if (argc > 2) {
        log("%s usage: airflow_injector.exe [full path to airflow_shell.dll]", debug_session::fail_tag);
        return 2;
    }

    // [1/6] Which DLL, and is it loadable into a 32-bit game at all.
    wchar_t selected_path[MAX_PATH]{};
    if (argc == 2) {
        if (std::wcslen(argv[1]) >= std::size(selected_path)) {
            log("%s [1/6] the DLL path is too long", debug_session::fail_tag);
            return 2;
        }
        std::memcpy(selected_path, argv[1], (std::wcslen(argv[1]) + 1) * sizeof(wchar_t));
    } else if (!default_dll_path(selected_path, static_cast<DWORD>(std::size(selected_path)))) {
        return 1;
    }

    wchar_t dll_path[MAX_PATH]{};
    const DWORD path_length = GetFullPathNameW(selected_path,
        static_cast<DWORD>(std::size(dll_path)), dll_path, nullptr);
    if (!path_length || path_length >= std::size(dll_path)) {
        log_failure("[1/6] GetFullPathNameW", path_length ? ERROR_INSUFFICIENT_BUFFER : GetLastError());
        return 1;
    }
    log("[1/6] checking the DLL");
    if (!check_dll(dll_path))
        return 1;

    // [2/6] + [3/6]
    bool waited = false;
    const DWORD process_id = wait_for_csgo(waited);
    if (!wait_for_game_modules(process_id, waited))
        return 1;

    const HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE | SYNCHRONIZE, FALSE, process_id);
    if (!process) {
        const DWORD error = GetLastError();
        log_failure("[4/6] OpenProcess(csgo.exe)", error, error == ERROR_ACCESS_DENIED ?
            "run the injector as administrator, or an anti-cheat is protecting the process" : nullptr);
        return 1;
    }

    BOOL injector_wow64 = FALSE;
    BOOL target_wow64 = FALSE;
    if (IsWow64Process(GetCurrentProcess(), &injector_wow64) && IsWow64Process(process, &target_wow64) &&
        injector_wow64 && !target_wow64) {
        log("%s [4/6] this csgo.exe is a 64-bit process; the shell and the injector are 32-bit",
            debug_session::fail_tag);
        CloseHandle(process);
        return 1;
    }

    int exit_code = 1;
    outcome step = inject(process, process_id, dll_path);
    if (step == outcome::done)
        step = follow_startup(process);
    if (step == outcome::done || step == outcome::attached) {
        monitor(process, process_id);
        step = outcome::exited;
    }
    if (step == outcome::exited)
        exit_code = report_exit(process);
    else
        log("%s the shell was not loaded; csgo.exe itself keeps running untouched", debug_session::fail_tag);

    CloseHandle(process);
    return exit_code;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    start_session();
    const HANDLE console_event = announce_console();
    const int exit_code = run(argc, argv);
    pump_log();
    if (console_event)
        CloseHandle(console_event);
    wait_before_close();
    return exit_code;
}
