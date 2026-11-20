# Visuals/Misc audit, November 2026

Scope: every control of `Visuals → Misc` (`visuals>misc>local`, `visuals>misc>other`,
`visuals>world`) and of `Misc → Movement → Air strafe`, with the path
`menu → config → publish → consumer in the game` written out for each one.

Two results are distinguished everywhere:

* **built** – the code compiles and the portable rules are covered by `tests/`;
* **verified in game** – only ever written down if the client was actually launched.

Nothing in this file is marked verified in game: no CS:GO client was available while these
changes were made. The manual checks that have to be run are listed at the end of every
section, and the values that can be pinned without the client (the zoom maths, the aspect
smoothing, the strafe modes, the jump gate) are covered by tests instead.

---

## 1. Third person

| Control | Where it goes now | Result |
| --- | --- | --- |
| `Thirdperson` | `VisualsMisc::Settings::thirdperson` → `Detail::Plan` → `CInput::camera_in_third_person` + `CInput::camera_offset` (`thirdperson.cpp`) | built |
| `Distance` | slider → `Settings::thirdperson_dist` → `PublishDistance` → **`cam_idealdist`** (written on every frame the slider changed, whatever the camera plan says) → the camera code reads that cvar back as the desired distance, clips it with a `MASK_NPCWORLDSTATIC` trace and writes only the clipped copy into `CInput::camera_offset.z` | built |
| `Force when dead` | `Settings::thirdperson_dead` → observer mode `4 → 5` while the local player is dead | built |
| `Disable on grenade` | `Settings::thirdperson_grenade` + `ctx.weapon_data->weapon_type == 9` → `Plan` returns an inactive plan, so the camera is handed back while a grenade is out and re-taken when it is put away | built |
| `Transparency` | `Settings::thirdperson_transparency` → `Detail::LocalModelAlpha` → the local model's `m_clrRender` alpha plus `m_nRenderMode = kRenderTransTexture`; the original pair is captured before the first write and restored when third person goes off | built |
| `+ with nades` | `Settings::thirdperson_transparency_nade`, used instead of `Transparency` while a grenade is out (0 keeps the base slider in charge) | built |
| `Disable animation` | **removed from the menu**; `cfg.misc.thirdperson_no_interp` is still parsed from old configs and now has no consumer at all | done |

`c_mindistance` / `c_maxdistance` are captured as soon as the ConVar interface exists -
`VisualsMisc::PrepareChaseCvars()` runs from `Hooks::Initialize`, i.e. at inject, and is
retried from the frame stage if the interface was not up yet - and widened to `0` / `99999`.
The same call pushes the saved slider value into `cam_idealdist`, so the cvar is already
correct before the first third person frame; the originals are handed back in `VisualsMisc::Shutdown()`, together with
`cam_idealdist`. Every write goes through the cvar's own `SetValue(float)` – no console
command is issued anywhere.

What is *not* done: the collision result is never written back into the slider or the cvar, so
the "walk into a wall, walk away" case is what the code is built for, but it has not been
seen in game yet.

Manual check: `150` on the slider, back into a wall, step away — the camera has to come back
out to `150`; drag the slider with third person already on — the picture has to follow while
dragging.

## 2. Fov changer, Aspect ratio, Viewmodel changer

