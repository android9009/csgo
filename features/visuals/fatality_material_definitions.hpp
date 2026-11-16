#pragma once
// Adapted from Fatality dee7327 / misc/init.cpp. Intentionally revised for
// distinct lit, unlit and composite looks; not a byte-for-byte VMT reproduction.
namespace FatalityMaterialDefinition {
inline constexpr const char* normal = R"VMT("VertexLitGeneric"
{
 "$basetexture" "vgui/white"
 "$model" "1"
 "$nofog" "1"
 "$halflambert" "1"
})VMT";
inline constexpr const char* flat = R"VMT("UnlitGeneric"
{
 "$basetexture" "vgui/white"
 "$model" "1"
 "$nofog" "1"
})VMT";
inline constexpr const char* rim_glow = R"VMT("VertexLitGeneric"
{
 "$basetexture" "vgui/white"
 "$model" "1"
 "$nofog" "1"
 "$additive" "1"
 "$envmap" "models/effects/cube_white"
 "$envmaptint" "[1 1 1]"
 "$envmapfresnel" "1"
 "$envmapfresnelminmaxexp" "[0 1 2]"
})VMT";
// A dedicated sharp Fresnel overlay avoids selfillum tint being multiplied
// by the primary diffuse hue (e.g. green Primary destroying magenta Secondary).
inline constexpr const char* contour = R"VMT("VertexLitGeneric"
{
 "$basetexture" "vgui/white"
 "$model" "1"
 "$nofog" "1"
 "$additive" "1"
 "$envmap" "models/effects/cube_white"
 "$envmaptint" "[1 1 1]"
 "$envmapfresnel" "1"
 "$envmapfresnelminmaxexp" "[0 1 6]"
})VMT";
inline constexpr const char* flow = R"VMT("UnlitGeneric"
{
 "$basetexture" "dev/zone_warning"
 "$basetexturetransform" "center .5 .5 scale 2 2 rotate 0 translate 0 0"
 "$model" "1"
 "$nofog" "1"
 "$additive" "1"
 "Proxies"
 {
  "TextureScroll"
  {
   "texturescrollvar" "$basetexturetransform"
   "texturescrollrate" "0.35"
   "texturescrollangle" "90"
  }
 }
})VMT";
inline constexpr const char* acrylic = R"VMT("VertexLitGeneric"
{
 "$basetexture" "vgui/white"
 "$bumpmap" "effects/flat_normal"
 "$model" "1"
 "$nofog" "1"
 "$halflambert" "1"
 "$envmap" "env_cubemap"
 "$envmaptint" "[.35 .35 .35]"
 "$envmapfresnel" "1"
 "$envmapfresnelminmaxexp" "[0.05 1 3]"
 "$phong" "1"
 "$phongexponent" "48"
 "$phongboost" "3"
 "$phongtint" "[1 1 1]"
 "$phongfresnelranges" "[0.05 0.5 1]"
 "$rimlight" "1"
 "$rimlightexponent" "4"
 "$rimlightboost" "1.5"
})VMT";
}
