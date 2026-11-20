#include "world_modulation.hpp"
#include "../../sdk/convars.hpp"
#include "../../sdk/interfaces/interfaces.hpp"
#include "../../sdk/material.hpp"
#include "../../sdk/visual_runtime_state.hpp"
#include "../../utils/log.hpp"
#include <windows.h>

namespace WorldModulation {
namespace {
// `mat_fullbright` is the engine's own switch for the material system's fullbright mode. It is
// captured before the first write so switching the checkbox off (or unloading) puts the value
// back exactly as the game had it, including after a map change.
ConVars::SavedVar g_fullbright;
bool g_fullbright_owned{};
bool g_fullbright_attempted{};
bool g_tint_reported{};

// "Sunlight": the cascaded-shadow rotation, re-asserted only when the value is not what this
// module wants (the game re-applies the map's own values on every level change, which is the
// same reason `world_fog` re-asserts `fog_override`). main also clears the cvar's change
// callbacks first (`fn_change_callbacks.remove_count()`); this branch has no callback registry,
// so a value the game has changed back is simply rewritten on the next frame.
struct ShadowState {
    bool attempted{};
    bool ready{};
    bool active{};
    ConVars::SavedVar rot_override{};
    ConVars::SavedVar max_dist{};
    ConVars::SavedVar rot[3]{};
} g_shadows;

void ApplySunlight(bool enabled) {
    if (!Interfaces::ConVar) return;
    if (!g_shadows.attempted) {
        g_shadows.attempted = true;
        g_shadows.ready = g_shadows.rot_override.Capture("cl_csm_rot_override") &&
                          g_shadows.max_dist.Capture("cl_csm_max_shadow_dist") &&
                          g_shadows.rot[0].Capture("cl_csm_rot_x") &&
                          g_shadows.rot[1].Capture("cl_csm_rot_y") &&
                          g_shadows.rot[2].Capture("cl_csm_rot_z");
        if (!g_shadows.ready) {
            LOG_WARN("visuals world: the cl_csm_* cvars did not resolve; \"Sunlight\" stays "
                     "inactive");
            return;
        }
        LOG_INFO("visuals world: shadow cvars captured (override %.0f, dist %.0f)",
                 static_cast<double>(g_shadows.rot_override.value),
                 static_cast<double>(g_shadows.max_dist.value));
    }
    if (!g_shadows.ready) return;
    if (enabled) {
        if (g_shadows.rot_override.var && ConVars::Float("cl_csm_rot_override", 0.f, 1.f, 0.f) < .5f)
            ConVars::SetInt(g_shadows.rot_override.var, 1);
        if (g_shadows.max_dist.var &&
            ConVars::Float("cl_csm_max_shadow_dist", -1.f, 100000.f, -1.f) !=
                kSunsetMaxShadowDistance)
            ConVars::SetFloat(g_shadows.max_dist.var, kSunsetMaxShadowDistance);
        static const char* names[3] = {"cl_csm_rot_x", "cl_csm_rot_y", "cl_csm_rot_z"};
        for (int i = 0; i < 3; ++i) {
            if (!g_shadows.rot[i].var) continue;
            if (ConVars::Float(names[i], -360.f, 360.f, -999.f) == kSunsetRotation[i]) continue;
            ConVars::SetFloat(g_shadows.rot[i].var, kSunsetRotation[i]);
        }
        if (!g_shadows.active) {
            g_shadows.active = true;
            LOG_INFO("visuals world: shadow rotation overridden (low sun)");
        }
    } else if (g_shadows.active) {
        const bool ok = g_shadows.rot_override.Restore() && g_shadows.max_dist.Restore() &&
                        g_shadows.rot[0].Restore() && g_shadows.rot[1].Restore() &&
                        g_shadows.rot[2].Restore();
        g_shadows.active = false;
        LOG_INFO("visuals world: shadow cvars handed back%s", ok ? "" : " - one was already gone");
    }
}

} // namespace

Settings Snapshot() {
    const auto effects = VisualRuntime::GetEffects();
    Settings settings;
    settings.nightmode = effects.nightmode;
    settings.nightmode_colour = effects.nightmode_colour;
    settings.sky = effects.sky;
    settings.sky_colour = effects.sky_colour;
    settings.fullbright = effects.fullbright;
    settings.sunlight = effects.sunlight;
    return settings;
}

void Tint(void* material, float* r, float* g, float* b) {
    if (!material || !Interfaces::MaterialSystem) return;
    const Settings settings = Snapshot();
    if (!settings.nightmode && !settings.sky) return;
    __try {
        auto* mat = static_cast<IMaterial*>(material);
        // The error material has no texture group worth reading, and tinting it turns the
        // purple checkerboard into something worse.
        if (mat->IsErrorMaterial()) return;
        // "smoke" is drawn with world textures but is a gameplay cue: tinting it makes smoke
        // grenades invisible on a night-mode map (main skips it for the same reason).
        const char* name = mat->GetName();
        if (name) {
            const char* smoke = "smoke";
            const char* a = name;
            const char* c = smoke;
            while (*a && *c && *a == *c) { ++a; ++c; }
            if (*a == *c) return;
        }
        const int group = Detail::GroupFor(mat->GetTextureGroupName(), settings);
        if (group == GroupNone) return;
        Detail::Apply(settings, group, r, g, b);
        if (!g_tint_reported) {
            g_tint_reported = true;
            LOG_INFO("visuals world: colour modulation active (group %d) for \"%s\"", group,
                     name ? name : "?");
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            LOG_WARN("visuals world: rejected a material read 0x%08lX; the tint stays off",
                     GetExceptionCode());
        }
    }
}

void Frame() {
    if (!Interfaces::ConVar) return;
    if (!g_fullbright_attempted) {
        g_fullbright_attempted = true;
        if (!g_fullbright.Capture("mat_fullbright")) {
            LOG_WARN("visuals world: mat_fullbright not found; \"Fullbright\" stays inactive");
            return;
        }
        LOG_INFO("visuals world: mat_fullbright captured at %.0f",
                 static_cast<double>(g_fullbright.value));
    }
    const Settings settings = Snapshot();
    ApplySunlight(settings.sunlight);
    if (settings.fullbright && !g_fullbright_owned) {
        if (ConVars::SetInt(g_fullbright.var, 1)) {
            g_fullbright_owned = true;
            LOG_INFO("visuals world: mat_fullbright = 1");
        }
    } else if (!settings.fullbright && g_fullbright_owned) {
        if (ConVars::SetInt(g_fullbright.var, static_cast<int>(g_fullbright.value))) {
            g_fullbright_owned = false;
            LOG_INFO("visuals world: mat_fullbright handed back to the game (%.0f)",
                     static_cast<double>(g_fullbright.value));
        }
    }
}

void Shutdown() {
    ApplySunlight(false);
    g_shadows = ShadowState{};
    if (g_fullbright_owned && g_fullbright.var) {
        ConVars::SetInt(g_fullbright.var, static_cast<int>(g_fullbright.value));
    }
    g_fullbright_owned = false;
    g_fullbright = ConVars::SavedVar{};
    g_fullbright_attempted = false;
    g_tint_reported = false;
}
} // namespace WorldModulation
