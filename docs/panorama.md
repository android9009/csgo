# Panorama (HUD JavaScript)

`sdk/panorama` wraps the two objects the client itself uses to run JavaScript inside the
HUD: `IPanoramaUIEngine` (from `panorama.dll`, interface `PanoramaUIEngine001`) and the
`UIEngine` it hands out. The only consumer today is the Misc tab's scoreboard equipment
row (`features/misc/general/scoreboard_script.cpp`).

## What is bound, and where the numbers come from

| Call | Vtable index |
|---|---|
| `IPanoramaUIEngine::AccessUIEngine` | 11 |
| `UIEngine::GetLastTargetPanel` | 56 |
| `UIEngine::RunScript` | 113 |

Nothing else is bound. `IUIPanel::GetID`/`GetParent`/`SetID` and
`UIEngine::IsValidPanelPointer` are deliberately absent: the public sources that agree on
11/56/113 disagree by one or two slots exactly in that region, and the neighbouring slots
are *mutating* calls (`SetID(uint64, const char*)`, `PanelDestroyed`) - a "GetID" that is
really `SetID` writes a garbage string pointer into the panel. The HUD panel is therefore
identified from JavaScript (`panel.id`, `panel.GetParent()`), which the engine handles and
which cannot desync from the build.

The indices come from public CS:GO sources that call them against the 32-bit client: the
Aviarita `lua-scripts` FFI binding (which loads `panorama.dll` and calls
`PanoramaUIEngine001`) and the unknowncheats thread on `GetLastDispatchedEventTargetPanel`.
Hand-written SDK headers (Fuzion, Skeletux) list the same functions one or two slots
apart, because Valve inserts vfuncs between builds.

**This is the one part of the codebase that cannot be verified offline and that the
signature scanner cannot check either**: a wrong index means calling the wrong game
function. Everything around it is therefore defensive:

- the `RunScript` slot is checked to live inside `panorama.dll`'s `.text` before use;
- each of the three calls sits in its own `__try/__except`;
- three exceptions disable the feature for the session
  (`scoreboard: weapon reveal disabled for this session`) instead of retrying;
- every step logs *before* it is performed (`panorama step 1/3 - AccessUIEngine`,
  `2/3 - GetLastTargetPanel`, `3/3 - RunScript (N bytes)`), so if a client build disagrees
  with an index, the last line of `csgo_base.log` names the call that did it.

### When nothing happens

The feature can stop at four points, and each one has its own line in `csgo_base.log`:

| Log | Meaning |
|---|---|
| `interfaces: PanoramaUIEngine001 not captured` | `panorama.dll` not loaded or renamed |
| `panorama: RunScript slot 113 (...) is outside panorama.dll` | vtable layout differs from the sources |
| `scoreboard: panorama step 2/3 - no panel yet` | the HUD has not dispatched a UI event yet - open the scoreboard once |
| `csgo-base: no scoreboard rows in this Panorama context` (game console) | the script ran, but in a context without a scoreboard |

The injected script also talks to the game console: `csgo-base: equipment engine installed
(layout 100x100x100, context "CSGOHud", 25 slots)` on install,
`csgo-base: equipment attached to N of M scoreboard row(s), keyed by xuid` when it starts
drawing, and `csgo-base: no scoreboard found from context "<id>"` when the context has no
scoreboard in it at all.

If the log shows `panorama: RunScript slot 113 (...) is outside panorama.dll`, the client
build has a different vtable layout and the index has to be re-derived.

## RunScript arity

Public sources disagree on whether the CS:GO `RunScript` takes six or seven parameters
after `this`. The call goes through an ESP-restoring thunk (`sdk/panorama/panorama.cpp`):

```
mov saved, esp
push ... (seven arguments, right to left)
mov ecx, engine
mov eax, g_run_script
call eax
mov esp, saved
```

Pushing one extra argument is harmless for a callee that ignores it, and restoring `ESP`
afterwards undoes whatever the callee popped, so neither arity can unbalance the stack.

## Finding the HUD panel

`GetLastTargetPanel` returns the panel that received the last dispatched UI event, so the
walk up to `CSGOHud` only works after the game has dispatched something (opening the
scoreboard is enough). When it returns null the feature stays idle and says so once:

```
scoreboard: CSGOHud panel not found after 30 attempts; weapon reveal idle ...
```

A map change or HUD reload invalidates the pointer; it is re-validated on every feed with
`IsValidPanelPointer` and re-acquired when it fails.

