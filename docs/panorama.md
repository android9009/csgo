# Panorama (HUD JavaScript)

`sdk/panorama` wraps the two objects the client itself uses to run JavaScript inside the
HUD: `IPanoramaUIEngine` (from `panorama.dll`, interface `PanoramaUIEngine001`) and the
`UIEngine` it hands out. The only consumer today is the Misc tab's scoreboard equipment
row (`features/misc/general/scoreboard_script.cpp`).

## What is bound, and where the numbers come from

| Call | Vtable index |
|---|---|
| `IPanoramaUIEngine::AccessUIEngine` | 11 |
| `UIEngine::IsValidPanelPointer` | 36 |
| `UIEngine::GetLastTargetPanel` | 56 |
| `UIEngine::RunScript` | 113 |
| `IUIPanel::GetID` | 9 |
| `IUIPanel::GetParent` | 25 |

The indices come from public CS:GO sources that call them against the 32-bit client: the
Aviarita `lua-scripts` FFI binding (which loads `panorama.dll` and calls
`PanoramaUIEngine001`) and the unknowncheats thread on `GetLastDispatchedEventTargetPanel`.
Hand-written SDK headers (Fuzion, Skeletux) list the same functions one or two slots
apart, because Valve inserts vfuncs between builds.

**This is the one part of the codebase that cannot be verified offline and that the
signature scanner cannot check either**: a wrong index means calling the wrong game
function. Everything around it is therefore defensive:

- the `RunScript` slot is checked to live inside `panorama.dll`'s `.text` before use;
- every call (panel lookup, parent walk, script run) sits in its own `__try/__except`;
- the panel search is bounded (`kPanelSearchAttempts`, 30 × 500 ms) and stops with one
  log line instead of hammering a possibly wrong slot;
- three exceptions disable the feature for the session
  (`scoreboard: weapon reveal disabled for this session`) instead of retrying.

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
  `csgo-sb-equipment-<xuid>` panel per scoreboard row, hides the commendation/music-kit
  flair so the row fits, registers `OnOpenScoreboard`, `Scoreboard_UpdateEverything` and
  `Scoreboard_UpdateJob`, and exposes `__sb.feed` / `__sb.destroy`. A layout tag
  (`120x90x110`) makes a slider change reinstall instead of returning early.
- `BuildFeedScript(players, scales)` — the engine plus one `__sb.feed({...})` push. Every
  feed carries the installer, so a HUD reload re-creates the rows without any hook.

Data comes from the client's own entities (`m_hMyWeapons`, `m_iItemDefinitionIndex`,
`m_ArmorValue`, `m_bHasHelmet`, `m_bHasDefuser`) and is translated to icon names through
a compile-time table, so nothing user-controlled ever reaches the JavaScript.

Icons are the client's own (`file://{images}/icons/equipment/<name>.svg`), the same set
the reference Lua uses: console name without its `item_`/`weapon_` prefix.

### Slider mapping

| Slider | What it scales |
|---|---|
| Horizontal scale | equipment panel `min-width` (30 px base) and the per-weapon minimum widths (pistols 31 px, knives 45 px, grenades 9-12 px) |
| Vertical scale | armor / helmet / defuser icon height (17 / 22 / 17 px base) |
| Weapon scale | weapon icon `textureheight` (18 px base) |

All three clamp to 50..200 % and round up to at least one pixel, so no setting can
collapse a row.

## Restoring

`BuildResetScript()` runs `__sb.destroy()`, which unregisters the handlers, restores the
flair panels and deletes the injected rows. It runs when the feature is switched off and
when the layout changes. The two hidden flair panels are restored through their parent
before the row is deleted.
