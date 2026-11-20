#pragma once
// Prop / texture transparency.
//
// Fatality (reversed2603/fatality_source dee7327) does this in two hooks: it forces
// RenderableInfo_t::m_nTranslucencyType in CClientLeafSystem::AddRenderablesToRender-
// Lists so studio props are queued on the translucent render list, and it writes the
// alpha straight into MeshInstanceData_t::m_DiffuseModulation.w inside
// CShaderAPIDx8::DrawMesh. Both entry points are reachable but neither is needed to
// get the same picture, and the second one needs a MeshInstanceData_t layout we
// cannot prove from the supplied module, so this file drives the material system
// instead, using only slots verified against the supplied binaries:
//   IMaterialSystem::FindMaterial                        slot 84
//   IMaterial::FindVar 11, AlphaModulate 27,
//              SetMaterialVarFlag 29, GetMaterialVarFlag 30,
//              IsErrorMaterial 42, GetAlphaModulation 44
//
// The engine asks the *material* whether it is translucent while it builds the render
// lists (IModelInfo::ComputeTranslucencyType -> IMaterial::IsTranslucent), so holding
// $alpha and MATERIAL_VAR_TRANSLUCENT on the model's own materials puts props on the
// translucent list exactly where Fatality's leaf hook does.
//
// Values are written only when they change, not every frame: flipping the translucent
// flag makes the engine recompute the material's state snapshot, so doing it once per
// slider change instead of once per frame keeps the cost off the render thread's back.
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
// What a material looked like before we touched it, and what we last wrote to it.
struct Ledger {
    float original_alpha = 1.f;
    bool original_translucent = false;
    float written_alpha = 1.f;
    bool written_translucent = false;
    bool touched = false;
};
inline std::unordered_map<IMaterial*, Ledger>& Ledgers() {
    static std::unordered_map<IMaterial*, Ledger> ledgers;
    return ledgers;
}

// One entry per model that was drawn, with the materials resolved from its studio
// header texture list (the texture name is the material name inside the
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

// Hold one material at the requested state; writes only on change.
inline void Apply(IMaterial* material, float alpha) {
    auto& ledgers = Ledgers();
    auto it = ledgers.find(material);
    if (it == ledgers.end()) {
        Ledger fresh;
        fresh.original_alpha = material->GetAlphaModulation();
        fresh.original_translucent = material->GetMaterialVarFlag(MATERIAL_VAR_TRANSLUCENT);
        fresh.written_alpha = fresh.original_alpha;
        fresh.written_translucent = fresh.original_translucent;
        it = ledgers.emplace(material, fresh).first;
    }
    auto& ledger = it->second;
    ledger.touched = true;
    const bool translucency_wanted = alpha < 1.f;
    if (translucency_wanted != ledger.written_translucent) {
        material->SetMaterialVarFlag(MATERIAL_VAR_TRANSLUCENT, translucency_wanted);
        ledger.written_translucent = translucency_wanted;
    }
    if (alpha != ledger.written_alpha) {
        material->AlphaModulate(alpha);
        ledger.written_alpha = alpha;
    }
}

// Put one material back the way it was found.
inline void Release(IMaterial* material) {
    auto& ledgers = Ledgers();
    const auto it = ledgers.find(material);
    if (it == ledgers.end() || !it->second.touched) return;
    auto& ledger = it->second;
    if (ledger.written_translucent != ledger.original_translucent) {
        material->SetMaterialVarFlag(MATERIAL_VAR_TRANSLUCENT, ledger.original_translucent);
        ledger.written_translucent = ledger.original_translucent;
    }
    if (ledger.written_alpha != ledger.original_alpha) {
        material->AlphaModulate(ledger.original_alpha);
        ledger.written_alpha = ledger.original_alpha;
    }
    ledger.touched = false;
}

// Drop models that have not been drawn for a while (releasing their materials first)
// and ledger entries that are back to their original state.
inline void Prune() {
    const auto now = Clock();
    auto& models = Models();
    for (auto it = models.begin(); it != models.end();) {
        if (now - it->second.seen <= 600) { ++it; continue; }
        for (auto* material : it->second.materials) Release(material);
        it = models.erase(it);
    }
    auto& ledgers = Ledgers();
    for (auto it = ledgers.begin(); it != ledgers.end();) {
        if (!it->second.touched) it = ledgers.erase(it);
        else ++it;
    }
}

// FRAME_RENDER_START: bring every recorded model's materials to the requested state
// and let go of the ones that no longer want it.
inline void Sync(float texture_percent, float prop_percent) {
    ++Clock();
    for (auto& [header, model] : Models()) {
        const float alpha = AlphaFor(PercentFor(model.target, texture_percent, prop_percent));
        if (alpha >= 1.f) {
            for (auto* material : model.materials) Release(material);
            continue;
        }
        for (auto* material : model.materials) Apply(material, alpha);
    }
    if ((Clock() & 0xff) == 0) Prune();
}

// Map change: the material pointers are about to become stale. Never dereferences the
// old keys, it just forgets them.
inline void Purge() {
    Models().clear();
    Ledgers().clear();
}
}
