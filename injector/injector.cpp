#include <Windows.h>
#include <TlHelp32.h>

#include <cstdio>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string>

namespace {
void print_error(const char* operation, DWORD error) {
    char* system_message = nullptr;
    const DWORD length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<char*>(&system_message), 0, nullptr);

    std::string message = length && system_message ? system_message : "unknown Windows error";
    if (system_message)
        LocalFree(system_message);
    while (!message.empty() && (message.back() == '\r' || message.back() == '\n'))
        message.pop_back();

    std::fprintf(stderr, "[-] %s failed: %lu (%s)\n", operation,
        static_cast<unsigned long>(error), message.c_str());
}

DWORD find_csgo() {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        print_error("CreateToolhelp32Snapshot", GetLastError());
        return 0;
    }

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
    } else {
        print_error("Process32FirstW", GetLastError());
    }

    CloseHandle(snapshot);
    return process_id;
}

bool default_dll_path(wchar_t* path, DWORD capacity) {
    const DWORD length = GetModuleFileNameW(nullptr, path, capacity);
    if (!length || length >= capacity) {
        print_error("GetModuleFileNameW", length ? ERROR_INSUFFICIENT_BUFFER : GetLastError());
        return false;
    }

    wchar_t* separator = wcsrchr(path, L'\\');
    if (!separator) {
        std::fputs("[-] Cannot determine the injector directory.\n", stderr);
        return false;
    }

    constexpr wchar_t dll_name[] = L"airflow_shell.dll";
    const std::size_t directory_length = static_cast<std::size_t>(separator - path + 1);
    if (directory_length + std::size(dll_name) > capacity) {
        std::fputs("[-] The default DLL path is too long.\n", stderr);
        return false;
    }

    std::memcpy(path + directory_length, dll_name, sizeof(dll_name));
    return true;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    std::puts("airflow_shell Debug injector (Win32)");

    if (argc > 2) {
        std::puts("Usage: airflow_injector.exe [full path to airflow_shell.dll]");
        return 2;
    }

    wchar_t selected_path[MAX_PATH]{};
    if (argc == 2) {
        if (std::wcslen(argv[1]) >= std::size(selected_path)) {
            std::fputs("[-] The DLL path is too long.\n", stderr);
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
        print_error("GetFullPathNameW", path_length ? ERROR_INSUFFICIENT_BUFFER : GetLastError());
        return 1;
    }

    const DWORD attributes = GetFileAttributesW(dll_path);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        std::fputs("[-] airflow_shell.dll was not found. Build Debug | x86 first or pass its path.\n",
            stderr);
        return 1;
    }

    const DWORD process_id = find_csgo();
    if (!process_id) {
        std::fputs("[-] csgo.exe is not running. Start CS:GO, then run the injector again.\n", stderr);
        return 1;
    }
    std::printf("[+] Found csgo.exe, pid=%lu\n", static_cast<unsigned long>(process_id));

    const HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, FALSE, process_id);
    if (!process) {
        print_error("OpenProcess", GetLastError());
        return 1;
    }

    const SIZE_T path_bytes = (std::wcslen(dll_path) + 1) * sizeof(wchar_t);
    void* remote_path = VirtualAllocEx(process, nullptr, path_bytes,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote_path) {
        print_error("VirtualAllocEx", GetLastError());
        CloseHandle(process);
        return 1;
    }

    int result = 1;
    HANDLE thread = nullptr;
    bool release_remote_path = true;
    do {
        SIZE_T written = 0;
        if (!WriteProcessMemory(process, remote_path, dll_path, path_bytes, &written) ||
            written != path_bytes) {
            print_error("WriteProcessMemory", GetLastError());
            break;
        }

        const HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        const FARPROC load_library = kernel32 ? GetProcAddress(kernel32, "LoadLibraryW") : nullptr;
        if (!load_library) {
            print_error("GetProcAddress(LoadLibraryW)", GetLastError());
            break;
        }

        thread = CreateRemoteThread(process, nullptr, 0,
            reinterpret_cast<LPTHREAD_START_ROUTINE>(load_library), remote_path, 0, nullptr);
        if (!thread) {
            print_error("CreateRemoteThread", GetLastError());
            break;
        }

        const DWORD wait_result = WaitForSingleObject(thread, 30000);
        if (wait_result != WAIT_OBJECT_0) {
            if (wait_result == WAIT_TIMEOUT) {
                std::fputs("[-] LoadLibraryW timed out. Remote memory was left allocated for safety.\n",
                    stderr);
                release_remote_path = false;
            } else {
                print_error("WaitForSingleObject", GetLastError());
            }
            break;
        }

        DWORD remote_module = 0;
        if (!GetExitCodeThread(thread, &remote_module)) {
            print_error("GetExitCodeThread", GetLastError());
            break;
        }
        if (!remote_module) {
            std::fputs("[-] LoadLibraryW returned null. Check that the DLL is Win32 and its dependencies are available.\n",
                stderr);
            break;
        }

        std::printf("[+] Injected successfully, module base=0x%08lX\n",
            static_cast<unsigned long>(remote_module));
        std::puts("[+] Debug log: C:\\dev\\csgo\\Bins\\debug.log");
        std::puts("[+] Crash dump: C:\\dev\\csgo\\Bins\\crash_last.dmp.txt");
        result = 0;

    } while (false);

    if (thread)
        CloseHandle(thread);
    if (release_remote_path)
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
    CloseHandle(process);
    return result;
}
