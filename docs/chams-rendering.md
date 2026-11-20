# Chams rendering revision

This revision intentionally differs from a literal copy of Fatality's VMTs.
Reference: reversed2603/fatality_source dee7327, misc/init.cpp,
features/chams.cpp, features/entity_rendering.cpp and sdk interfaces.
Also reviewed: Osiris csgo-end-of-life, ChamsMaterialKeyValuesFactory.h
(MIT attribution in sdk/third_party/licenses/Osiris-inventory.txt).

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

## Current reduced set and completed runtime paths

The later user request supersedes the seven-material table above: **None / Flat /
Glow** are now the only options everywhere, including world objects, ragdolls,
arms, attachments, history and On shot controls. Glow is retained Legacy, not the
removed Rim Glow material. Saved IDs 2 (Flat) and 7 (Glow) are preserved; removed
IDs normalize to None. Only Flat's VMT is created; old VMT definitions are removed.

Ragdolls resolve DT_CSRagdoll::m_hPlayer, with a reverse player m_hRagdoll fallback.
Team classification comes from that player or the ragdoll's own team RecvProp,
not blindly from the BaseEntity team offset. The local ragdoll uses Local's player
profile (there is no independent local-ragdoll control). Attachments resolve the
weapon-world-model parent, viewmodel owner, owner entity and move-parent links
with a bounded, cycle-checked chain. Draw-only renderables can resolve their base
entity using the supplied IClientRenderable / IClientUnknown ABI. First-person
weapons use Local attachments; first-person arms retain their separate setting.

On shot is implemented as a bounded eight-ghost render cache. Enemy draw payloads,
model transform and up to 256 bone matrices are copied, not retained as transient
bone pointers. Confirmed local weapon_fire selects a target using a current
crosshair trace; player_hurt provides actual-victim confirmation/fallback. Ghosts
last one second, fading over the last 0.4 seconds. Snapshots older than 0.25 seconds
are rejected. Duplicate fire/hurt notifications are debounced. Replay validates
entity identity/model and expires on disable, clock rollback, round/map change.
The listener is removed at shutdown. Replay occurs in actual IVRenderView slot 9
SceneEnd; the old optional VViewRender slot 6 hook was not the SceneEnd interface.

This project has no functioning rage resolver/lag-record shot pipeline. Therefore
this is a rendered-pose fire/hurt ghost, **not a server rewind/lag-compensated
resolver record**. A miss with no identifiable crosshair target creates no ghost;
penetration/spread target selection is not inferred from missing rage data.
Existing history/fake controls are restricted to the same three options, but this
change does not implement history lag compensation or fake-angle generation.

Portable tests validate reduced selection, pass order, hidden colors, fade and
snapshot freshness. Native event callbacks, attachment chains, all agent/ragdoll
classes and ghost replay still require an in-game check; no runtime result is
claimed from compilation alone.

## Skeet-hook-source review and additional modes

Reference reviewed: https://github.com/makavtoJJ1/Skeet-hook-source at
`1bab8213bd866f4c366a2071a09b27d6ff76820e`.
- src/effects/model_render.cpp: prepare_material, variable configuration,
  resource names, classification and modulation.
- src/menu/colored_models_menu.cpp: Default / Solid / Shaded / Metallic / Glow /
  Bubble / Original choices.
- include/skeet/effects/model_render.hpp: texture resource names.

The repository contains model chams, not just Lua material bindings. Its native
address cells, material-internal vfuncs (e.g. 99/69), cloned-vtable recovery and
studio memory offsets are build-specific. None of those sdk/hooks/layouts are imported.
No license file was identified in the recursive repository listing; no C++ source
was copied. The material parameter configurations and provenance are documented
here; no claim of an authenticated original Skeet source is made.

At the user's later request, two choices are added without replacing Flat/Glow:
- **Shaded (ID 9)**: VertexLitGeneric + Phong, Secondary RGB controls highlight
  tint; Secondary alpha controls exponent max(1, 255-alphaByte), as in reference
  kind 2. A flat normal map is supplied explicitly for the Phong path.
- **Metallic (ID 10)**: VertexLitGeneric + editor/cube_vertigo_blurred_hdr,
  Phong boost zero, Secondary RGB controls envmap tint; Secondary alpha controls
  Fresnel response 1-alpha, as in reference kind 3.

Primary alpha remains surface opacity. Unlike Glow's intensity alpha, the two
new modes deliberately preserve the reference's Secondary-alpha semantics.
New IDs do not reuse the removed Normal/Acrylic IDs or alter existing saves.
Both modes share live/preview/ragdoll/attachment/shot-ghost pass infrastructure;
all material lists show None / Flat / Glow / Shaded / Metallic. Bubble and the
reference's alternate Glow are not blindly imported over the existing Glow.
Rendering depends on Source shader support, cubemap assets and illumination;
portable tests/native builds are not a substitute for a visual in-game check.

## Prop / texture transparency

`features/visuals/model_transparency.hpp` implements the two Visuals → Misc sliders
that were previously dead.

Fatality's reference implementation (reversed2603/fatality_source dee7327) does it in
two places: `hooks/scene_end.cpp` forces `RenderableInfo_t::m_nTranslucencyType` so the
leaf system queues studio props into the translucent list, and
`hooks/draw_model_execute.cpp` `shader_api_draw_mesh` writes the alpha straight into
`MeshInstanceData_t::m_DiffuseModulation.w` inside `CShaderAPIDx8::DrawMesh`. The
second one lives in `shaderapidx9.dll`, which the user did not supply, and the leaf
function has no signature we can prove either, so this port keeps the *effects* and
reaches them through the material system with verified slots only:

- `DrawModelExecute` only *records* the draw: the model's studio header plus whether
  it is a prop or a character. Materials come from the header's texture list (the
  texture name is the material name inside the `Model textures` group).
- At `FRAME_RENDER_START` the recorded models' materials get `$alpha` and
  `MATERIAL_VAR_TRANSLUCENT`; `FRAME_RENDER_END` restores them. Because the engine
  asks the material for translucency while it builds the render lists
  (`ComputeTranslucencyType` → `IMaterial::IsTranslucent`), this places props on the
  translucent list exactly where Fatality's leaf hook does, and `$alpha` carries the
  amount `m_DiffuseModulation.w` would have.
- `prop_transparency` applies to world props (phys/dynamic props, breakables, doors,
  foliage); `texture_transparency` applies to character models (`CCSPlayer`,
  `CCSRagdoll`, `CHostage`) so their own skins go see-through.
- View models, hands, weapon world models, throwables and the bomb are excluded:
  their materials are shared with the HUD/preview passes, and grenades/C4 are the
  glow and chams paths' business.
- The slider is a percentage: 0 leaves the model untouched, 100 is fully invisible.
  `model_transparency_rules.hpp` holds that mapping and the classification, and is
  interface-free so `tests/model_transparency_test.cpp` can check it off-target.
- Captured "original" alpha/flag values are kept per material so a missed restore
  (an exception between the begin and the end) cannot drift; the map is dropped when
  the in-game context resets.
