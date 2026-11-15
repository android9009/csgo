#pragma once
#include <windows.h>
#include <cstdio>

namespace PastavarStatus {
constexpr DWORD version = 1;
enum State : LONG { Unknown = 0, Initializing = 1, Ready = 2, Failed = 3, Crashed = 4, Unloaded = 5 };
struct Shared {
    DWORD protocol;
    volatile LONG state;
    char detail[512];
};
inline void MappingName(DWORD pid, wchar_t (&name)[64]) {
    swprintf_s(name, L"Local\\PastavarStatus_%lu", static_cast<unsigned long>(pid));
}
}
