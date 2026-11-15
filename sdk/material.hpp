#pragma once
// IMaterial / IMaterialVar wrappers (Source 1 material system).
// Vtable indices follow the legacy sdk/game/structs.hpp i_material layout,
// cross-checked against the chams implementation (alpha=27, color=28,
// set_flag=29, get_flag=30, find_var=11, increment_ref=12).
#include <cstddef>
#include <cstdint>

enum MaterialVarFlags : int {
    MATERIAL_VAR_ADDITIVE = 1 << 7,
    MATERIAL_VAR_NOFOG = 1 << 14,
    MATERIAL_VAR_IGNOREZ = 1 << 15,
    MATERIAL_VAR_TRANSLUCENT = 1 << 21,
};

class IMaterialVar {
protected:
    template <typename Fn> Fn VFunc(std::size_t index) const {
        return reinterpret_cast<Fn>((*reinterpret_cast<void***>(const_cast<IMaterialVar*>(this)))[index]);
    }
public:
    void SetValue(float value) { using Fn = void (__thiscall*)(void*, float); if (auto fn = VFunc<Fn>(4)) fn(const_cast<IMaterialVar*>(this), value); }
    void SetValue(int value) { using Fn = void (__thiscall*)(void*, int); if (auto fn = VFunc<Fn>(5)) fn(const_cast<IMaterialVar*>(this), value); }
    void SetValue(const char* value) { using Fn = void (__thiscall*)(void*, const char*); if (auto fn = VFunc<Fn>(6)) fn(const_cast<IMaterialVar*>(this), value); }
    void SetValue(float x, float y, float z) { using Fn = void (__thiscall*)(void*, float, float, float); if (auto fn = VFunc<Fn>(11)) fn(const_cast<IMaterialVar*>(this), x, y, z); }
};

class IMaterial {
protected:
    template <typename Fn> Fn VFunc(std::size_t index) const {
        return reinterpret_cast<Fn>((*reinterpret_cast<void***>(const_cast<IMaterial*>(this)))[index]);
    }
public:
    const char* GetName() const { using Fn = const char* (__thiscall*)(void*); auto fn = VFunc<Fn>(0); return fn ? fn(const_cast<IMaterial*>(this)) : ""; }
    const char* GetTextureGroupName() const { using Fn = const char* (__thiscall*)(void*); auto fn = VFunc<Fn>(1); return fn ? fn(const_cast<IMaterial*>(this)) : ""; }
    IMaterialVar* FindVar(const char* name, bool* found, bool complain = true) {
        using Fn = IMaterialVar* (__thiscall*)(void*, const char*, bool*, bool);
        auto fn = VFunc<Fn>(11);
        return fn ? fn(this, name, found, complain) : nullptr;
    }
    void IncrementReferenceCount() { using Fn = void (__thiscall*)(void*); if (auto fn = VFunc<Fn>(12)) fn(this); }
    void DecrementReferenceCount() { using Fn = void (__thiscall*)(void*); if (auto fn = VFunc<Fn>(13)) fn(this); }
    void AlphaModulate(float alpha) { using Fn = void (__thiscall*)(void*, float); if (auto fn = VFunc<Fn>(27)) fn(this, alpha); }
    void ColorModulate(float r, float g, float b) { using Fn = void (__thiscall*)(void*, float, float, float); if (auto fn = VFunc<Fn>(28)) fn(this, r, g, b); }
    void SetMaterialVarFlag(int flag, bool on) { using Fn = void (__thiscall*)(void*, int, bool); if (auto fn = VFunc<Fn>(29)) fn(this, flag, on); }
    bool GetMaterialVarFlag(int flag) const { using Fn = bool (__thiscall*)(void*, int); auto fn = VFunc<Fn>(30); return fn ? fn(const_cast<IMaterial*>(this), flag) : false; }
};