## The injected script

The architecture is a port of **velocity** (`wubly/velocity`,
`cs2/velocity-cs2/project/core/features/misc/impl/scoreboard_weapons.cpp`), which is the
reference for this feature on the current client generation: inject **one** installer that
defines a tiny message-passing client (`SClient.receive({type, content})` with registered
handlers) and a weapon manager, then send small per-player messages afterwards instead of
re-injecting the whole script every frame. Two builders remain, in
`features/misc/general/scoreboard_script.cpp`:

- `BuildEngineScript(scales)` — the installer. It defines `SClient` and `SWeaponManager`,
  registers `updateWeapons`/`clearWeapons`, registers the three CS:GO scoreboard events
  (`OnOpenScoreboard`, `Scoreboard_UpdateEverything`, `Scoreboard_UpdateJob`) so a rebuilt
  row redraws from the stored state, and bakes the sliders into `CFG` (icon 16 px, armour
  17 px, helmet 22 px, money label 13 px, panel `min-width` 30 px, all scaled). A layout tag
  is not needed for reinstalling any more: the geometry is read from `CFG` on every draw.
  Reinstalling unregisters the previous handlers first, so nothing stacks.
- `BuildUpdateScript(player, scales)` / `BuildClearScript(xuid, nickname)` — one message per
  player: `SClient.receive({type:"updateWeapons",content:{xuid,nickname,weapons:[{path,type}],
  active,armor,defuser,money}})`, and the explicit `clearWeapons` for a row that lost its
  equipment. Both are guarded with `typeof SClient !== "undefined"`, so sending before the
  installer is harmless.

The row itself is **CS:GO's**, not CS2's: velocity looks rows up by `player-<xuid>` / `id-<xuid>`
panel ids, which do not exist in this client, so the gamesense Lua's approach is used —
`FindChildrenWithClassTraverse("sb-row")`, matched by the row's own `m_xuid` (the steam id,
now read natively from `player_info_t` +0x8) and, for bots which have no xuid, by the
nickname label. The container is created in the `data-stat="name"` cell and moved before the
cell's second child, because an appended panel is clipped by the row's width.

What comes from velocity and is now part of this build:

| Reference behaviour | Effect |
|---|---|
| installer once + `SClient.receive` messages | the 13 KB script is injected once instead of every 500 ms; an unchanged loadout costs nothing |
| native cache keyed by steam id, skip identical state | no message unless the loadout actually changed (the previous version resent everything) |
| explicit `clearWeapons` when a player has nothing | a dead or disconnected player's icons disappear instead of going stale |
| icon width by weapon class (`sizeFor`) | rifles 52 px, `mac10` 30 px, pistols 30–42 px, grenades 14–16 px, knife 16 px, bomb 22 px, taser 18 px |
| `scaling = "stretch-aspect-preserve"` | icons keep their aspect ratio inside their width |
| container with `box-bg`/`box-border`/`box-content` (rgba background, 1 px border, `fit-children`) | the icons sit in a box that reads as part of the row |
| `RemoveAndDeleteChildren()` then rebuild | no stale children when a loadout shrinks |
| active weapon at opacity 1.0, the rest 0.35 | the same emphasis the reference uses |

Everything else - the slider mapping, armour/helmet/defuser icons, the money label, the
flair-hiding ids - is the gamesense Lua port described below.

### Slider mapping

| Slider | What it scales |
|---|---|
| Horizontal scale | container `min-width` (30 px base) and every icon width (velocity's table) |
| Vertical scale | armour / helmet icon height (17 / 22 px base) and the money label (13 px base) |
| Weapon scale | weapon icon height (16 px base) |

All three clamp to 50..200 % and round up to at least one pixel, so no setting can collapse
a row.

### Where the native side sends from

`scoreboard.cpp` runs the whole thing from `FrameStage` stage 4: compare the collected
equipment with the last state sent per steam id every `kFeedIntervalMs` (100 ms, the same
rate the reference uses), send only what changed, send `clearWeapons` for rows that vanished,
and re-send the installer every `kReinstallMs` (10 s) so a HUD reload recovers by itself.

## Restoring

`BuildResetScript()` runs `__sb.destroy()`, which unregisters the handlers, restores the
flair panels and deletes the injected rows. It runs when the feature is switched off and
when the layout changes. The two hidden flair panels are restored through their parent
before the row is deleted.
