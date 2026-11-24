#include "removals.hpp"
#include "../../../../context.hpp"
#include "../../../../sdk/convars.hpp"
#include "../../../../sdk/entity/entity.hpp"
#include "../../../../sdk/entity/player.hpp"
#include "../../../../sdk/interfaces/interfaces.hpp"
#include "../../../../sdk/material.hpp"
#include "../../../../utils/memory.hpp"
#include <atomic>
#include <cstring>

namespace Removals {
namespace {
std::atomic<std::uint64_t> g_mask{0};

// ---------------------------------------------------------------------------
// Cvar overrides (post processing / shadows / water / particle / blood / decals)
// ---------------------------------------------------------------------------
//
// Each override captures the cvar's value before the first write and hands it
// back when the bit goes off, when there is no game, and on unload. Writes are
// change-driven: nothing is written while the cvar already holds the wanted
// value. The same shape as WorldFog, one table instead of two cvars.
struct CvarOverride {
    const char* name;
    ConVars::ConVar* var{};
    int saved{};
    int target;
    bool asserted{};
    bool found{};
};

CvarOverride g_overrides[] = {
    {"mat_postprocess_enable", {}, 0, 0, false, false}, // bit 3: "Post Processing"
    {"cl_csm_shadows",         {}, 0, 0, false, false}, // bit 9: "Shadows" (main's choice)
    {"mat_water_simm",         {}, 0, 0, false, false}, // bit 4: "Water Effects"
    {"r_drawparticles",        {}, 0, 0, false, false}, // bit 2: "Particle" (render switch)
    {"mat_decals",             {}, 0, 0, false, false}, // bits 5+6: "Blood"/"Decals"
};

void CaptureCvars() {
    for (auto& override_entry : g_overrides) {
        if (override_entry.found) continue;
        override_entry.var = ConVars::Find(override_entry.name);
        if (!override_entry.var) continue;
        override_entry.saved = override_entry.var->GetInt();
        override_entry.found = true;
    }
}

void AssertCvars(const Flags& flags) {
    CaptureCvars();
    const struct {
        CvarOverride* entry;
        bool wanted;
    } table[] = {
        {&g_overrides[0], flags.post_processing},
        {&g_overrides[1], flags.shadows},
        {&g_overrides[2], flags.water},
        {&g_overrides[3], flags.particle},
        {&g_overrides[4], Detail::DecalsActive(flags.blood, flags.decals)},
    };
    for (const auto& row : table) {
        auto* entry = row.entry;
        if (!entry->found) continue;
        if (Detail::ShouldWrite(row.wanted, entry->asserted)) {
            const int value = row.wanted ? entry->target : entry->saved;
            if (entry->var->GetInt() != value) ConVars::SetInt(entry->var, value);
            entry->asserted = row.wanted;
        } else if (row.wanted && entry->var->GetInt() != entry->target) {
            // The game (a map config, a reset) switched the cvar back mid-override:
            // re-assert it, the same way the world fog re-asserts its override.
            ConVars::SetInt(entry->var, entry->target);
        }
    }
}

void RestoreCvars() {
    for (auto& override_entry : g_overrides) {
        if (!override_entry.found || !override_entry.asserted) continue;
        if (override_entry.var->GetInt() != override_entry.saved)
            ConVars::SetInt(override_entry.var, override_entry.saved);
        override_entry.asserted = false;
    }
}

// ---------------------------------------------------------------------------
// Smoke: MATERIAL_VAR_NO_DRAW on the vistasmoke sprite materials (main's
// remove_projectiles.cpp). The smoke cloud is vpcf-sprite rendered through these
// four materials, so the flag hides it wherever it is.
// ---------------------------------------------------------------------------
const char* const kSmokeMaterials[4] = {
    "particle/vistasmokev1/vistasmokev1_smokegrenade",
    "particle/vistasmokev1/vistasmokev1_emods",
    "particle/vistasmokev1/vistasmokev1_emods_impactdust",
    "particle/vistasmokev1/vistasmokev1_fire",
};

struct SmokeState {
    IMaterial* material[4]{};
    bool resolved[4]{};
    bool asserted{};
};

SmokeState g_smoke;

void AssertSmoke(bool wanted) {
    // The vistasmoke materials load on demand (first smoke of the map), so a
    // missing slot is retried every frame while the bit is on - the same loop as
    // main's remove_smoke(). The FindMaterial miss is a hash lookup; complain=false
    // keeps a missing material off the console. The error material is shared, so it
    // is never flagged (and stops its slot's retries).
    if (wanted && Interfaces::MaterialSystem) {
        for (int i = 0; i < 4; ++i) {
            if (g_smoke.resolved[i]) continue;
            auto* material =
                Interfaces::MaterialSystem->FindMaterial(kSmokeMaterials[i], "Other textures", false, nullptr);
            if (material && !material->IsErrorMaterial()) {
                g_smoke.material[i] = material;
                g_smoke.resolved[i] = true;
            } else if (material) {
                g_smoke.resolved[i] = true; // error material: nothing to flag
            }
        }
    }
    if (!Detail::ShouldWrite(wanted, g_smoke.asserted)) return;
    if (wanted) {
        for (int i = 0; i < 4; ++i)
            if (g_smoke.material[i] && !g_smoke.material[i]->GetMaterialVarFlag(MATERIAL_VAR_NO_DRAW))
                g_smoke.material[i]->SetMaterialVarFlag(MATERIAL_VAR_NO_DRAW, true);
    } else {
        for (int i = 0; i < 4; ++i)
            if (g_smoke.material[i] && g_smoke.material[i]->GetMaterialVarFlag(MATERIAL_VAR_NO_DRAW))
                g_smoke.material[i]->SetMaterialVarFlag(MATERIAL_VAR_NO_DRAW, false);
    }
    g_smoke.asserted = wanted;
}

// ---------------------------------------------------------------------------
// Netvar removals (flash / visual punch)
// ---------------------------------------------------------------------------

// Bit 0: the local player's flash overlay.
void FlashFrameStage(int stage, bool enabled) {
    if (!enabled || (stage != 0 && stage != 5)) return;
    if (!ctx.in_game || !ctx.local || !ctx.local->IsAlive()) return;
    auto& duration = ctx.local->m_flFlashDuration();
    if (!Detail::ShouldClear(duration)) return;
    duration = 0.f;
}

// Bit 8: the local player's visual punch angles.
void PunchFrameStage(int stage, bool enabled) {
    if (!enabled || (stage != 0 && stage != 5)) return;
    if (!ctx.in_game || !ctx.local) return;
    auto& aim = ctx.local->m_aimPunchAngle();
    if (Detail::ShouldZero(aim.x, aim.y, aim.z)) aim = {};
    auto& view = ctx.local->m_viewPunchAngle();
    if (Detail::ShouldZero(view.x, view.y, view.z)) view = {};
}
} // namespace

void Publish(std::uint64_t mask) { g_mask.store(mask, std::memory_order_relaxed); }

void FrameStage(int stage) {
    const auto mask = g_mask.load(std::memory_order_relaxed);
    const auto flags = FromMask(mask);
    if (!ctx.in_game) {
        // Out of a game the overrides are handed straight back (the same contract
        // as the world fog).
        Restore();
        return;
    }
    FlashFrameStage(stage, flags.flash);
    PunchFrameStage(stage, flags.visual_punch);
    // The cvar and material state is read while the engine builds the frame, so
    // it is asserted at render start.
    if (stage == 5 && mask) {
        AssertCvars(flags);
        AssertSmoke(flags.smoke);
    }
}

void Restore() {
    RestoreCvars();
    AssertSmoke(false);
}

bool SkipModel(const ModelRenderInfo_t& info) {
    if (!Enabled(g_mask.load(std::memory_order_relaxed), Bits::Ragdolls)) return false;
    if (!ctx.in_game || !Interfaces::EntityList) return false;
    CBaseEntity* entity = info.entity_index > 0
                              ? static_cast<CBaseEntity*>(Interfaces::EntityList->GetClientEntity(info.entity_index))
                              : nullptr;
    if (!entity && info.renderable) {
        auto* unknown = MEM::GetVFunc<void*(__thiscall*)(void*)>(info.renderable, 0)(info.renderable);
        if (unknown) entity = MEM::GetVFunc<CBaseEntity*(__thiscall*)(void*)>(unknown, 7)(unknown);
    }
    auto* cls = entity ? entity->GetClientClass() : nullptr;
    return cls && cls->m_pNetworkName && std::strcmp(cls->m_pNetworkName, "CCSRagdoll") == 0;
}
} // namespace Removals
