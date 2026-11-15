#include "log.hpp"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
namespace Log {
static void WriteFile(const char* text) {
    // The injector loads its DLL from the same directory as pastavar_debug.log.
    // Resolve the DLL module, not the host executable (csgo.exe).
    static const std::wstring path = [] {
        HMODULE module = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&WriteFile), &module)) return std::wstring{};
        std::vector<wchar_t> filename(32768);
        const DWORD length = GetModuleFileNameW(module, filename.data(), static_cast<DWORD>(filename.size()));
        if (!length || length >= filename.size()) return std::wstring{};
        std::wstring result(filename.data(), length);
        const auto separator = result.find_last_of(L"\\/");
        if (separator == std::wstring::npos) return std::wstring{};
        result.resize(separator + 1);
        return result + L"csgo_base.log";
    }();
    if (path.empty()) {
        OutputDebugStringA("[csgo-base] Cannot determine DLL directory for csgo_base.log\n");
        return;
    }
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        OutputDebugStringA("[csgo-base] Cannot open csgo_base.log beside DLL\n");
        return;
    }
    DWORD written = 0;
    const auto length = static_cast<DWORD>(std::strlen(text));
    if (!::WriteFile(file, text, length, &written, nullptr) || written != length)
        OutputDebugStringA("[csgo-base] Failed to write csgo_base.log\n");
    CloseHandle(file);
}
void Print(Level level, const char* format, ...) {
    char text[1024]{};
    const char* tag = level == Level::Error ? "[error] " : level == Level::Warning ? "[warn] " : "[info] ";
    int prefix = std::snprintf(text, sizeof(text), "[csgo-base] %s", tag);
    va_list args; va_start(args, format);
    std::vsnprintf(text + prefix, sizeof(text) - static_cast<size_t>(prefix), format, args);
    va_end(args);
    const size_t len = std::strlen(text);
    if (len + 2 < sizeof(text)) { text[len] = '\n'; text[len + 1] = '\0'; }
    OutputDebugStringA(text);
    WriteFile(text);
#ifdef _DEBUG
    std::fputs(text, stderr);
#endif
}
}
