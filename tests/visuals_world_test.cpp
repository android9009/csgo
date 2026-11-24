// Host checks for Visuals -> World: Nightmode / Sky colour modulation and the Sunlight
// constants. The file only exercises the pure parts of features/visuals/misc/world/world_modulation.hpp;
// the detour and the cvar writes need the game.
#include "../features/visuals/misc/world/world_modulation.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>

using namespace WorldModulation;

int main() {
    // Texture group routing: only the group that has its own tint is touched, and a group whose
    // checkbox is off must come back as "no group" rather than as white.
    Settings settings;
    assert(Detail::GroupFor("World textures", settings) == GroupNone);
    assert(Detail::GroupFor("SkyBox textures", settings) == GroupNone);

    settings.nightmode = true;
    assert(Detail::GroupFor("World textures", settings) == GroupWorld);
    assert(Detail::GroupFor("SkyBox textures", settings) == GroupNone);
    assert(Detail::GroupFor("StaticProp textures", settings) == GroupNone);
    assert(Detail::GroupFor("", settings) == GroupNone);
    assert(Detail::GroupFor(nullptr, settings) == GroupNone);

    settings.sky = true;
    assert(Detail::GroupFor("SkyBox textures", settings) == GroupSkyBox);
    assert(Detail::GroupFor("World textures", settings) == GroupWorld);

    // A group name has to match exactly: the engine's own strings are what `GetTextureGroupName`
    // returns, so a prefix of one of them is a different group.
    settings.nightmode = true;
    assert(Detail::GroupFor("World texture", settings) == GroupNone);
    assert(Detail::GroupFor("World textures ", settings) == GroupNone);

    // Channel extraction from the menu's packed colour (0xRRGGBBAA).
    // rgba() packs (r << 16) | (g << 8) | b | (a << 24), so red is 0xFFFF0000.
    assert(std::fabs(Detail::Channel(0xFFFF0000u, 0) - 1.f) < 1e-6f); // r
    assert(std::fabs(Detail::Channel(0xFFFF0000u, 1) - 0.f) < 1e-6f); // g
    assert(std::fabs(Detail::Channel(0xFFFF0000u, 2) - 0.f) < 1e-6f); // b
    assert(std::fabs(Detail::Channel(0x0000FFFFu, 2) - 1.f) < 1e-6f); // b from the low byte
    assert(std::fabs(Detail::Channel(0x808080FFu, 1) - (128.f / 255.f)) < 1e-6f);

    // Multipliers: a group with no tint is 1/1/1, the tinted group gets the colour's channels.
    float r = 0.f, g = 0.f, b = 0.f;
    Detail::Multipliers(Settings{}, GroupWorld, r, g, b);
    assert(r == 1.f && g == 1.f && b == 1.f);

    Settings night;
    night.nightmode = true;
    night.nightmode_colour = 0xFF804020u; // a=0xFF r=0x80 g=0x40 b=0x20
    Detail::Multipliers(night, GroupWorld, r, g, b);
    assert(std::fabs(r - (128.f / 255.f)) < 1e-6f);
    assert(std::fabs(g - (64.f / 255.f)) < 1e-6f);
    assert(std::fabs(b - (32.f / 255.f)) < 1e-6f);
    Detail::Multipliers(night, GroupSkyBox, r, g, b);
    assert(r == 1.f && g == 1.f && b == 1.f);

    // Apply multiplies the engine's own modulation and clamps into 0..1, and it leaves a group
    // that is not tinted - and a missing pointer - untouched.
    float er = .5f, eg = .5f, eb = .5f;
    Detail::Apply(Settings{}, GroupWorld, &er, &eg, &eb);
    assert(er == .5f && eg == .5f && eb == .5f);

    er = eg = eb = 1.f;
    Detail::Apply(night, GroupWorld, &er, &eg, &eb);
    assert(std::fabs(er - (128.f / 255.f)) < 1e-6f);
    assert(std::fabs(eg - (64.f / 255.f)) < 1e-6f);
    assert(std::fabs(eb - (32.f / 255.f)) < 1e-6f);

    // An engine value above one is clamped instead of being handed to the shader.
    er = eg = eb = 4.f;
    Detail::Apply(night, GroupWorld, &er, &eg, &eb);
    assert(er <= 1.f && eg <= 1.f && eb <= 1.f);

    // Negative and non-finite inputs cannot escape as negative or NaN.
    er = -1.f;
    eg = std::nanf("");
    eb = 1.f;
    Detail::Apply(night, GroupWorld, &er, &eg, &eb);
    assert(er == 0.f && eg == 0.f && eb >= 0.f);

    Detail::Apply(night, GroupWorld, nullptr, &eg, &eb);
    Detail::Apply(night, GroupNone, &er, &eg, &eb);

    // A broken picker (all channels zero would be black) is not treated as "render the map
    // black": the multiplier keeps whatever the colour says, and the clamp above is what keeps
    // the result valid.
    Settings black;
    black.nightmode = true;
    black.nightmode_colour = 0xFF000000u;
    Detail::Multipliers(black, GroupWorld, r, g, b);
    assert(r == 0.f && g == 0.f && b == 0.f);

    // Sunlight: a low sun and a long shadow distance, i.e. main's sunset mode.
    assert(kSunsetRotation[0] < 0.f);
    assert(kSunsetMaxShadowDistance == 10000.f);

    std::puts("visuals_world_test: ok");
    return 0;
}
