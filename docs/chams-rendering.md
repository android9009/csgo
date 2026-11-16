# Chams rendering revision

This revision intentionally differs from a literal copy of Fatality's VMTs.
Reference: reversed2603/fatality_source dee7327, misc/init.cpp,
features/chams.cpp, features/entity_rendering.cpp and sdk interfaces.
Also reviewed: Osiris csgo-end-of-life, ChamsMaterialKeyValuesFactory.h
(MIT attribution in third_party/licenses/Osiris-inventory.txt).

## Materials and passes

| kind | Material | Passes per depth layer | Color / strength |
|---|---|---|---|
| 1 | Normal / VertexLitGeneric | lit surface | Primary RGBA; no selfillum or flat flag |
| 2 | Flat / UnlitGeneric | unlit surface | Primary RGBA |
| 3 | Rim Glow | Flat + additive cube_white Fresnel [0 1 2] | Primary base, Secondary edge RGB * alpha |
| 4 | Contour | Normal + sharp additive Fresnel [0 1 6] | Primary base, independent Secondary edge RGB * alpha |
| 5 | Flow | Flat + additive scrolling dev/zone_warning | Primary base, Secondary RGB; both alphas affect overlay |
| 6 | Acrylic | lit, phong, cubemap surface | Primary diffuse; Secondary RGB * alpha controls envmap/phong, alpha also scales rim boost |
| 7 | Legacy | Flat + retained dev/glow_armsrace | Primary base, Secondary Fresnel tint/strength |

Contour replaces the old selfillum-Fresnel scheme with a dedicated sharp envmap
pass. This avoids multiplication of Secondary by Primary's RGB, which could
black out an oppositely colored contour. It is a Fresnel edge, not a constant
pixel-width screen-space outline. Legacy keeps the engine material and configures
its Fresnel parameters when exposed. It does not silently fall back to Flat.
Flow keeps Fatality's known texture, but now scrolls at 0.35 with a denser UV scale
and additive composition over a clean base. Actual texture availability is still
installation-dependent. Acrylic uses a flat normal map instead of the Nuke water
normal, a usable 48 phong exponent, nonconstant Fresnel ranges and local lighting.

`ChamsPasses::Build` plans both game and preview drawing. Hidden passes precede
visible passes. The hidden base and overlay use Hidden RGB, preserving Secondary
alpha as effect strength; visible passes use Primary/Secondary. Preview requests
visible passes only because there is no wall to occlude the model. A successful
base is retained if an optional overlay is unavailable. Primary alpha is applied
continuously, without the former >95% opacity snap.

## ABI and state

CS:GO Win32 interface slots, cross-checked with the reference declarations:
- IMaterial: FindVar 11, refcount 12/13, AlphaModulate 27, ColorModulate 28,
  Set/GetMaterialVarFlag 29/30, IsErrorMaterial 42, GetAlpha 44, GetColor 45,
  GetShaderName 49.
- IMaterialVar: SetFloatValue 4, SetIntValue 5, SetVecValue 11.
- IVRenderView: SetBlend 4, GetBlend 5, SetColorModulation 6, GetColorModulation 7.
- IStudioRender: ambient Vector4 colors 20, local LightDesc_t array 22.
  LightDesc_t has a checked 88-byte layout. No extra vector getter is invoked.

Material lookups/variable pointers are cached. Private material names are versioned
as chams_v2 so reinjection does not select an old same-name shader definition.
Per-pass material modulation, alpha, ignore-Z, additive and wireframe flags, plus
render-view modulation/blend, are restored. Legacy variable writes follow the
reference's retained-material semantics (its tint is set afresh each draw).

## Preview lighting

Only Chams preview receives the new neutral directional ambient cube and two
local directional lights. The other agent/weapon previews retain their previous
lighting and turntable camera behavior. Texture lookup tries editor/cubemap.hdr,
editor/cubemap, then models/effects/cube_white; missing/error-name/zero-width
textures are rejected and a selected texture is retained/released. Local lights
are cleared on normal cleanup and fault cleanup. A missing cube is logged, not
represented as successful reflection validation.

## Validation scope

Portable tests check every kind's pass count/order, hidden/visible colors,
Secondary alpha, disabled/invalid choices, and VMT distinctions. Windows builds
check native compilation only. Actual shader output, UV scroll, reflection assets,
shader-driver behavior and Legacy stability require an in-game test. Preview uses
controlled studio illumination, not each map's lighting: pixel-identical output
with every match scene is neither expected nor claimed.
