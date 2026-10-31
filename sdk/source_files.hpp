#pragma once

#include <Windows.h>

#include <cstdio>

// The small IBaseFileSystem subset used by Legendware's skin preview loader.
// It reads files from the game's search paths, including files packed in VPKs.
namespace source_files {
class IBaseFileSystem {
public:
    int Read(void* output, int size, void* file) {
        using function_t = int(__thiscall*)(void*, void*, int, void*);
        const auto table = *reinterpret_cast<void***>(this);
        return reinterpret_cast<function_t>(table[0])(this, output, size, file);
    }

    void* Open(const char* file_name, const char* options, const char* path_id = nullptr) {
        using function_t = void*(__thiscall*)(void*, const char*, const char*, const char*);
        const auto table = *reinterpret_cast<void***>(this);
        return reinterpret_cast<function_t>(table[2])(this, file_name, options, path_id);
    }

    void Close(void* file) {
        using function_t = void(__thiscall*)(void*, void*);
        const auto table = *reinterpret_cast<void***>(this);
        reinterpret_cast<function_t>(table[3])(this, file);
    }

    unsigned int Size(void* file) {
        using function_t = unsigned int(__thiscall*)(void*, void*);
        const auto table = *reinterpret_cast<void***>(this);
        return reinterpret_cast<function_t>(table[7])(this, file);
    }
};

inline IBaseFileSystem* base_file_system() {
    static IBaseFileSystem* cached = nullptr;
    if (cached)
        return cached;

    const auto module = GetModuleHandleA("filesystem_stdio.dll");
    if (!module)
        return nullptr;

    using create_interface_t = void*(__cdecl*)(const char*, int*);
    const auto create_interface = reinterpret_cast<create_interface_t>(
        GetProcAddress(module, "CreateInterface"));
    if (!create_interface)
        return nullptr;

    // Legendware enumerates the same interface registry by the VBaseFileSystem prefix.
    // Asking CreateInterface directly avoids depending on its module-specific prologue.
    for (int version = 32; version >= 1; --version) {
        char name[64]{};
        std::snprintf(name, sizeof(name), "VBaseFileSystem%03d", version);
        if (const auto instance = static_cast<IBaseFileSystem*>(create_interface(name, nullptr))) {
            cached = instance;
            return cached;
        }
    }

    cached = static_cast<IBaseFileSystem*>(create_interface("VBaseFileSystem", nullptr));
    return cached;
}
} // namespace source_files
