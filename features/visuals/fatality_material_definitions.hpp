#pragma once
// Flat plus portable shader configurations inspired by Skeet-hook-source
// src/effects/model_render.cpp::prepare_material (kinds 2/3).
// No native-address bindings or private material-system layouts are copied.
namespace FatalityMaterialDefinition {
inline constexpr const char* flat = R"VMT("UnlitGeneric"
{
 "$basetexture" "vgui/white"
 "$model" "1"
 "$nofog" "1"
})VMT";
inline constexpr const char* shaded=R"VMT("VertexLitGeneric"
{
 "$basetexture" "vgui/white"
 "$bumpmap" "effects/flat_normal"
 "$model" "1"
 "$nofog" "1"
 "$allowdiffusemodulation" "1"
 "$basemapalphaphongmask" "1"
 "$basealphaenvmapmask" "1"
 "$phong" "1"
 "$phongboost" "1"
 "$phongexponent" "32"
 "$phongtint" "[1 1 1]"
 "$phongfresnelranges" "[1 0 0]"
})VMT";
inline constexpr const char* metallic=R"VMT("VertexLitGeneric"
{
 "$basetexture" "vgui/white"
 "$model" "1"
 "$nofog" "1"
 "$allowdiffusemodulation" "1"
 "$basemapalphaphongmask" "1"
 "$basealphaenvmapmask" "1"
 "$phong" "1"
 "$phongboost" "0"
 "$envmap" "editor/cube_vertigo_blurred_hdr"
 "$envmaptint" "[1 1 1]"
 "$envmapcontrast" "0"
 "$envmapfresnel" "1"
})VMT";

}
