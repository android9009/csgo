#pragma once
// Minimal ConVar access: enough to read a few client cvars whose values the misc runtime
// needs (sv_maxunlag, cl_interp, cl_interp_ratio, cl_updaterate).
//
// The layout, the ICvar slot and the value obfuscation are ported from this repository's
// `main` branch (`structs.hpp` / `interfaces.hpp` / `convars.cpp`): CS:GO stores the
// numeric value XORed with the ConVar's own pointer, and aliased cvars keep the real
// storage on their `parent`. Every read is range-checked by the caller, because a wrong
// slot or a build without the obfuscation would otherwise look like a plausible number.
#include <cstdint>
#include <cstring>
#include <windows.h>

#include "interfaces/interfaces.hpp"

namespace ConVars {
class ConVar {
public:
    // PAD(4) is the vtable pointer.
    void* vtable_{};
    ConVar* next{};
    bool registered{};
    const char* name{};
    const char* help_string{};
    int flags{};
    void* unused_{};
    ConVar* parent{};
    const char* default_value{};
    char* string{};
    int string_length{};
    float float_value{};
    int int_value{};
    bool has_min{};
    float min_value{};
    bool has_max{};
    float max_value{};

    const char* Name() const { return name; }

    // CS:GO obfuscates the numeric values with the object pointer (`main`: c_convar).
    float GetFloat() const {
        const ConVar* storage = parent ? parent : this;
        const std::uint32_t bits = *reinterpret_cast<const std::uint32_t*>(&storage->float_value) ^
                                   static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(this));
        float value = 0.f;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    int GetInt() const {
        const ConVar* storage = parent ? parent : this;
        return storage->int_value ^ static_cast<int>(reinterpret_cast<std::uintptr_t>(this));
    }
};

// IConVar::FindVar - slot 16 in the published ICvar layout (ported from `main`).
inline ConVar* Find(const char* name) {
    if (!name || !Interfaces::ConVar) return nullptr;
    using Fn = ConVar* (__thiscall*)(void*, const char*);
    auto fn = reinterpret_cast<Fn>((*reinterpret_cast<void***>(Interfaces::ConVar))[16]);
    if (!fn) return nullptr;
    ConVar* var = nullptr;
    __try {
        var = fn(Interfaces::ConVar, name);
        // A wrong slot returns something that is not a ConVar: refuse anything whose
        // name does not match the request byte for byte.
        if (var && (!var->name || std::strcmp(var->name, name) != 0)) var = nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        var = nullptr;
    }
    return var;
}

// True when the cvar exists and its name round-trips, i.e. the ICvar slot is right.
inline bool Exists(const char* name) { return Find(name) != nullptr; }

// Reads a float cvar and keeps the caller's fallback unless the value is finite and
// inside [low, high]. Both bounds are what makes a misresolved ConVar harmless.
inline float Float(const char* name, float low, float high, float fallback) {
    const ConVar* var = Find(name);
    if (!var) return fallback;
    const float value = var->GetFloat();
    if (!(value >= low && value <= high)) return fallback;
    return value;
}

// Same for an integer cvar.
inline int Int(const char* name, int low, int high, int fallback) {
    const ConVar* var = Find(name);
    if (!var) return fallback;
    const int value = var->GetInt();
    if (value < low || value > high) return fallback;
    return value;
}
} // namespace ConVars
