#pragma once
// Visuals -> World: "Nightmode", "Sky" and "Fullbright".
//
// Provenance: this repository's `main` branch, `world_modulation.cpp`
// (`c_world_modulation::override_world_color`) and `vmt_hooks.cpp` (`override_config`).
//
//   Nightmode  - multiplies the colour modulation of every material whose texture group is
//                "World textures" by the menu colour.
//   Sky        - the same for the "SkyBox textures" group.
//   Fullbright - `mat_fullbright` forced to 1 for as long as the checkbox is on, and handed
//                back to the game's own value when it goes off (main sets
//                `material_system_config_t::m_nFullbright` through the
//                `IMaterialSystem::OverrideConfig` virtual; the cvar drives the same engine
//                switch and needs no unverified struct layout, which is why it is the route
//                this build takes).
//
// The colour path hangs off a detour on `IMaterial::GetColorModulation`
// (`materialsystem.dll`, `main`'s `get_color_modulation`), which the client calls once per
// material and frame. The maths is in `Detail` here so it can be checked on the host.
#include <cmath>
#include <cstdint>

namespace WorldModulation {
// Colour groups the two tint controls map to. The names are the engine's own texture group
// strings, i.e. exactly what `IMaterial::GetTextureGroupName` returns.
enum Group { GroupNone = 0, GroupWorld = 1, GroupSkyBox = 2 };

struct Settings {
    bool nightmode{};
    std::uint32_t nightmode_colour{0xFFFFFFFFu};
    bool sky{};
    std::uint32_t sky_colour{0xFFFFFFFFu};
    bool fullbright{};
};

namespace Detail {
// A packed colour (0xRRGGBBAA, the layout the menu's pickers use) as three multipliers, each in
// 0..1. A channel that is not a number counts as 1, so a broken picker renders the map as it
// was instead of black.
inline float Channel(std::uint32_t colour, int index) {
    const int shift = 16 - index * 8;
    const float value = static_cast<float>((colour >> shift) & 0xFFu) / 255.f;
    return std::isfinite(value) ? value : 1.f;
}

// Which group a texture group name belongs to, and whether that group is being tinted.
inline int GroupFor(const char* texture_group, const Settings& settings) {
    if (!texture_group) return GroupNone;
    const char* world = "World textures";
    const char* skybox = "SkyBox textures";
    auto equal = [](const char* a, const char* b) {
        if (!a || !b) return false;
        while (*a && *b) {
            if (*a != *b) return false;
            ++a; ++b;
        }
        return *a == *b;
    };
    if (settings.nightmode && equal(texture_group, world)) return GroupWorld;
    if (settings.sky && equal(texture_group, skybox)) return GroupSkyBox;
    return GroupNone;
}

// The three multipliers a material of `group` should be tinted with.
inline void Multipliers(const Settings& settings, int group, float& r, float& g, float& b) {
    r = g = b = 1.f;
    if (group == GroupWorld) {
        r = Channel(settings.nightmode_colour, 0);
        g = Channel(settings.nightmode_colour, 1);
        b = Channel(settings.nightmode_colour, 2);
    } else if (group == GroupSkyBox) {
        r = Channel(settings.sky_colour, 0);
        g = Channel(settings.sky_colour, 1);
        b = Channel(settings.sky_colour, 2);
    }
}

// The tint itself: multiplies the engine's own modulation in place. Values are clamped to
// 0..1, which is what the renderer accepts; without the clamp a slider above the colour's
// range would push HDR values the shaders do not expect.
inline void Apply(const Settings& settings, int group, float* r, float* g, float* b) {
    if (!r || !g || !b) return;
    if (group == GroupNone) return;
    float mr, mg, mb;
    Multipliers(settings, group, mr, mg, mb);
    auto scale = [](float value, float multiplier) {
        const float out = value * multiplier;
        if (!std::isfinite(out)) return 0.f;
        if (out < 0.f) return 0.f;
        if (out > 1.f) return 1.f;
        return out;
    };
    *r = scale(*r, mr);
    *g = scale(*g, mg);
    *b = scale(*b, mb);
}
} // namespace Detail

// The settings the menu thread last published (VisualRuntime carries them with the rest of the
// Visuals tab).
Settings Snapshot();

// Called from the GetColorModulation detour for every material the client modulates. `material`
// is an IMaterial*, the three floats are the engine's own modulation for this frame.
void Tint(void* material, float* r, float* g, float* b);
// FRAME_RENDER_START: keeps `mat_fullbright` in step with the checkbox.
void Frame();
// Hands `mat_fullbright` back to the game's value.
void Shutdown();
} // namespace WorldModulation
