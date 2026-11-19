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
(layout 100x100x100)` on install and `csgo-base: equipment engine attached to N scoreboard
row(s)` when it starts drawing.

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
  `Scoreboard_UpdateJob`, and exposes `__sb.feed` / `__sb.destroy`. The scoreboard is
  located from the script's own context panel: `FindChildTraverse` in that subtree first,
  then up to 32 levels towards `CSGOHud` - so any HUD panel works as a script context. A layout tag
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

"Reveal money" uses the same row: a `Label` named `money` in the injected panel, filled
from `m_iAccount` when a plausible value was read (`entry.m` in the payload). When the
prop cannot be found the label simply never appears - the feature does not guess offsets.

All three clamp to 50..200 % and round up to at least one pixel, so no setting can
collapse a row.

## Restoring

`BuildResetScript()` runs `__sb.destroy()`, which unregisters the handlers, restores the
flair panels and deletes the injected rows. It runs when the feature is switched off and
when the layout changes. The two hidden flair panels are restored through their parent
before the row is deleted.
