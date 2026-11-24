#pragma once
// IMaterial / IMaterialVar wrappers (Source 1 material system).
// Vtable indices follow the legacy sdk/game/structs.hpp i_material layout,
// cross-checked against the chams implementation (alpha=27, color=28,
// set_flag=29, get_flag=30, find_var=11, increment_ref=12).
// Verified against CMaterial's 108-slot vtable in the supplied Legacy materialsystem.dll
// (slot 61 is anchored by the GetNumAnimationFrames string; 42 compares this against the
// error material). See docs/interface-verification.md.
#include <cstddef>
#include <cstdint>

enum MaterialVarFlags : int {
    MATERIAL_VAR_DEBUG = 1 << 0,
    MATERIAL_VAR_NO_DEBUG_OVERRIDE = 1 << 1,
    // The smoke removal toggles this on the vistasmoke sprite materials. The value is
    // from the legacy sdk/game/structs.hpp material_flags_t (main's defines.hpp), the
    // same layout the vtable slots above follow - NOT the public Source SDK enum.
    MATERIAL_VAR_NO_DRAW = 1 << 2,
    MATERIAL_VAR_USE_IN_FILLRATE_MODE = 1 << 3,
    MATERIAL_VAR_VERTEXCOLOR = 1 << 4,
    MATERIAL_VAR_VERTEXALPHA = 1 << 5,
    MATERIAL_VAR_SELFILLUM = 1 << 6,
    MATERIAL_VAR_ADDITIVE = 1 << 7,
    MATERIAL_VAR_ALPHATEST = 1 << 8,
    MATERIAL_VAR_MULTIPASS = 1 << 9,
    MATERIAL_VAR_ZNEARER = 1 << 10,
    MATERIAL_VAR_MODEL = 1 << 11,
    MATERIAL_VAR_FLAT = 1 << 12,
    MATERIAL_VAR_NOCULL = 1 << 13,
    MATERIAL_VAR_NOFOG = 1 << 14,
    MATERIAL_VAR_IGNOREZ = 1 << 15,
    MATERIAL_VAR_DECAL = 1 << 16,
    MATERIAL_VAR_ENVMAPSPHERE = 1 << 17,
    MATERIAL_VAR_NOALPHAMOD = 1 << 18,
    MATERIAL_VAR_ENVMAPCAMERASPACE = 1 << 19,
    MATERIAL_VAR_BASEALPHAENVMAPMASK = 1 << 20,
    MATERIAL_VAR_TRANSLUCENT = 1 << 21,
    MATERIAL_VAR_NORMALMAPALPHAENVMAPMASK = 1 << 22,
    MATERIAL_VAR_NEEDS_SOFTWARE_SKINNING = 1 << 23,
    MATERIAL_VAR_OPAQUETEXTURE = 1 << 24,
    MATERIAL_VAR_ENVMAPMODE = 1 << 25,
    MATERIAL_VAR_SUPPRESS_DECALS = 1 << 26,
    MATERIAL_VAR_HALFLAMBERT = 1 << 27,
    MATERIAL_VAR_WIREFRAME = 1 << 28,
    MATERIAL_VAR_ALLOWALPHATOCOVERAGE = 1 << 29,
    MATERIAL_VAR_IGNORE_ALPHA_MODULATION = 1 << 30,
    MATERIAL_VAR_VERTEXFOG = 1 << 31,
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
    // CS:GO x86 IMaterialVar::SetVecValue (Fatality IMaterialVar.h slot 11).
    void SetVecValue(float x,float y,float z) {using Fn=void(__thiscall*)(void*,float,float,float);if(auto fn=VFunc<Fn>(11))fn(this,x,y,z);}
    void SetValue(float x,float y,float z){SetVecValue(x,y,z);}
};

class IMaterial {
protected:
    template <typename Fn> Fn VFunc(std::size_t index) const {
        return reinterpret_cast<Fn>((*reinterpret_cast<void***>(const_cast<IMaterial*>(this)))[index]);
    }
public:
    const char* GetName() const { using Fn = const char* (__thiscall*)(void*); auto fn = VFunc<Fn>(0); return fn ? fn(const_cast<IMaterial*>(this)) : ""; }
    // CS:GO x86 IMaterial::GetShaderName, reference declaration slot 49.
    const char* GetShaderName() const {using Fn=const char*(__thiscall*)(void*);auto fn=VFunc<Fn>(49);return fn ? fn(const_cast<IMaterial*>(this)) : "";}
    const char* GetTextureGroupName() const { using Fn = const char* (__thiscall*)(void*); auto fn = VFunc<Fn>(1); return fn ? fn(const_cast<IMaterial*>(this)) : ""; }
    IMaterialVar* FindVar(const char* name, bool* found, bool complain = true) {
        using Fn = IMaterialVar* (__thiscall*)(void*, const char*, bool*, bool);
        auto fn = VFunc<Fn>(11);
        return fn ? fn(this, name, found, complain) : nullptr;
    }
    void IncrementReferenceCount() { using Fn = void (__thiscall*)(void*); if (auto fn = VFunc<Fn>(12)) fn(this); }
    void DecrementReferenceCount() { using Fn = void (__thiscall*)(void*); if (auto fn = VFunc<Fn>(13)) fn(this); }
    bool IsErrorMaterial() const { using Fn = bool (__thiscall*)(void*); auto fn = VFunc<Fn>(42); return fn && fn(const_cast<IMaterial*>(this)); }
    float GetAlphaModulation() const { using Fn = float (__thiscall*)(void*); auto fn = VFunc<Fn>(44); return fn ? fn(const_cast<IMaterial*>(this)) : 1.f; }
    void GetColorModulation(float* r, float* g, float* b) const { using Fn = void (__thiscall*)(void*, float*, float*, float*); if(auto fn=VFunc<Fn>(45)) fn(const_cast<IMaterial*>(this),r,g,b); }
    void AlphaModulate(float alpha) { using Fn = void (__thiscall*)(void*, float); if (auto fn = VFunc<Fn>(27)) fn(this, alpha); }
    void ColorModulate(float r, float g, float b) { using Fn = void (__thiscall*)(void*, float, float, float); if (auto fn = VFunc<Fn>(28)) fn(this, r, g, b); }
    void SetMaterialVarFlag(int flag, bool on) { using Fn = void (__thiscall*)(void*, int, bool); if (auto fn = VFunc<Fn>(29)) fn(this, flag, on); }
    bool GetMaterialVarFlag(int flag) const { using Fn = bool (__thiscall*)(void*, int); auto fn = VFunc<Fn>(30); return fn ? fn(const_cast<IMaterial*>(this), flag) : false; }
};
