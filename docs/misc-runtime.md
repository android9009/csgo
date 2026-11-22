# Misc runtime

> **2026-11-22: the misc logic is removed.** Everything the Misc tab and the MISC group of
> the Visuals tab did in the game is gone:
>
> - `features/misc/` — radar reveal, clan tag, reveal enemy chat, ping spike, auto-accept,
>   console filter, the sv_pure / sv_cheats switches, the whole MOVEMENT group (auto strafe,
>   bhop, quick stop, edge jump / edge stop) and the buy bot;
> - `features/visuals/misc/` — third person, the fov changer ("Remove zoom", "Force second
>   zoom"), the aspect-ratio override, the viewmodel fov, the viewmodel position/rotation
>   sliders, "Remove scope" (the UpdatePostScreenEffects detour), "Force crosshair" and the
>   event-driven effects (tracers, impacts, hitmarker, out of FOV);
> - `sdk/hooks/createmove.cpp` — the CreateMove hook existed only for movement and the buy
>   bot, so it is gone with them.
>
> The menu, the config and the runtime state are untouched: `menu/tabs/tab_misc.cpp`, the
> MISC group of `menu/tabs/tab_visuals.cpp`, `menu/config.hpp`, the bridges
> (`menu/bridges/misc_runtime.hpp`, `menu/bridges/visual_runtime.hpp`) and
> `sdk/visual_runtime_state.hpp` all keep their controls and settings. The feature headers
> that the bridges include (`features/misc/{general,buybot,movement,other}/*.hpp` and
> `features/visuals/misc/misc.hpp`) now carry only the `Settings` structs and a no-op
> `Publish`, so the published values are accepted and dropped. The scope/spread geometry the
> menu's own overlay draws with (`VisualsMisc::Detail`) and the fx data model
> (`features/visuals/misc/fx.hpp`) stay, because the menu still uses them.

## What this means at runtime

- The Misc tab and the MISC group of Visuals render as before; toggling a control only
  changes the config value (and what the JSON config stores/loads).
- Nothing in the game reads those values anymore, so the controls have no in-game effect.
- The CreateMove / GetViewModelFOV / GetScreenAspectRatio vtable hooks and the
  UpdatePostScreenEffects / CalcViewModelView detours are no longer installed.
- The game-event listener (tracers / hitmarker) is no longer set up.
- The hotkeys that only toggled misc settings (for example F3 for bhop) keep working as
  config toggles.

## Where the old code went

The implementation files and their portable rules were deleted, along with the tests that
covered them (`tests/{buybot,misc_clantag,misc_hooks,misc_radar,movement,visuals_misc}_test.cpp`).
`tests/visuals_fx_test.cpp` stays: `fx.hpp` is still the data model the menu overlay is built
on. The signature catalogue (`utils/patterns/catalog.hpp`) intentionally keeps the entries the
removed features used (`update_postscreen_effects`, `calc_viewmodel_view`, `game_rules`, …) —
it is a reference catalogue, and `tests/pattern_catalog_test.cpp` validates every entry.
