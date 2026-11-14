#include "log.hpp"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
namespace Log {
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
#ifdef _DEBUG
    std::fputs(text, stderr);
#endif
}
}
