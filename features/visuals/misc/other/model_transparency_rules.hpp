#pragma once
// Prop / texture transparency rules: slider -> alpha, and which draws the pass is
// allowed to touch. Interface-free so the rules can be unit tested off-target;
// the material walk lives in model_transparency.hpp.
#include <string_view>
#include "../../chams/chams_object_names.hpp"

namespace ModelTransparency {
// A model that reaches DrawModelExecute with more textures than this is left
// untouched rather than partially modulated.
inline constexpr int kMaxMaterials = 16;

// Slider percentage -> alpha. 0 % means "leave the model alone" (1.0), 100 %
// means fully invisible (0.0). Values outside the range are clamped.
inline float AlphaFor(float percent) {
    if (!(percent > 0.f)) return 1.f;
    if (percent > 100.f) percent = 100.f;
    return 1.f - percent * 0.01f;
}

enum class Target { None, Prop, Texture };

// Character models get "texture transparency" (their own skins go see-through),
// world props get "prop transparency". View models, hands and throwables are left
// alone: their materials are shared with the HUD/preview passes, and grenades and
// the bomb are covered by the glow/chams paths instead.
inline Target Classify(std::string_view network_name) {
    if (network_name.empty()) return Target::None;
    if (network_name == "CCSPlayer" || network_name == "CCSRagdoll" || network_name == "CHostage") return Target::Texture;
    if (network_name == "CBaseViewModel" || network_name == "CBaseWeaponWorldModel") return Target::None;
    if (ChamsObjectNames::Projectile(network_name) || network_name == "CPlantedC4" || network_name == "CC4") return Target::None;
    if (network_name == "CCascadeLight" || network_name == "CEnvTonemapController") return Target::None;
    // Everything else drawn through the model path is a world prop: phys/dynamic
    // props, breakables, doors, foliage. Whether the $alpha write is visible is up
    // to the entity's own .vmt, exactly as with the engine's other blends.
    return Target::Prop;
}

// Percent that applies to a subject, or 0 when the feature is off for it.
inline float PercentFor(Target target, float texture_percent, float prop_percent) {
    switch (target) {
    case Target::Texture: return texture_percent;
    case Target::Prop: return prop_percent;
    default: return 0.f;
    }
}
}
