#pragma once
#include "skin_icon_path.hpp"
#include <cstdint>
#include <vector>

// Pass the exact VBaseFileSystem interface, not VFileSystem. Uses Source 1's
// IBaseFileSystem virtual interface. Returns encoded PNG bytes; decoding and
// device-resource lifetime belong to the renderer. Call from the render thread.
namespace SkinIconFile {
template <typename T> inline T Slot(void* instance, int index) {
    return reinterpret_cast<T>((*reinterpret_cast<void***>(instance))[index]);
}
inline std::vector<std::uint8_t> Read(void* fs, const std::string& path) {
    if (!fs || path.empty()) return {};
    using Open = void* (__thiscall*)(void*, const char*, const char*, const char*);
    using Size = unsigned (__thiscall*)(void*, void*);
    using Read = int (__thiscall*)(void*, void*, int, void*);
    using Close = void (__thiscall*)(void*, void*);
    void* file = Slot<Open>(fs, 2)(fs, path.c_str(), "rb", "GAME");
    if (!file) return {};
    const unsigned size = Slot<Size>(fs, 7)(fs, file);
    struct FileGuard {
        void* fs; void* file;
        ~FileGuard() { Slot<Close>(fs, 3)(fs, file); }
    } guard{fs, file};
    std::vector<std::uint8_t> bytes;
    // Prevent malformed files from forcing unbounded allocations.
    if (size && size <= 8u * 1024u * 1024u) {
        bytes.resize(size);
        if (Slot<Read>(fs, 0)(fs, bytes.data(), static_cast<int>(size), file) != static_cast<int>(size)) bytes.clear();
    }
    return bytes;
}
inline std::vector<std::uint8_t> ReadIcon(void* fs, std::string_view weapon, std::string_view paint = {}) {
    if (!paint.empty() && paint != "default") {
        auto bytes = Read(fs, SkinIconPath::Generated(weapon, paint));
        if (!bytes.empty()) return bytes;
    }
    return Read(fs, SkinIconPath::Base(weapon));
}
}
