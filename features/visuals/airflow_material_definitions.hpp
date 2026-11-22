#pragma once
// Material set of the old "airflow" project (origin/main: chams.cpp::init_materials),
// ported verbatim for the new base. The index order and menu labels match the old
// project's list: Textured, Shaded, Flat, Metallic, Glow, Bubble, Custom Sprite,
// Fade Up. Creation goes through the same KeyValues::FromString path as before
// (see fatality_material_factory.hpp); only the recipes and the colouring logic
// changed (render-view modulation instead of the material's own modulation).
namespace AirflowMaterialDefinition {
enum Index { Soft = 0, Shaded, Flat, Shiny, Glow, Bubble, World, FadeUp, Count };

inline constexpr const char* Name(int index) {
    switch (index) {
        case Soft:  return "csgo_soft";
        case Shaded:return "csgo_shaded";
        case Flat:  return "csgo_flat";
        case Shiny: return "csgo_shiny";
        case Glow:  return "csgo_glow";
        case Bubble:return "csgo_bubble";
        case World: return "csgo_world";
        default:    return "csgo_fadeup";
    }
}

inline constexpr const char* Shader(int index) {
    // csgo_flat is the only unlit recipe in the set (old materials[2]).
    return index == Flat ? "UnlitGeneric" : "VertexLitGeneric";
}

inline constexpr const char* VMT(int index) {
    switch (index) {
        case Soft:  // Textured: self-illuminated flat lit surface, colour from the render view.
            return R"VMT("VertexLitGeneric"
            {
                "$basetexture" "vgui/white"
                "$nofog" "1"
                "$model" "1"
                "$nocull" "0"
                "$selfillum" "1"
                "$halflambert" "1"
                "$znearer" "0"
                "$flat" "1"
                "$ignorez" "1"
            }
            )VMT";
        case Shaded: // Scene-lit surface with a (zero-exponent) phong slot for a tint.
            return R"VMT("VertexLitGeneric"
            {
                "$basetexture" "vgui/white"
                "$phong" "1"
                "$phongexponent" "0"
                "$phongboost" "0"
                "$rimlight" "1"
                "$rimlightexponent" "0"
                "$rimlightboost" "0"
                "$model" "1"
                "$nocull" "0"
                "$nofog" "0"
                "$ignorez" "1"
                "$halflambert" "0"
                "$basemapalphaphongmask" "1"
            }
            )VMT";
        case Flat:  // The old unlit recipe, used alone or as the glow base pass.
            return R"VMT("UnlitGeneric"
            {
                "$basetexture" "vgui/white"
                "$ignorez" "0"
                "$envmap" ""
                "$nofog" "1"
                "$model" "1"
                "$nocull" "0"
                "$selfillum" "1"
                "$halflambert" "1"
                "$znearer" "0"
                "$flat" "1"
            }
            )VMT";
        case Shiny: // Metallic: cubemap reflection tinted by the main colour.
            return R"VMT("VertexLitGeneric"
            {
                "$basetexture" "vgui/white"
                "$phong" "1"
                "$BasemapAlphaPhongMask" "1"
                "$phongexponent" "15"
                "$normalmapalphaenvmask" "1"
                "$envmap" "env_cubemap"
                "$envmaptint" "[0.6 0.6 0.6]"
                "$phongboost" "[0.6 0.6 0.6]"
                "$phongfresnelranges" "[0.5 0.5 1.0]"
                "$nofog" "1"
                "$ignorez" "1"
                "$model" "1"
                "$nocull" "0"
                "$selfillum" "1"
                "$halflambert" "1"
                "$znearer" "0"
                "$flat" "1"
            }
            )VMT";
        case Glow:  // Additive cube-white fresnel; drawn over the flat base pass.
            return R"VMT("VertexLitGeneric"
            {
                "$additive" "1"
                "$envmap" "models/effects/cube_white"
                "$envmaptint" "[1 1 1]"
                "$envmapfresnel" "1"
                "$envmapfresnelminmaxexp" "[0 1 2]"
                "$alpha" "1"
                "$nofog" "1"
                "$ignorez" "1"
            }
            )VMT";
        case Bubble: // Additive envmap fresnel shell.
            return R"VMT("VertexLitGeneric"
            {
                "$basetexture" "vgui/white"
                "$additive" "1"
                "$nofog" "1"
                "$ignorez" "1"
                "$basemapalphaphongmask" "1"
                "$envmap" "env_cubemap"
                "$envmapfresnel" "1"
                "$envmaptint" "[.2 .2 .2]"
            }
            )VMT";
        case World: // Custom Sprite: the money paint texture (old default base texture).
            return R"VMT("VertexLitGeneric"
            {
                "$baseTexture" "models\weapons\customization\paints\custom\money"
                "$basemapalphaphongmask" "1"
                "$envmap" "env_cubemap"
                "$envmapfresnel" "1"
                "$envmaptint" "[.2 .2 .2]"
            }
            )VMT";
        default:    // Fade Up: fresnel rim only; the diffuse is forced black at init.
            return R"VMT("VertexLitGeneric"
            {
                "$baseTexture" "vgui/white"
                "$basemapalphaphongmask" "1"
                "$envmap" "env_cubemap"
                "$envmapfresnel" "1"
                "$envmaptint" "[.2 .2 .2]"
            }
            )VMT";
    }
}

// Everything after the shader's opening '{' (the flat key/value body the
// KeyValues::FromString path consumes, matching the previous factory usage).
inline const char* Parameters(const char* vmt) {
    if (!vmt) return nullptr;
    for (; *vmt; ++vmt) if (*vmt == '{') return vmt + 1;
    return nullptr;
}
}
