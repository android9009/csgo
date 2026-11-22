# Misc runtime

> **2026-11-22: the misc features are gone, completely.**
>
> - `features/misc/` — removed in full: radar reveal, clan tag, reveal enemy chat,
>   ping spike, auto-accept, console filter, the sv_pure / sv_cheats switches, the whole
>   MOVEMENT group (auto strafe, bhop, quick stop, edge jump / edge stop) and the buy bot.
>   No files of the module remain (neither the `.cpp` implementations nor the Settings
>   headers).
> - `features/visuals/misc/` — removed in full: third person, the fov changer ("Remove
>   zoom", "Force second zoom"), the aspect-ratio override, the viewmodel fov, the viewmodel
>   position/rotation sliders, "Remove scope", "Force crosshair", the event-driven effects
>   (tracers, impacts, hitmarker, out of FOV) and the fx data model (`fx.hpp`).
> - `sdk/hooks/createmove.cpp` — the CreateMove hook existed only for movement and the buy
>   bot, so it is gone with them; the GetViewModelFOV / GetScreenAspectRatio vtable hooks and
>   the UpdatePostScreenEffects / CalcViewModelView / weapon_debug_spread_show detours are no
>   longer installed, and the game-event listener is no longer set up.
> - `menu/bridges/misc_runtime.hpp` — the menu bridge that published the Misc tab settings
>   is removed; `VisualSettings::Configure()` no longer publishes the misc/visuals-misc
>   blocks either, so the runtime state carries no misc settings at all.
> - `lua-patches/` (the bundled Skeet / Neverlose patch scripts) and `tests/lua/` (their
>   lupa harness) are removed from the repository.
>
> The menu and the config are untouched: `menu/tabs/tab_misc.cpp`, the MISC group of
> `menu/tabs/tab_visuals.cpp`, `menu/config.hpp` and the JSON save/load keep every control
> and setting. Toggling a control still changes the config value; nothing in the game reads
> it anymore.

## What this means at runtime

- The Misc tab and the MISC group of Visuals render as before; the controls are inert.
- `menu/overlays/screen_overlay.hpp` keeps drawing the "Remove scope" lines and the
  "Spread" circle (plain menu drawing now — the scope-mask detour is gone). Its geometry,
  formerly `VisualsMisc::Detail`, is inlined into the overlay file itself.
- `menu/overlays/fx_overlay.hpp` keeps compiling against `VisualRuntime::fx_frame`, which
  now simply stays empty.
- The hotkeys that only toggled misc settings (for example F3 for bhop) keep working as
  config toggles.

## What was deleted, in full

- All `features/misc/**` and `features/visuals/misc/**` files.
- `sdk/hooks/createmove.cpp`.
- `menu/bridges/misc_runtime.hpp`.
- `tests/{buybot,misc_clantag,misc_hooks,misc_radar,movement,visuals_misc,visuals_fx}_test.cpp`.
- `lua-patches/{nl_all_patches.lua,skeet_all_patches.lua,README.md}`,
  `tests/lua/{run.py,run_all.lua,test_nl.lua,test_skeet.lua,mock/harness.lua}` and the
  `.tmp_hooks.lua` scratch file.
- Project entries in `csgo_base.vcxproj` / `csgo_base.vcxproj.filters`.

The signature catalogue (`utils/patterns/catalog.hpp`) intentionally keeps the entries the
removed features used (`update_postscreen_effects`, `calc_viewmodel_view`, `game_rules`, …)
— it is a reference catalogue, and `tests/pattern_catalog_test.cpp` validates every entry.
