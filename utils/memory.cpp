#include "memory.hpp"
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace MEM {
static std::vector<int> ParsePattern(std::string_view pattern) {
    std::vector<int> bytes;
    std::istringstream stream{std::string(pattern)};
    std::string token;
    while (stream >> token) {
        if (token == "?" || token == "??") bytes.push_back(-1);
        else {
            try { bytes.push_back(std::stoi(token, nullptr, 16) & 0xFF); }
            catch (...) { return {}; }
        }
    }
    return bytes;
}
std::uintptr_t FindPattern(const std::uint8_t* start, std::uintptr_t size, std::string_view pattern) {
    if (!start || !size) return 0;
    const auto bytes = ParsePattern(pattern);
    if (bytes.empty() || size < bytes.size()) return 0;
    for (std::uintptr_t i = 0; i + bytes.size() <= size; ++i) {
        bool matched = true;
        for (size_t j = 0; j < bytes.size(); ++j) {
            if (bytes[j] != -1 && start[i + j] != static_cast<std::uint8_t>(bytes[j])) {
                matched = false; break;
            }
        }
        if (matched) return reinterpret_cast<std::uintptr_t>(start + i);
    }
    return 0;
}
void* GetModuleBase(std::string_view module) {
    if (module.empty()) return GetModuleHandleA(nullptr);
    const std::string name(module);
    return GetModuleHandleA(name.c_str());
}
void* GetExport(void* module_base, std::string_view name) {
    if (!module_base) return nullptr;
    const std::string symbol(name);
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(module_base), symbol.c_str()));
}
std::uintptr_t FindPattern(std::string_view module, std::string_view pattern) {
    auto* base = static_cast<std::uint8_t*>(GetModuleBase(module));
    if (!base) return 0;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
    // Restrict scanning to executable sections to avoid unrelated data-page matches.
    auto* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        const auto length = static_cast<std::uintptr_t>(section->Misc.VirtualSize);
        const auto found = FindPattern(base + section->VirtualAddress, length, pattern);
        if (found) return found;
    }
    return 0;
}
std::uintptr_t GetAbsolute(std::uintptr_t relative) {
    if (!relative) return 0;
    std::int32_t displacement = 0;
    std::memcpy(&displacement, reinterpret_cast<const void*>(relative), sizeof(displacement));
    return relative + sizeof(displacement) + displacement;
}
std::uintptr_t ResolveRelative(std::uintptr_t address, std::uintptr_t offset) {
    return GetAbsolute(address + offset);
}
bool GetSection(std::uintptr_t base, std::string_view section_name,
                std::uintptr_t* out_start, std::uintptr_t* out_size) {
    if (!base || !out_start || !out_size) return false;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    auto* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        char name[9]{}; std::memcpy(name, section->Name, 8);
        if (section_name == name) {
            *out_start = base + section->VirtualAddress;
            *out_size = section->Misc.VirtualSize;
            return true;
        }
    }
    return false;
}
bool ReadPointer(std::uintptr_t address, std::uintptr_t& value) {
    if (!address) return false;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    return value != 0;
}
}
