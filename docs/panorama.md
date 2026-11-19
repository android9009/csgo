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

Two builders, declared in `features/misc/general/general.hpp` and defined in
`features/misc/general/scoreboard_script.cpp`:

- `BuildEngineScript(scales)` — idempotent installer for the HUD context. It creates one
  `csgo-sb-equipment-<key>` panel per scoreboard row, hides the commendation/music-kit
  flair so the row fits, registers `OnOpenScoreboard`, `Scoreboard_UpdateEverything` and
  `Scoreboard_UpdateJob`, and exposes `__sb.feed` / `__sb.destroy`. A layout tag
  (`120x90x110`) makes a slider change reinstall instead of returning early.
- `BuildFeedScript(players, scales)` — the engine plus one `__sb.feed({...})` push. Every
  feed carries the installer, so a HUD reload re-creates the rows without any hook.

The nickname in the payload is read from `player_info_t` (+0x10) into a fixed 64-byte field
of the `Equipment` entry - **by value**, because `player_info_t` itself lives on the stack of
the collecting loop. An earlier version stored a pointer into it, and the payload builder
then walked a dangling pointer: the string grew until the allocator threw, which surfaced as
a `0xE06D7363` crash a few frames after the scoreboard script was enabled. `Quote()` is
bounded for the same reason.

Data comes from the client's own entities (`m_hMyWeapons`, `m_iItemDefinitionIndex`,
`m_ArmorValue`, `m_bHasHelmet`, `m_bHasDefuser`) and is translated to icon names through
a compile-time table, so nothing user-controlled ever reaches the JavaScript.

### What the injection copies from the reference Lua

The layout is a port of the gamesense **"Scoreboard equipment" Lua**
(`fakeangle/gamesense_workshop_dump`), which is the only published implementation of this
feature for the final 2023 client. The parts that matter and that this build now matches:

| Reference behaviour | Why it matters |
|---|---|
| `GetContextPanel().FindChildTraverse("ScoreboardContainer").FindChildTraverse("Scoreboard")` (fallback `id-eom-scoreboard-container`) | the scoreboard is looked up from the HUD root, not from whatever panel the script happens to run in; the fallback covers the end-of-match scoreboard |
| rows via `FindChildrenWithClassTraverse("sb-row")`, keyed by the row's `m_xuid` | row panels are recreated by the game, so they cannot be held; the key survives |
| name cell found through `data-stat="name"`, anchor `GetChild(0)` | the row's column order is the client's, not this code's |
| `parent.MoveChildBefore(panel, parent.GetChild(1))` | **appended** panels land behind the stat columns and are clipped away - this is why the row did not appear before |
| 25 slot parents, each with four size classes (`smaller` 15px, `small` 17px, `medium` 18px, `large` 21px) | knives need the wide class, grenades the narrow one; one class for everything makes icons look wrong or misaligned |
| `SLOT_OVERRIDE` (knives -> `smaller`), `MIN_WIDTHS`/`MAX_WIDTHS` (pistols 31px, knives 45px, grenades 9-12px, flashbang capped at 12px) | the reference's own table, scaled by the sliders |
| team wash (`TEAM_COLORS`) and shadow (`SHADOW_COLORS`) per `GameStateAPI.GetPlayerTeamName`, opacity 0.85 selected / 0.35 rest, ±5px icon margins and the per-weapon margin fixes | what makes the icons read as part of the row |
| armour as `kevlar.svg` plus a separate `helmet.svg`, never one combined icon | the client's icon set has no combined texture |
| hide `id-sb-name__commendations__*` and `id-sb-name__musickit`, tighten `id-sb-skillgroup-image` | the space the icons occupy comes from flair the row does not need |

Two behaviours are this build's own: the payload also carries the nickname, so a context
where `GameStateAPI` is out of reach still matches rows (`keyed by nickname`), and the
reveal-money label lives in the same panel.

Icons are the client's own (`file://{images}/icons/equipment/<name>.svg`), the same set
the reference Lua uses: console name without its `item_`/`weapon_` prefix.

### Slider mapping

| Slider | What it scales |
|---|---|
| Horizontal scale | equipment panel `min-width` (30 px base) and the per-weapon minimum widths (pistols 31 px, knives 45 px, grenades 9-12 px) |
| Vertical scale | armor / helmet / defuser icon height (17 / 22 / 17 px base) |
| Weapon scale | the four icon size classes (15 / 17 / 18 / 21 px base) |

"Reveal money" uses the same row: a `Label` named `money` in the injected panel, filled
from `m_iAccount` when a plausible value was read (`entry.m` in the payload). When the
prop cannot be found the label simply never appears - the feature does not guess offsets.

All three clamp to 50..200 % and round up to at least one pixel, so no setting can
collapse a row.

## Testing the script without the game

`tests/panorama/run.sh` compiles a dumper against the real `BuildEngineScript`/`BuildFeedScript`
and runs the result under node against a fake Panorama panel tree (`tests/panorama/sim.js`):
`CSGOHud > ScoreboardContainer > Scoreboard > sb-row > data-stat="name"`. It asserts that a
feed creates the panels, that the panel lands in the first column, that the icons, size
classes, opacities, team wash, armour, helmet and money come out right, that a player
without equipment is hidden again, and that rows keyed only by nickname still match. This
covers the part that has no error reporting of its own: when the injected script does
nothing in game, nothing anywhere says why.

## Restoring

`BuildResetScript()` runs `__sb.destroy()`, which unregisters the handlers, restores the
flair panels and deletes the injected rows. It runs when the feature is switched off and
when the layout changes. The two hidden flair panels are restored through their parent
before the row is deleted.
