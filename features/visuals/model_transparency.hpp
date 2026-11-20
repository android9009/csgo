#pragma once
// Prop / texture transparency.
//
// How Fatality does it (reference: reversed2603/fatality_source dee7327,
// hooks/draw_model_execute.cpp `shader_api_draw_mesh` and hooks/scene_end.cpp
// `add_renderables_to_render_lists`): the leaf system is forced to queue studio
// props into the translucent render list (`RenderableInfo_t::m_nTranslucencyType`),
// and the per-mesh alpha is written straight into
// `MeshInstanceData_t::m_DiffuseModulation.w` inside CShaderAPIDx8::DrawMesh.
//
// Both of those entry points are outside what the supplied binaries let us verify:
// DrawMesh lives in shaderapidx9.dll (not supplied) and the leaf-system function has
// no portable signature we can prove. This file therefore reaches the same two
// effects through the material system, using only slots verified against the
// supplied binaries (docs/interface-verification.md):
//   IMaterialSystem::FindMaterial                        slot 84
//   IMaterial::FindVar 11, AlphaModulate 27,
//              SetMaterialVarFlag 29, GetMaterialVarFlag 30,
//              IsErrorMaterial 42, GetAlphaModulation 44
//
// The equivalence holds because the engine asks the material for translucency when
// it builds the render lists (IModelInfo::ComputeTranslucencyType ->
// IMaterial::IsTranslucent), so writing MATERIAL_VAR_TRANSLUCENT before
// FRAME_RENDER_START puts the prop on the translucent list the same way Fatality's
// leaf hook does, and `$alpha` supplies the amount that DrawMesh would have written
// per instance.
#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include "../../sdk/interfaces/interfaces.hpp"
#include "../../sdk/material.hpp"
#include "../../sdk/studiohdr.hpp"
#include "model_transparency_rules.hpp"

namespace ModelTransparency {
// State captured the first time a material is touched, so a missed restore cannot
// make the value drift across frames.
struct Original {
    float alpha = 1.f;
    bool translucent = false;
};
inline std::unordered_map<IMaterial*, Original>& Originals() {
    static std::unordered_map<IMaterial*, Original> originals;
    return originals;
}

// One entry per model that was actually drawn, with the materials resolved from its
// studio header texture list (the texture name is the material name inside the
// "Model textures" group).
struct Model {
    std::vector<IMaterial*> materials;
    Target target = Target::None;
    std::uint64_t seen = 0;
};
inline std::unordered_map<const studiohdr_t*, Model>& Models() {
    static std::unordered_map<const studiohdr_t*, Model> models;
    return models;
}
inline std::uint64_t& Clock() {
    static std::uint64_t clock = 0;
    return clock;
}
// Materials currently held in the translucent state by Begin, restored by End.
inline std::array<IMaterial*, kMaxMaterials * 8>& Applied() {
    static std::array<IMaterial*, kMaxMaterials * 8> applied{};
    return applied;
}
inline int& AppliedCount() {
    static int count = 0;
    return count;
}

inline IMaterial* MaterialFor(const mstudiotexture_t* texture) {
    if (!texture || !texture->name[0] || !Interfaces::MaterialSystem) return nullptr;
    auto* material = Interfaces::MaterialSystem->FindMaterial(texture->name, "Model textures", false, nullptr);
    return material && !material->IsErrorMaterial() ? material : nullptr;
}

inline Model& Record(const studiohdr_t* header, Target target) {
    auto& models = Models();
    auto it = models.find(header);
    if (it == models.end()) {
        Model model;
        model.target = target;
        if (Interfaces::MaterialSystem && header->textures > 0 && header->textures <= 128 && header->texture_index > 0) {
            for (int i = 0; i < header->textures; ++i) {
                auto* material = MaterialFor(header->texture(i));
                if (!material) continue;
                bool seen = false;
                for (auto* known : model.materials) seen = seen || known == material;
                if (!seen) model.materials.push_back(material);
            }
        }
        it = models.emplace(header, std::move(model)).first;
    }
    it->second.target = target;
    it->second.seen = Clock();
    return it->second;
}

// Called for every DrawModelExecute: remember which models are props/characters and
// which materials belong to them. Cheap, and does nothing while both sliders are 0.
inline void Note(const studiohdr_t* header, Target target) {
    if (!header || target == Target::None) return;
    Record(header, target);
}

// Drop models that were not drawn for a while (map change is detected separately).
inline void Prune() {
    auto& models = Models();
    if (models.size() < 64) return;
    const auto now = Clock();
    for (auto it = models.begin(); it != models.end();) {
        if (now - it->second.seen > 600) it = models.erase(it);
        else ++it;
    }
}

// FRAME_RENDER_START: hold the model's own materials at the requested alpha so the
// engine queues them as translucent for this frame.
inline void Begin(float texture_percent, float prop_percent) {
    auto& applied = Applied();
    auto& count = AppliedCount();
    auto& originals = Originals();
    count = 0;
    if ((!(texture_percent > 0.f) && !(prop_percent > 0.f)) || !Interfaces::MaterialSystem) return;
    ++Clock();
    for (auto& [header, model] : Models()) {
        const float alpha = AlphaFor(PercentFor(model.target, texture_percent, prop_percent));
        if (alpha >= 1.f) continue;
        for (auto* material : model.materials) {
            if (!material || count >= static_cast<int>(applied.size())) continue;
            if (originals.find(material) == originals.end())
                originals.emplace(material, Original{material->GetAlphaModulation(), material->GetMaterialVarFlag(MATERIAL_VAR_TRANSLUCENT)});
            const auto& original = originals[material];
            material->AlphaModulate(alpha);
            if (!original.translucent) material->SetMaterialVarFlag(MATERIAL_VAR_TRANSLUCENT, true);
            applied[count++] = material;
        }
    }
}

// FRAME_RENDER_END: put every touched material back.
inline void End() {
    auto& applied = Applied();
    auto& count = AppliedCount();
    auto& originals = Originals();
    for (int i = 0; i < count; ++i) {
        auto* material = applied[i];
        if (!material) continue;
        const auto it = originals.find(material);
        if (it == originals.end()) continue;
        material->AlphaModulate(it->second.alpha);
        material->SetMaterialVarFlag(MATERIAL_VAR_TRANSLUCENT, it->second.translucent);
    }
    count = 0;
}

// Map change: forget everything. Never dereferences the stale keys.
inline void Purge() {
    Models().clear();
    Originals().clear();
    AppliedCount() = 0;
}
}
