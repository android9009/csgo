# Chams rendering

Chams use the material set and colouring logic of the old "airflow" project
(`origin/main`: `chams.cpp::init_materials` / `draw_model`), ported onto this
base. The previous Fatality-VMT revision (pastavar/chams_v3_* materials,
borrowed `dev/glow_armsrace`, material-own modulation) is gone.

## Materials

All eight recipes are created at runtime from VMT buffers (same KeyValues
path as before: `KeyValues::FromString(shader, flat params)` via
`FatalityMaterialFactory::Parse`, then `IMaterialSystem::CreateMaterial`).
Definitions live in `features/visuals/airflow_material_definitions.hpp`.

| menu label | bit | recipe | notes |
|---|---|---|---|
| Textured | 1 | csgo_soft | self-illuminated flat lit, VertexLitGeneric |
| Shaded | 9 | csgo_shaded | scene-lit, zero-exponent phong slot |
| Flat | 2 | csgo_flat | UnlitGeneric; also the glow base pass |
| Metallic | 10 | csgo_shiny | env_cubemap reflection + phong 15 |
| Glow | 7 | csgo_glow | additive cube_white fresnel; two passes |
| Bubble | 6 | csgo_bubble | additive envmap fresnel shell |
| Custom Sprite | 8 | csgo_world | money paint texture (old default) |
| Fade Up | 5 | csgo_fadeup | fresnel rim only; diffuse forced black |

Bit values 3 (rim_glow) and 4 (contour) are dead legacy slots: saved configs
holding them normalise to None in `VisualSettings::Configure`.

## Colouring (the airflow way)

Per pass, `PreviewMaterial::Apply` (`features/visuals/material_renderer.hpp`):

1. material flags: TRANSLUCENT + NOFOG on, IGNOREZ = pass's ignore-Z,
   WIREFRAME off;
2. `$envmaptint` (when the recipe has an envmap) = draw colour, i.e. the
   primary colour for every recipe except Glow, where it is the secondary
   (glow) colour; Glow also sets `$envmapfresnelminmaxexp` =
   `[0, 1, 5 - 5*fill]` with `fill = clamp(secondary.a - 0.15, 0, 1)`
   (old `glow_fill` formula); Metallic additionally sets `$phongtint` to the
   secondary colour;
3. colour and alpha are taken by the render view, not the material:
   `IVRenderView::SetColorModulation(rgb)` + `SetBlend(a)` (slots 6/4), with
   the previous modulation/blend (slots 7/5) restored on `Restore()`;
4. `IModelRender::ForcedMaterialOverride(material)`, then the original draw,
   then override + render-view state restored.

The material's own ColorModulate/AlphaModulate are no longer used (except the
one-time black diffuse of Fade Up at init) — that was the part that rendered
wrong before.

## Pass plan

`ChamsPasses::Build` is unchanged: visible passes, preceded by hidden passes
(same recipe, hidden colour, ignore-Z) when the group's X-ray is on. Glow is
the only composite: flat base (recipe 2, primary colour) + additive glow
(recipe 7, secondary colour). Preview draws visible passes only; live
rendering draws hidden then visible.

## ABI

CS:GO Win32 interface slots, cross-checked with the reference declarations
(see `docs/interface-verification.md`):
- IMaterial: FindVar 11, refcount 12/13, ColorModulate 28,
  Set/GetMaterialVarFlag 29/30, IsErrorMaterial 42.
- IMaterialVar: SetFloatValue 4, SetIntValue 5, SetVecValue 11.
- IVRenderView: SetBlend 4, GetBlend 5, SetColorModulation 6,
  GetColorModulation 7.
- IStudioRender: 33 = ForcedMaterialOverride (preview only, type 0 / -1).

Material names are the old project's (`csgo_soft` ... `csgo_fadeup`); each
instance is reference-counted once at creation and cached per recipe. A
failed creation logs and falls back to the original model (no debug-material
substitution).
