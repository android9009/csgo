# Misc runtime

The Misc tab was pure UI: `menu/tabs/tab_misc.cpp` built controls for `cfg.misc.*`,
and no code read those values. This module implements the subset the existing SDK and
hook set can support safely, and documents the rest instead of pretending it works.

Feature placement follows the menu, not the config struct: the first Misc group, the
clan tag and the radar switches live in `features/misc/`, while **removals live in the
Visuals tab** (`visuals>misc>other>removals`) and are therefore implemented under
`features/visuals/removals/`.

## Layout

One folder per menu group box, **one header for the whole folder**, one translation unit
per feature: `general/general.hpp` declares everything the folder exposes (the façade,
each feature's portable rule, and the script builders) and the `.cpp` files next to it
hold all the logic. A tab folder owns a small façade that only fans out; the root file
owns the game-thread entry point and the shared failure policy.

```
features/misc/
  misc.hpp/.cpp            entry point: guards, SEH, dispatch to group boxes
  general/                 first Misc group box ("MISC", id misc>general)
    general.hpp            the folder's single header: Settings, Publish/FrameStage/
                           Shutdown, and every portable rule below
    general.cpp            façade: fan out to radar, clan tag and scoreboard
    radar.cpp              radar reveal
    clantag.cpp            clan tag
    scoreboard.cpp         rank reveal + the equipment feed
    scoreboard_script.cpp  the injected Panorama JavaScript builders
  movement/                next phase (bhop, autostrafe, edge-*, ...)
  buy_bot.hpp/.cpp         next phase — single file, no folder

features/visuals/removals/
  removals.hpp/.cpp        façade + bit order of the Removals combo box
  flash.hpp/.cpp           bit 0 "Flashbang Effect"
  punch.hpp/.cpp           bit 8 "Visual punch"
```

`general.hpp` never includes Windows or engine types, so any part of it can be used by
the portable tests: the decision rule that gates each engine write is an `inline`
function there, and the `.cpp` only performs the write. Adding a feature means adding
`<name>.cpp` to the folder, declaring what it exposes in `general.hpp`, and adding one
line to the façade in `general.cpp`.

### Menu ids

`menu/tabs/tab_misc.cpp` used to mix two prefixes for one group: the group box was
`misc>various` while 22 child controls still used `misc>main>...`. The group id, the
builder (`menu::group::misc_general`) and every child id now say `misc>general>...`,
matching the folder that implements them; "various" was a placeholder that named
nothing in the menu.
The dead `cfg.misc.remove_flash` key was dropped as well - no control ever bound it,
because the flash switch is the first item of the Removals combo box.

## Implemented

| Feature | Config key | Stage | What it does |
|---|---|---|---|
| Radar | `cfg.misc.radar` | 4 (net update end) | sets `m_bSpotted` on living, non-dormant players of a playable team |
| Clan tag | `cfg.misc.clantag` | 0/4/5 | calls `SetClanTag` from `engine.dll` (signature `dwSetClanTag`) |
| Reveal ranks | `cfg.misc.scoreboard_options` bit 0 | 4 | calls the client's `ServerRankRevealAll` handler at most once per second |
| Reveal weapon | `cfg.misc.scoreboard_options` bit 1 | 4 | injects a Panorama script that draws the equipment row (see `docs/panorama.md`) |
| Remove flash | `cfg.misc.removals` bit 0 | 0/5 | zeroes `m_flFlashDuration` on the local player |
| Visual punch | `cfg.misc.removals` bit 8 | 0/5 | zeroes `m_aimPunchAngle` and `m_viewPunchAngle` on the local player |

Entry point: `Misc::FrameStage(stage)` and `Removals::FrameStage(stage)` from
`sdk/hooks/framestage.cpp`, after the context refresh, inside the existing SEH frame.

The Removals combo box stores a bitset, so `Removals::Bits` must stay in the same order
as the item list in `menu/tabs/tab_visuals.cpp`. `tests/removals_test.cpp` pins that
order: a silent reorder in the menu would otherwise toggle the wrong effect.

## Scoreboard

The Scoreboard combo box has three items; two are wired:

- **Reveal ranks** (bit 0) — the client ships a handler for the `ServerRankRevealAll`
  user message. Calling it makes the server send the skill-group table, and the native
  scoreboard then draws ranks for everybody it will ever show. It is called from
  `Misc::FrameStage` stage 4, at most once per second (`kRevealIntervalMs`): each call is
  a user message, and flooding one would be both useless and measurable. Once per
  session the log prints how many slots actually carry a skill group afterwards — the
  server decides what it fills in, so that line is the only honest confirmation.
- **Reveal weapon** (bit 1) — the vanilla scoreboard only shows equipment the server
  decided to send, and its equipment cells are sized for that. The feature injects
  JavaScript into the `CSGOHud` Panorama context, creates one panel per scoreboard row
  and fills it with the client's own equipment icons for every player. Data is read from
  the entities (`m_hMyWeapons`, armor, helmet, defuser) and pushed twice a second; the
  script re-installs itself on every push, so a HUD reload cannot leave it orphaned.
  The three sliders under the combo box drive the injected layout:
  `scoreboard_scale_x` (row and per-weapon widths), `scoreboard_scale_y` (armor and
  helmet icons) and `scoreboard_weapon_scale` (weapon icons). See `docs/panorama.md` for
  the vtable indices, the arity-safe call and the failure policy.
- **Reveal unmatched ranks** (bit 2) and the `scoreboard_unmatched_scale` slider — not
  implemented, by request.

`CCSPlayerResource` (`sdk/entity/player_resource.hpp`) exposes the per-slot rank and win
arrays through the `dwPlayerResource` global. It is used for the one-shot count above,
but it is the SDK piece any future scoreboard work needs.

## Threading and cost

- Everything runs on the game thread. No engine call is made from the D3D thread.
- Settings reach the game thread through per-feature atomics, published once per frame
  by `MiscSettings::Configure()` / `RemovalsSettings::Configure()`. The game thread
  never touches `evo::gui` objects.
- No allocation, no string formatting, no netvar table lookup per frame: netvar offsets
  are cached by the `NETVAR_FALLBACK` statics and the clan tag function pointer is
  resolved once.
- Radar walks at most 64 player slots and writes only when the flag is currently false,
  so a settled frame performs no writes.
- Punch angles are recomputed by the game every frame, so the removal is a per-frame
  zero, and only when a component is non-zero.
- The clan tag is re-applied at most every 5 seconds, or immediately when the tag, the
  local player entity (map change) or the enable state changes. Each `SetClanTag` call
  is an engine string update the server rate-limits, so a per-frame call would be
  useless and measurable.

## Failure behaviour

- `Misc::FrameStage` is wrapped in `__try/__except`; one stale entity pointer stops the
  whole runtime for the session (`g_failed`) and logs once instead of throwing every
  frame.
- `dwSetClanTag` missing from the catalogue → warning, feature inert; radar keeps
  working.
- The rank reveal signature is checked against the client's `.text` range before it is
  called, and the call itself sits in its own `__try/__except`: a stale pattern disables
  this one feature and logs once, instead of the whole Misc runtime.
- The Panorama path fails in three separated places - interface, panel search, script
  call - each with its own bound. A wrong vtable index disables the weapon reveal for the
  session after three exceptions; the rank reveal keeps working.
- Flash is cleared only for a positive duration, which also rejects NaN. Punch is
  zeroed when any component is non-zero, which includes NaN.
- The tag the player already owned is captured from `m_szClan` before the first write
  and restored when the feature is switched off or the DLL unloads, so toggling does not
  silently wipe a custom tag. If `m_szClan` cannot be read, the restore writes an empty
  tag (what every such feature does by default).

## Not implemented, and why

These controls stay inert. Each needs a subsystem this base does not have, so wiring
them would only produce dead toggles:

- **Logs, hitsounds, indicators** - need a shot/hit pipeline (ragebot + event tracking);
  to be implemented together with the ragebot.
- **Scoreboard "Reveal unmatched ranks"** - needs the engine's own rank lookup for players
  the server never sent a skill group for; not implemented by request.
- **Reveal enemy chat, console filter** - need a `SayText2` / `ConMsg` hook, i.e. a new
  vtable or engine detour the base deliberately does not install.
- **Removals 1-7 and 9** (smoke, particles, post processing, water, blood, decals,
  ragdolls, shadows) - material, effect-dispatch and entity-visibility manipulation;
  each needs its own validated hook, and a wrong one is a visible or crashing change.
- **Ragdoll thrust, ping spike** - physics and netchannel manipulation; gameplay-visible
  and deserving more validation than string signatures provide.
- **sv_pure / sv_cheats bypass** - signature-dependent patches with a crash surface
  larger than the feature is worth.
- **Auto-accept** - lives in Panorama; there is no supported hook for it here.
- **Movement and Buybot** - next phases. Movement needs a `CreateMove` detour, which
  `sdk/hooks/detours.cpp` intentionally leaves empty; Buybot needs the convar /
  `ClientCmd` path.

## Checks

Portable tests:

- `tests/misc_radar_test.cpp` - `ShouldSpot` for dormant, dead, local, spectator and
  wrong-team targets.
- `tests/misc_clantag_test.cpp` - `ShouldApply` transitions (first enable, steady state,
  tag change, new local entity, interval, `GetTickCount` wraparound, disabled, no local
  player, null pointers), `ShouldRestore` exactly-once semantics, and the tag length.
- `tests/removals_test.cpp` - the combo box bit order, `FromMask`, and the flash/punch
  write rules including NaN and infinity.
- `tests/scoreboard_test.cpp` - the Scoreboard combo box bit order, the rank reveal gate
  (disabled, not in game, zero interval, first send, inside the interval, and the
  `GetTickCount` wrap) and the bounded panel-search gate.
- `tests/scoreboard_script_test.cpp` - scale clamping and the scaled layout/precision
  values, the icon table, and the feed payload: no player without equipment, no slot 0,
  balanced braces, self-contained engine, and icon names restricted to plain identifiers
  (which is what makes injection through the payload impossible).

The engine-facing code is guarded but cannot be verified in a live process here: check
in game that the log has no `misc: frame-stage exception` and that the tag, radar and
removals behave.
