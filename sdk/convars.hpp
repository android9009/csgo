#pragma once
// Minimal ConVar access: read and write client cvars (sv_maxunlag, cl_interp,
// cl_interp_ratio, cl_updaterate for the ping layer; the chase camera pair plus
// cam_idealdist and the viewmodel offsets for the visuals misc block).
//
// Writes go through the ConVar's own virtual `SetValue(float)` (slot 15, the index this
// repository's `main` branch uses in structs.hpp: c_convar::set_value), never through a raw
// field write: SetValue refreshes the cached string, fires the change callbacks and applies
// the cvar's own clamping, which is exactly what the game's consumers read. No console
// command is ever issued.
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

// ConVar::SetValue(float) / SetValue(int) / SetValue(const char*) - slots 15 / 16 / 14,
// again the layout `main` uses. An exception (a wrong slot on a different build) is reported
// as a failed write and never reaches the caller.
using SetFloatFn = void (__thiscall*)(ConVar*, float);
using SetIntFn = void (__thiscall*)(ConVar*, int);
using SetStringFn = void (__thiscall*)(ConVar*, const char*);

inline bool SetFloat(ConVar* var, float value) {
    if (!var || !var->vtable_) return false;
    __try {
        const auto fn = reinterpret_cast<SetFloatFn>(
            (static_cast<void**>(var->vtable_))[15]);
        if (!fn) return false;
        fn(var, value);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

inline bool SetInt(ConVar* var, int value) {
    if (!var || !var->vtable_) return false;
    __try {
        const auto fn = reinterpret_cast<SetIntFn>((static_cast<void**>(var->vtable_))[16]);
        if (!fn) return false;
        fn(var, value);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

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

// A cvar captured for the session: the value to hand back on unload, plus whether the
// capture actually happened (a missing cvar must not turn into a write of zero).
struct SavedVar {
    ConVar* var{};
    float value{};
    bool valid{};

    bool Capture(const char* name) {
        var = Find(name);
        if (!var) return false;
        value = var->GetFloat();
        valid = true;
        return true;
    }

    bool Restore() {
        if (!valid || !var) return false;
        const bool ok = SetFloat(var, value);
        valid = false;
        return ok;
    }
};


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
