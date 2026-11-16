#pragma once
// Material definitions from reversed2603/fatality_source, dee7327.
namespace FatalityMaterialDefinition {
inline constexpr const char* normal = R"VMT("VertexLitGeneric"
{
  "$basetexture" "vgui/white"
  "$nofog"        "1"
  "$model"        "1"
  "$nocull"       "0"
  "$selfillum"    "1"
  "$halflambert"  "1"
  "$znearer"      "0"
  "$flat"         "1"
}
)VMT";
inline constexpr const char* flat = R"VMT("UnlitGeneric"
{
  "$basetexture" "vgui/white"
  "$nofog"        "1"
  "$model"        "1"
  "$nocull"       "0"
  "$selfillum"    "1"
  "$halflambert"  "1"
  "$znearer"      "0"
  "$flat"         "1"
}
)VMT";
inline constexpr const char* rim_glow = R"VMT("VertexLitGeneric"
{
	"$basetexture" "vgui/white"
	"$additive" "1"
	"$envmap" "models/effects/cube_white"
	"$envmaptint" "[1 1 1]"
	"$envmapfresnel" "1"
	"$envmapfresnelminmaxexp" "[0 1 2]"
	"$alpha" "0.8"
})VMT";
inline constexpr const char* contour = R"VMT("VertexLitGeneric"
{
	$basetexture "vgui/white"
	$bumpmap "vgui/white"
	$selfillum "1"
	$selfIllumFresnel "1"
	$selfIllumFresnelMinMaxExp "[0 0.18 0.1]"
	$selfillumtint "[0.3 0.001 0.1]"
})VMT";
inline constexpr const char* flow = R"VMT("UnlitGeneric"
{
	"$basetexture" "dev/zone_warning"
	"proxies"
	{
		"TextureScroll"
		{
			"texturescrollvar" "$basetexturetransform"
			"texturescrollrate" "0.1"
			"texturescrollangle" "90"
		}
	}
})VMT";
inline constexpr const char* acrylic = R"VMT("VertexLitGeneric"
{
	"$basetexture" "VGUI/white"
	"$bumpmap" "de_nuke/hr_nuke/pool_water_normals_002"
	"$bumptransform" "center 0.5 0.5 scale 0.25 0.25 rotate 0.0 translate 0.0 0.0"
	"$nofog" "1"
	"$model" "1"
	"$color2" "[1.0 1.0 1.0]"
	"$halflambert" "1"
	"$envmap" "env_cubemap"
	"$envmaptint" "[0.1 0.1 0.1]"
	"$envmapfresnel" "1.0"
	"$envmapfresnelminmaxexp" "[1.0 1.0 1.0]"
	"$phong" "1"
	"$phongexponent" "1024"
	"$phongboost" "4.0"
	"$phongfresnelranges" "[1.0 1.0 1.0]"
	"$rimlight" "1"
	"$rimlightexponent" "4.0"
	"$rimlightboost" "2.0"
})VMT";
}
