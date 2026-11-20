#pragma once
// Prop / texture transparency.
//
// The model's *own* materials are modulated for the duration of one
// DrawModelExecute call, so a prop keeps its textures and simply blends. No
// material override is involved, which is why this composes with chams: when a
// subject has chams the chams pass runs first and this one is skipped.
//
// Only slots verified against the supplied binaries are used
// (see docs/interface-verification.md):
//   IMaterialSystem::FindMaterial                        slot 84
//   IMaterial::FindVar 11, AlphaModulate 27,
//              SetMaterialVarFlag 29, GetMaterialVarFlag 30,
//              IsErrorMaterial 42, GetAlphaModulation 44
#include <array>
#include <cstddef>
#include <unordered_map>
#include "../../sdk/interfaces/interfaces.hpp"
#include "../../sdk/material.hpp"
#include "../../sdk/studiohdr.hpp"
#include "model_transparency_rules.hpp"

namespace ModelTransparency {
// State captured the first time a material is touched, so a missed restore (an
// exception between the draw and the restore) cannot make the value drift.
struct Original {
    float alpha = 1.f;
    bool translucent = false;
};
inline std::unordered_map<IMaterial*, Original>& Originals() {
    static std::unordered_map<IMaterial*, Original> originals;
    return originals;
}

// Resolve the model's materials through the studio header texture list; the
// texture name is the material name inside the "Model textures" group.
inline IMaterial* MaterialFor(const mstudiotexture_t* texture) {
    if (!texture || !texture->name[0] || !Interfaces::MaterialSystem) return nullptr;
    auto* material = Interfaces::MaterialSystem->FindMaterial(texture->name, "Model textures", false, nullptr);
    return material && !material->IsErrorMaterial() ? material : nullptr;
}

// Modulate every material of the model. Returns the number touched; the touched
// list is what Restore needs (it never outlives the draw).
inline int Apply(const studiohdr_t* header, float percent, std::array<IMaterial*, kMaxMaterials>& touched) {
    if (!header || !Interfaces::MaterialSystem) return 0;
    const float alpha = AlphaFor(percent);
    if (alpha >= 1.f) return 0;
    if (header->textures <= 0 || header->textures > 128 || header->texture_index <= 0) return 0;
    int count = 0;
    for (int i = 0; i < header->textures && count < kMaxMaterials; ++i) {
        auto* material = MaterialFor(header->texture(i));
        if (!material) continue;
        bool seen = false;
        for (int j = 0; j < count; ++j) seen = seen || touched[j] == material;
        if (seen) continue;
        auto& originals = Originals();
        if (originals.find(material) == originals.end())
            originals.emplace(material, Original{material->GetAlphaModulation(), material->GetMaterialVarFlag(MATERIAL_VAR_TRANSLUCENT)});
        const auto& original = originals[material];
        material->AlphaModulate(alpha);
        if (!original.translucent) material->SetMaterialVarFlag(MATERIAL_VAR_TRANSLUCENT, true);
        touched[count++] = material;
    }
    return count;
}

// Put the captured values back. Safe to call with count == 0.
inline void Restore(const std::array<IMaterial*, kMaxMaterials>& touched, int count) {
    auto& originals = Originals();
    for (int i = 0; i < count; ++i) {
        auto* material = touched[i];
        if (!material) continue;
        const auto it = originals.find(material);
        if (it == originals.end()) continue;
        material->AlphaModulate(it->second.alpha);
        material->SetMaterialVarFlag(MATERIAL_VAR_TRANSLUCENT, it->second.translucent);
    }
}

// Draw with the model's materials modulated, then restore. Returns false when the
// caller should draw normally instead (feature off or no material resolved).
template <typename Fn>
inline bool Draw(const studiohdr_t* header, float percent, Fn&& draw) {
    std::array<IMaterial*, kMaxMaterials> touched{};
    const int count = Apply(header, percent, touched);
    if (!count) return false;
    draw();
    Restore(touched, count);
    return true;
}

// Drop captured state (map change). Never dereferences the stale keys.
inline void Purge() { Originals().clear(); }
}