| Control | Status |
| --- | --- |
| `Fov` | unchanged behaviour (player `m_iDefaultFOV` / `m_iFOV` / `m_iFOVStart`, captured default restored when the checkbox goes off) |
| `Aspect ratio` | the returned ratio is now smoothed **once per frame** (`Detail::SmoothAspectRatio`, exponential, `tau = 40 ms`, keyed on `Globals->framecount`), so the engine sees one number per frame instead of the slider's integer steps. `0` = engine value and returns immediately. Below `20%` the value blends from the engine's own ratio down to the floor instead of snapping, which removes the "nothing happens, then it jumps" edge at the bottom of the slider. `178` still means `1.78`. |
| `Remove zoom` | the zoom session lives in `Detail::ZoomSession`: it only accepts values the *game* wrote (a value equal to this module's own last write is ignored, which is what used to make the zoom creep towards a full removal), it keys on the weapon, and `Detail::ZoomFov` applies `Force second zoom` before `Remove zoom`. 40/90 at 0/50/100% gives 40/65/90 and a 120-frame test pins 50% at 65. Only the field of view is touched – the scope mask is a different feature. |
| `Force second zoom` | `Detail::ForcedFirstZoomFov`, unchanged rules, now fed from the session above |
| `Force viewmodel in scope` | **still not applied.** main clears `weapon_info->hide_viewmodel_in_zoom` at `+0x1BD`; in the supplied `client.dll` the only reader/writer of a byte at that displacement belongs to a different object, and this repo's `CCSweaponData` puts its own tail byte at `+0x1DD`. A wrong byte in live weapon state is worse than a dead checkbox, so it stays published-but-ignored until the field is pinned down (needs a `C_BaseViewModel`/weapon-info RE pass, or the `UpdatePostScreenEffects` hook that main also uses for scope removal). |
| `Viewmodel changer → FOV` | unchanged (`ClientMode::GetViewModelFOV`, slot 35) |
| `Viewmodel changer → X / Y / Z` | **new consumer**: written into the game's own `viewmodel_offset_x/y/z` cvars, which is where the client reads the viewmodel pose offset from. Originals captured once and restored when the block is switched off or the module unloads. The world camera and the world fov are not involved. |
| `Viewmodel changer → Roll / Yaw / Pitch` | **new consumer**: MinHook detour on `C_BaseViewModel::CalcViewModelView` (`sdk/hooks/viewhooks.cpp`, signature `calc_viewmodel_view` in the catalogue, resolved at runtime). The original writes the frame's base pose into `eye_pos`/`eye_angles`; the three sliders are added to the *angles* afterwards, so every frame starts from the game's own pose and nothing accumulates, and the world camera is not involved at all (it is the viewmodel that is placed with those angles). The detour's signature was verified in the supplied `client.dll`: `55 8B EC 83 EC 58 56 57` matches exactly once (RVA `0x1B7F80`), it takes `this` in `ecx`, three stack arguments and ends in `ret 0xC`. |
| `Disable landing bob` | **not applied, and it says so.** The bob is added *inside* `CalcViewModelView` from the player's own landing state, and the only way to separate it from the base pose would be to replicate the engine's bob curve. Branching on a guess would move the model for the wrong reasons, so the checkbox stays a no-op. |

Manual check: slowly drag `Aspect ratio` around `177…179` — the picture has to move
continuously; set `Remove zoom` to `50`, scope in and count a few seconds — the zoom must stay
half-removed; move `X` and turn the camera 90° — the weapon has to stay on the same side of the
screen.

## 3. Scope, crosshair, spread

| Control | Status |
| --- | --- |
| `Remove scope` (None / Static / Dynamic / Gradient …) | **implemented; needs a manual check.** The mask is dropped natively: a MinHook detour on the player's post-screen pass (`UpdatePostScreenEffects`, signature `update_postscreen_effects`, RVA `0x3FAEA0` in the supplied `client.dll`, unique match, plain `ret` epilogue so the thunk pops nothing) clears `m_bIsScoped` for the duration of the call and restores it immediately after — this is main's `remove_post_screen_effects` trick, and it is also why the pass is hooked rather than the mask being overdrawn. The hand-over is skipped (and the call forwarded untouched) when the object being processed is not the local player, so a mismatched `this` fails closed instead of corrupting state. The four lines are drawn by this module (`menu/overlays/screen_overlay.hpp`) with `gapPx = focalPx * tan(angle)`: Static is a fixed 6° gap, Dynamic is the weapon's live `m_fAccuracyPenalty`, both with the menu's colour, thickness (`Detail::ScopeThickness`, 5–100 % → 1.2–5 px), `Invert` and gradient fade. They vanish when the player is not scoped, and the whole overlay is fed by `VisualRuntime::PublishScreen` from `FRAMERENDER_START`, never by an entity read on the render thread. |
| `Bullet tracers` (Local / Enemy / Teammates, each with colour and Laser/Line style) | **implemented; needs a manual check.** `bullet_impact` arrives through the shared game-event listener, the shooter is resolved with `IEngineClient::GetPlayerForUserID` (slot 9, verified in the supplied `engine.dll` — its body calls slot 8 `GetPlayerInfo` and compares the struct's user id), and the record is projected once per frame at `FRAME_RENDER_START` with the same matrix the renderer is about to use. Start point is the shooter's eye, end point is the impact — one record per impact, i.e. one per shot for everything that fires a single bullet. The three categories have their own colour and style; both styles are drawn by this module (`Laser` is the thicker line, `Line` the thin one — the engine's beam interface is not verified for this build, and pretending it is would be a lie about what the picture is made of). |
| `Show impacts` | **implemented.** The local player's own `bullet_impact` positions become small boxes for four seconds, drawn in `impacts_color` with `impacts_color2` on the corners. |
| `Hitmarker` (World / Screen / Kibit) | **implemented.** `player_hurt` where the attacker is the local player: `World` marks the victim's position, `Screen` and `Kibit` mark the crosshair (Kibit larger), each with its own colour and its own duration slider. An unprojectable victim drops its `World` marker instead of drawing it in the wrong place. |
| `Penetration crosshair` | **not implemented.** It needs a penetration test (main's `penetration.cpp`, ~640 lines of surface/material/trace logic and its own offsets) to decide green vs red; a single trace hit is not the answer and was not faked. |
| `Force crosshair` | **not implemented.** The native crosshair is a HUD element whose paint routine has to be reached (or its visibility condition patched); drawing a copy was explicitly out of scope. |
| `Spread` | **implemented; needs a manual check.** A circle whose radius is `Detail::SpreadRadiusPixels` — the same `focalPx * tan(inaccuracy)` conversion as the dynamic scope gap, so the circle and the gap always agree — drawn by `screen_overlay.hpp` from colour 1 to colour 2 around the ring, with no circle at all below one pixel. Independent of `Remove scope`: it draws whether or not the mask is off. |

`Out of FOV` stays **not implemented** (it needs a per-player off-screen direction published
alongside the ESP frame; the ESP capture is where that belongs and it is not wired yet).
`Force crosshair` and `Penetration crosshair` stay **not implemented**: neither was replaced by a
placeholder. `Force crosshair` has to change the HUD's own crosshair visibility (drawing a copy
was explicitly rejected), and `Penetration crosshair` needs a real penetration test (surface
thickness and material, per weapon and per view) — a single trace hit is not that answer, and a
green dot that would be green anyway is worse than no dot.

## 4. Misc → Movement → Air strafe

Four items in the combo box, direction and permission kept apart:

| Item | Behaviour |
| --- | --- |
| `Viewangles` | the base direction is the live look yaw; the camera is only read, so a manual turn is followed and never compensated |
| `Movement keys` | the base direction is the physical key vector in the camera frame: `W+D` is the forward-right diagonal, `S` points backwards, opposite keys cancel, and no keys at all means "do nothing this tick" (no guessed direction, no leftover from the previous tick) |
| `Easy strafe` | eases the wish direction towards the look direction with `RotateTowardsYaw` (shortest arc, so `179 → -179` is a 2° turn) limited by the `Smoothing` slider (20°/tick at 0 … 2°/tick at 100); the state is dropped on landing and the camera is never written |
| `Jump release (while held)` | gate: the strafer only runs while the jump button is held |
| `Jump release (after release)` | gate: the strafer only runs after a real press-and-release in the air; a jump that was never let go, and a space bar that was never touched, both do not open it (`JumpGateState` clears its edges on landing) |

The gate is evaluated **before** anything writes into the command, including the early exit
for a player who is barely moving, and the wish is rotated into the command's own frame
instead of the command's angles being written. `cfg.misc.autostrafe` gained one bit
(`autostrafe_jump_release_after`, bit 4) so old configs keep their meaning: bit 3 is still
"jump release", and the new polarity is simply absent from older files.

Manual check (each item separately): jump, let go of the mouse — the trajectory may change,
the camera yaw must not; then keep turning the mouse — the strafer must not fight it.

## 5. Visuals → Misc → Other and Visuals → World

These are the two groups in `tab_visuals.cpp` (`visuals>misc>other`, `visuals>world`), not the
same-named `Misc → Other` of `tab_misc.cpp`.

| Control | Status |
| --- | --- |
| `Texture transparency` | works: material `$alpha` + `MATERIAL_VAR_TRANSLUCENT` from the recorded original (see `model_transparency.hpp`) |
| `Prop transparency` | works, same mechanism |
| `Tracers` (Local/Enemy/Teammates, colours, style) | **not implemented** – needs a `bullet_impact` / `weapon_fire` listener (the pattern exists in `shot_chams.cpp`) plus a screen-space line per shot; nothing is drawn today |
| `Hitmarker` (world/screen/kibit, duration) | **not implemented** – same event listener plus overlay drawing |
| `Show impacts` | **not implemented** |
| `Out of FOV` | **not implemented** |
| `Fog` | **new consumer**: `features/visuals/world_fog.hpp`, called at `FRAME_RENDER_START`. The colour from the menu is written into `fog_color` and `fog_override` is asserted to `1` (once per frame at most, and only when the cvar no longer holds the value this module wants, because the game re-applies the map's fog on every level change). The map's own density and start/end range are left alone, so the checkbox recolours the map's fog instead of replacing it. Both cvars are captured before the first write and handed back when the checkbox goes off, when there is no game running, and on unload. |
| `Nightmode`, `Fullbright`, `Sunlight`, `Sky`, `Vignette`, `Exposure`, `Bloom`, `Local contrast`, `Color correction`, `Depth of field`, `Weather` | **not implemented.** main implements these through four different mechanisms, none of which exists in this branch yet: world-texture modulation (a hooked material colour path, `world_modulation.cpp`), `DT_EnvTonemapController` netvars for bloom/exposure, the `fog_override` / `cl_csm_*` cvar families, and `load_named_sky` for the sky box. `Fog` is done through the cvar layer (above); the rest needs either the world-texture colour
modulation hook or the tonemap entity netvars or `load_named_sky`, none of which is part of
this branch yet. |

The two transparency sliders and `Fog` are the only members of these groups with a real
consumer today.
Everything else in the two groups is a live control with no effect, and this table is the
honest state of it.

## 6. Build and test status

* `g++ -std=c++20 -O1 -I. tests/*.cpp` – 22 translation units, all pass, including the new
  rules: `ZoomFov` ordering, 120-frame `Remove zoom` stability, the aspect smoothing and the
  low-end blending, `LocalModelAlpha`, `DesiredDistance`/`ClampedDistance`, the viewmodel
  offset clamp, and the four strafe modes with the jump gate.
* Syntax check (`/tmp/chk.sh`) for every changed translation unit: `thirdperson.cpp`,
  `fov.cpp`, `publish.cpp`, `autostrafe.cpp`, `movement.cpp`, `viewhooks.cpp`, `framestage.cpp`,
  `misc.hpp`, `movement.hpp`, `convars.hpp`, `entity.hpp`. The two new portable checks
  (`ScopeGapPixels`/`FocalPixels`/`ScopeThickness`/`ViewmodelAngle`) run inside
  `tests/visuals_misc_test.cpp`; `tests/pattern_catalog_test.cpp` now expects the two new
  catalogue entries (103 ported signatures). `tests/visuals_fx_test.cpp` covers the new
  projection path end to end: `ProjectPoint`/`ProjectSegment` (including the near-plane clip of
  a tracer that starts behind the camera), the three categories, both tracer styles, the record
  lifetimes and the pixel→fraction conversion.
* The MSVC project itself was not built here (no MSVC in this sandbox). New translation unit:
  `features/visuals/misc/effects.cpp` (tracers/impacts/hitmarkers) — added to
  `csgo_base.vcxproj` and its `.filters`; `CMakeLists.txt` globs sources and needs no edit.
* `g_aspect_failed` / the aspect thunk from `e581d2e` are untouched: the calling-convention fix
  stays exactly as it was.
