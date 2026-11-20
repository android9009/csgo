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
} // namespace

Settings Snapshot() {
    const auto effects = VisualRuntime::GetEffects();
    Settings settings;
    settings.nightmode = effects.nightmode;
    settings.nightmode_colour = effects.nightmode_colour;
    settings.sky = effects.sky;
    settings.sky_colour = effects.sky_colour;
    settings.fullbright = effects.fullbright;
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
    if (g_fullbright_owned && g_fullbright.var) {
        ConVars::SetInt(g_fullbright.var, static_cast<int>(g_fullbright.value));
    }
    g_fullbright_owned = false;
    g_fullbright = ConVars::SavedVar{};
    g_fullbright_attempted = false;
    g_tint_reported = false;
}
} // namespace WorldModulation
