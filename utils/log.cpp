#include "log.hpp"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
namespace Log {
static void WriteFile(const char* text) {
    char path[MAX_PATH]{};
    DWORD length = GetTempPathA(MAX_PATH, path);
    if (!length || length >= MAX_PATH - 16) return;
    std::strncat(path, "csgo_base.log", MAX_PATH - std::strlen(path) - 1);
    FILE* file = nullptr;
    if (fopen_s(&file, path, "a") != 0 || !file) return;
    std::fputs(text, file);
    std::fclose(file);
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
