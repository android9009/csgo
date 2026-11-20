# Misc runtime


> **November 2026 rework.** The third person camera distance is now driven by the chase
> camera's own cvars (`cam_idealdist` written from the `Distance` slider, `c_mindistance` /
> `c_maxdistance` widened once and restored on unload), the collision-clipped distance is
> never written back, `Transparency` / `+ with nades` write the local model's colour alpha and
> render mode, the aspect ratio is smoothed once per rendered frame, the zoom session refuses
> to read this module's own writes, and `Viewmodel changer → X/Y/Z` writes
> `viewmodel_offset_x/y/z`. `Disable animation` is gone from the menu. See
> `docs/visuals-audit.md` for the per-control status, including what is still not applied.


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
    general.cpp            façade: fan out to radar, clan tag, chat and ping
    radar.cpp              radar reveal
    clantag.cpp            clan tag
    chat.cpp               reveal enemy chat (DispatchUserMessage hook)
    ping.cpp               ping spike (SendDatagram / ProcessPacket hooks)
  other/                   second Misc group box ("OTHER", id misc>other)
    other.hpp              the folder's single header
    other.cpp              façade: fan out to auto-accept and the console filter
    accept.cpp             auto-accept (EmitSound1 hook + SetLocalPlayerReady)
    console_filter.cpp     console filter (tier0 ConMsg / ConColorMsg hooks)
  movement/                MOVEMENT group box (id misc>movement)
    movement.hpp           the folder's single header: Settings and every portable rule
    movement.cpp           façade + the per-tick collection, called from CreateMove
    autostrafe.cpp         "Air strafe" (port of main's c_movement::auto_strafe)
    bhop.cpp               "Bhop" (port of main's c_movement::auto_jump)
    quickstop.cpp          "Quick stop" (port of main's fast_stop/instant_stop)
    edge.cpp               "Edge jump" / "Edge stop" (this build's own trace forecast)
  buybot/                  BUY BOT group box (id misc>buybot)
    buybot.hpp             the folder's single header: Settings, weapon table, scheduler
    buybot.cpp             game-rules lookup, client command, once-per-round latch

features/visuals/misc/     MISC group of the Visuals tab (visuals>misc>local)
  misc.hpp                 the folder's single header: Settings, camera, fov, zoom and
                           aspect rules
  publish.cpp              published settings (atomics) - named publish, not misc, because
                           features/misc/misc.cpp already owns that file name (MSB8027)
  thirdperson.cpp          "Thirdperson" - writes CInput's chase camera
  fov.cpp                  "Fov changer" + "Remove zoom" + "Force second zoom" - player fov
                           fields; "Force viewmodel in scope" is published but not applied
                           (unverified weapon-info flag); the aspect ratio and viewmodel fov
                           detours live in sdk/hooks/viewhooks.cpp

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
| Remove flash | `cfg.misc.removals` bit 0 | 0/5 | zeroes `m_flFlashDuration` on the local player |
| Visual punch | `cfg.misc.removals` bit 8 | 0/5 | zeroes `m_aimPunchAngle` and `m_viewPunchAngle` on the local player |
| Reveal enemy chat | `cfg.misc.reveal_enemy_chat` | hook | prints the enemy team chat the client received but refuses to draw, in the game's own format |
| Ping spike | `cfg.misc.ping_spike_enabled` + `cfg.aim.ping_spike_amt` | hook | lowers the incoming sequence number so the server measures a higher ping |
| Auto-accept | `cfg.misc.autoaccept` | hook | accepts the match when the accept popup beeps |
| Console filter | `cfg.misc.console_filter` | hook | drops five documented connection/queue lines from the console |
| Buy bot | `cfg.misc.buybot_*` | CreateMove (tick) | during freeze time, sends one `buy X; buy Y; ...` line through `IEngineClient::ClientCmd_Unrestricted`; up to three attempts per round |
| Third person | `cfg.misc.thirdperson` (+ `_dist`, `_dead`, `_grenade`) | FRAME_RENDER_START + CInput | sets `CInput::camera_in_third_person` and `camera_offset = { pitch, yaw, distance }`; the distance is clipped by the same `MASK_NPCWORLDSTATIC` +-16 unit trace `main` uses; "Force when dead" flips observer mode 4 -> 5 |
| Fov changer | `cfg.misc.fov_enabled`, `cfg.misc.fov` | FRAME_RENDER_START | writes the player's `m_iDefaultFOV` / `m_iFOV` / `m_iFOVStart`, restores the captured original when switched off or while scoped |
| Remove zoom | `cfg.misc.fov_scope` | FRAME_RENDER_START | while scoped, interpolates between the zoom field of view the game wrote and the world fov: 0% keeps the zoom, 100% removes it |
| Force second zoom | `cfg.misc.force_second_zoom` | FRAME_RENDER_START | keeps the first zoom level's field of view when the weapon zooms further |
| Force viewmodel in scope | `cfg.misc.force_viewmodel_scope` | - | **not applied**: main clears `weapon_info->hide_viewmodel_in_zoom` at `+0x1BD`; in the supplied `client.dll` the only reader/writer of that displacement belongs to another object, and this repo's `CCSWeaponData` puts its own tail byte at `+0x1DD`, so neither the object nor the offset is proven |
| Aspect ratio | `cfg.misc.aspect` | IEngineClient `GetScreenAspectRatio` (slot 101) | returns `aspect / 100.f` (main's detour); 0 keeps the engine value |
| Viewmodel fov | `cfg.misc.vm_fov_enabled`, `cfg.misc.vm_fov` | GetViewModelFOV (slot 35) | replaces the viewmodel field of view with the slider value |

Entry point: `Misc::FrameStage(stage)` and `Removals::FrameStage(stage)` from
`sdk/hooks/framestage.cpp`, after the context refresh, inside the existing SEH frame.

The Removals combo box stores a bitset, so `Removals::Bits` must stay in the same order
as the item list in `menu/tabs/tab_visuals.cpp`. `tests/removals_test.cpp` pins that
order: a silent reorder in the menu would otherwise toggle the wrong effect.

## Hooks

The three Misc features that cannot be driven from `FrameStageNotify` install their own
vtable or export hooks, and every index below is copied from a public source rather than
guessed; the engine numbering matches `FrameStageNotify` 37, which this base already
hooks. `Publish` only sets the switch; the patch itself is installed from the game thread
on the first frame after the switch was turned on, so no code is rewritten while the game
thread may be executing inside the function being hooked.

| Hook | Slot / export | Gated by | Source |
|---|---|---|---|
| `IBaseClientDLL::DispatchUserMessage` | vtable 38 | `cfg.misc.reveal_enemy_chat` | CSGOSimple `hooks.hpp`, UC 365807 |
| `IEngineSoundClient::EmitSound1` | vtable 5 | `cfg.misc.autoaccept` | CSGOSimple `hooks.hpp` |
| `SetLocalPlayerReady` | `client.dll` signature `set_local_player_ready` | `cfg.misc.autoaccept` | CSGOSimple `misc.cpp` |
| `tier0!ConMsg`, `tier0!ConColorMsg`, `tier0!Msg` | DLL exports, decorated names first (`?ConMsg@@YAXPBDZZ`, `?ConColorMsg@@YAXABVColor@@PBDZZ`) | `cfg.misc.console_filter` | tier0 export table |
| `CNetChan::SendDatagram` | `engine.dll` signature `send_datagram` / `send_datagram_alt` | `cfg.misc.ping_spike_enabled` | `main`: `offsets.cpp`, `detour_hooks.cpp` |
| `CNetChan::ProcessPacket` | `engine.dll` signature `process_packet` (already in the ported block) | `cfg.misc.ping_spike_enabled` | `main`: `detour_hooks.cpp` |

**Reveal enemy chat** (`general/chat.cpp`) never modifies the message. The engine already
delivers team chat to every client - that is why the client carries `cl_mute_enemy_team` to
hide it - so the feature reads the `CS_UM_SayText2` (6) payload, decodes the four fields it
needs (`ent_idx`, `chat`, `msg_name`, `params[]`) and prints the lines the HUD refused to
draw. The provenance is the gamesense **"Enemy team chat revealer" Lua**: keep only lines
whose sender is an enemy, remember when a player last spoke and drop a repeat inside
0.4 s (`ChatGate`, `ShouldAnnounce`), print the line itself. Nothing is written to game
memory, so there is no read object to repoint and no payload to rebuild.

Two things had to be solved to read the payload at all. The hook signature is
`(int msg_type, int data_bytes, bf_read& msg)`, so the third argument is a read object, not
a buffer: the payload pointer is found by validation - every machine word of the object is
tried and kept only when the buffer it points at starts with a length-prefixed
`Cstrike_Chat_` name - which also logs the field it settled on
(`bf_read payload field +0x..`). Only the first nine words are examined; everything past
the real object belongs to the caller. The protobuf decode is bounded by `data_bytes`, so a
different layout yields "not a chat message" rather than a read past the end.

The line is printed through tier0's own `ConColorMsg`/`Msg` exports, so it appears in the
console (`[chat] Speaker : text`). It does not appear in the chat panel: that panel is
Panorama, and injecting into it is a separate piece of work; every cheats' Lua does it
through its own chat-print API, which this DLL does not have yet. Only the client's display
filter is bypassed - if the server never sends a line, no client-side hook can invent it.

**Auto-accept** (`other/accept.cpp`) waits for the popup sample
`UIPanorama.popup_accept_match_beep` on `EmitSound1`, then calls the client's
`SetLocalPlayerReady("")`. The signature is resolved once through the catalogue and
range-checked against `client.dll`'s `.text` before the call; a stale pattern means the
feature logs one warning and never fires. That sound is the only "a match was found"
signal a client DLL can see.

**Console filter** (`other/console_filter.cpp`) hooks the three variadic tier0 print
functions with MinHook (no signature and no vtable index can go stale), formats the line
once, and forwards it as `"%s"` so the original keeps its own output path; the colour of
`ConColorMsg` is passed through unchanged. tier0 exports its C++ symbols decorated, so
each hook tries `?ConMsg@@YAXPBDZZ`, `?ConColorMsg@@YAXABVColor@@PBDZZ`, `?Msg@@YAXPBDZZ`
first and the plain spellings second, and the log names the one that resolved. It drops
only the five literal prefixes listed in `other.hpp` — the connect/queue chatter — and
never a line it does not recognise.

**Ping spike** (`general/ping.cpp`) is ported from `main`'s `c_ping_spike`. On
`SendDatagram` it computes `desired_ms/1000 - outgoing_latency - lerp_time`, clamps the
result to `sv_maxunlag`, converts it to ticks with the current `interval_per_tick` and
subtracts them from `CNetChan::in_sequence_nr` (+0x1C), so the server acknowledges the
client later than it really sent. `ProcessPacket` (+0x28 `in_reliable_state`) marks every
freshly processed packet, and the next datagram is then left untouched - that alternating
pattern is what keeps the shift from looking like a constant offset. Outgoing latency
comes from `IVEngineClient::GetNetChannelInfo` (slot 78) and `INetChannelInfo::GetLatency`
(9); both the interface's own vtable and the returned value are validated, so a wrong slot
degrades to "latency unknown" instead of a bad call. `lerp_time` and `sv_maxunlag` are
read through `sdk/convars.hpp` (`ICvar::FindVar` slot 16, `c_convar` layout and the value
obfuscation from `main`'s `structs.hpp`), each with a range check and a sane fallback.
Before it writes anything it validates the connection: the object the client-state offset
points at must have a vtable inside `engine.dll`, and `+0x1C` must hold a plausible
sequence counter. Either check failing disables the feature with one log line instead of
corrupting an unrelated structure, and the first successful spike logs the counter before
and after. The whole detour body is SEH-guarded and switches itself off after one
exception.

**Reveal enemy chat** (`general/chat.cpp`) hooks `IBaseClientDll::DispatchUserMessage`
(slot 38, the table that also holds `FrameStageNotify` 37) and reads the `SayText2` the
client already received. The engine hands team chat to every client - that is why the
client carries `cl_mute_enemy_team` to hide it - so nothing is forged server-side and the
message is never modified: the hook decodes a copy, decides, and forwards the original.

The decision mirrors the gamesense "Enemy team chat revealer" Lua line by line:

| Lua | Here |
|---|---|
| `player_say` → keep when `entity.is_enemy(sender)` | `IsEnemyChat`: sender team is 2/3 and differs from the local team |
| `lastChatMessage[sender]` + 0.4 s window | `ChatGate`: one stamp per sender, a repeat inside `kChatRepeatMs` is dropped |
| `Cstrike_Chat_<team>_<Loc\|Dead>` localization | `FormatChatLine`: `(T) name @ place : text`, `*DEAD*(CT) name : text`, empty place → `Unknown` |
| `m_szLastPlaceName` | `CCSPlayer::LastPlaceName()` (prop offset, read-only) |
| `entity.is_alive(sender)` | `CCSPlayer::IsAlive()` |
| `chat.print_player` | one console line per revealed message |

Two differences, both deliberate: the Lua checks `GameStateAPI.IsSelectedPlayerMuted`,
which lives only in Panorama's own JS state and has no native mirror in this client, so a
player muted through the scoreboard is still revealed; and the line goes to the console
instead of the cheat's chat panel, because this build has no chat panel.

## Movement

The MOVEMENT group box is driven from a **`ClientMode::CreateMove` detour** - slot 24 of
the client-mode vtable, the index this repository's `main` branch uses
(`CREATE_MOVE_IDX = 24`). The object itself is not exported through `CreateInterface`, so
it is reached the same way `main` reaches it: `client`'s vfunc 10, the dword at `+5`,
dereferenced twice (`sdk/interfaces/interfaces.cpp`). The result is only accepted when its
vtable holds a CreateMove slot and lives in `client.dll`; otherwise `ClientMode` stays null,
one warning is logged and movement is off instead of a wrong pointer being hooked.

The original is always called first, so the module corrects the game's own command instead
of replacing the player's input.

| Item | Where it comes from | What it writes |
|---|---|---|
| Air strafe | `main`: `c_movement::auto_strafe` | in the air only: `forwardmove = clamp(5850/speed, ±cl_forwardspeed)`, `sidemove = ±cl_sidespeed` by the sign of the angle between velocity and view, and the wish is rotated by `-diff * smooth` between ticks. The rotated wish is rotated *back* into the command's own view frame before it is written, so the camera never turns - the old "Viewangles" item wrote `cmd->view_angles` and the camera followed it, which is why that item is gone. "Easy strafe" drops the smoothing, "Jump release" takes `IN_JUMP` away while airborne |
| Bhop | `main`: `c_movement::auto_jump` | adds or removes `IN_JUMP` so the jump is released for a tick and re-issued on landing; ladders and noclip are skipped |
| Quick stop | `main`: `fast_stop` + `instant_stop` | on the ground for 3 ticks with no movement button: the full counter-strafe, `-speed * (cos, sin)(view - velocity yaw)`; stops once the speed is under 15 |
| Edge jump | this build (no reference: `main`'s `edge_jump` is an empty stub) | a 24-unit probe ahead along the direction of travel plus a downward trace; a drop deeper than 16 units while moving faster than 90 adds `IN_JUMP` |
| Edge stop | this build, same probe | at that edge, the counter-strafe so the feet stay on the floor |

**Not implemented, and why:** "No fall damage" and "Instant duck". CS:GO decides fall
damage on the server from the velocity the client reports on the landing tick, so no input
removes it, and nothing in `main` implements either feature. Both menu items were removed
rather than left as switches that do nothing.

## Buy bot

The BUY BOT group box has no master switch: an empty selection is "off", exactly like the
Auto strafe combo box. `features/misc/buybot/buybot.hpp` owns the weapon table and the
scheduler, `buybot.cpp` owns the two engine calls.

**What is sent.** One command line, in `main`'s order - primary, secondary, armor, then the
one-item groups - because the server rejects a purchase the player can no longer afford, so
whatever money is left must reach the rifle and not a grenade:

| Menu item | Command |
|---|---|
| Primary: AK47 / M4A1 | `buy ak47; buy m4a1; buy m4a1_silencer;` |
| Primary: SG-556 | `buy aug; buy sg556;` |
| Primary: AWP / SSG-08 / SCAR-20 & G3SG1 / Negev | `buy awp;` / `buy ssg08;` / `buy scar20; buy g3sg1;` / `buy negev;` |
| Secondary: Deagle / Dual Berettas / Tec-9 & Five-SeveN / P250 / CZ75-Auto / R8 | `buy deagle;` / `buy elite;` / `buy tec9; buy fn57;` / `buy p250;` / `buy cz75a;` / `buy revolver;` |
| Armor: Kevlar / Kevlar + Helmet | `buy vest;` / `buy vesthelm;` |
| Defuser / Taser | `buy defuser;` / `buy taser;` |
| Grenades | `buy hegrenade; buy molotov; buy incgrenade; buy smokegrenade; buy flashbang;` |

The CT/T pairs are `main`'s: an item the current team cannot buy is rejected by the server
and the *next* command in the same line still runs. The two entries `main` folds together
(CZ75-Auto with the Desert Eagle, R8 with it too) are separate menu items here, so each maps
to its own command, and the single "Grenades" checkbox covers `main`'s three grenade bits
plus the flashbang.

**When it buys.** `main` listens for the `round_start` game event; this base has no game
event listener, so the window is the state that event stands for: `c_game_rules`'
`m_bFreezePeriod` (byte `+0x20` - the field `main` reads as `is_freeze_time()`), located
through the `game_rules` signature `main` also carries. Freeze time is exactly when the
client is allowed to buy. The object is resolved once, and the byte is checked to be a bool
before the first read is trusted; a mismatch logs one warning and leaves the feature off
instead of writing into an unrelated global.

The purchase is issued from the `ClientMode::CreateMove` detour the movement module already
owns - the bot needs a per-tick entry point, not the command, so it costs one branch per
tick when the window is closed. `main` fires exactly one attempt the tick the round starts;
this build retries up to three times, one second apart, inside the same window, because a
`buy` that arrives before the server has finished the round transition is rejected. A
duplicate purchase is refused by the server, so a retry after a successful buy cannot spend
twice. Attempts are counted per window and reset when freeze time ends; nothing is logged
per tick, only the attempt itself.

**Not implemented:** buying outside freeze time (`mp_buytime`), loadout-aware selections
(the menu picks a weapon, not a saved loadout slot), and an "own the weapon already" check -
re-buying something already held is refused by the server, so the retry path stays safe
without walking the weapon handle array.

## Visuals tab: the Misc group

`visuals>misc>local` is implemented from `features/visuals/misc/`, in the same shape as the
Misc tab's folders. The settings are published by `VisualSettings::Configure()` and read by
the hooks, never the other way round.

| Item | Hook | Provenance |
|---|---|---|
| Thirdperson (+ Distance, Force when dead, Disable on grenade) | `FrameStageNotify` FRAME_RENDER_START + `CInput` | `main`: `thirdperson.cpp` (`c_thirdperson::run_alive`) - same `CInput` fields, same `{ pitch, yaw, distance }` offset, same +-16 unit trace box, same `MASK_NPCWORLDSTATIC`, same observer-mode 4 -> 5 flip for the dead camera |
| Fov changer (Fov) | `FrameStageNotify` FRAME_RENDER_START | `main`: `c_fov_and_view::change_fov` writes the same player fields (`default_fov`, `fov`, `fov_start`) |
| Remove zoom, Force second zoom | FRAME_RENDER_START | `main`: the `fovs[zoom]` term of `change_fov` and its `skip_second_zoom` clamp, expressed against the zoom field of view the game itself wrote |
| Force viewmodel in scope | - | `main`: `change_fov_dead_and_remove_recoil` writes `weapon_info->hide_viewmodel_in_zoom`; left unwritten here until the field is pinned down (a wrong byte in live weapon state is worse than a disabled checkbox) |
| Aspect ratio | `IEngineClient::GetScreenAspectRatio`, slot 101 | `main`: `get_screen_aspect_ratio` detour |
| Viewmodel changer (FOV) | `ClientMode::GetViewModelFOV`, slot 35 | `main`: `get_viewmodel_fov` detour |

Third person writes the engine's own camera instead of moving `CViewSetup` in
`ClientMode::OverrideView`. An earlier revision did the latter and it is exactly what made
the picture wrong: moving the render origin leaves the engine's third-person path out of the
loop, so the view is composed as if the camera were still in the head and the aspect/fov
handling that belongs to that path never runs. The two fields it needs are
`CInput::camera_in_third_person` (`+0xA9`) and `CInput::camera_offset` (`+0xAC`, `+0xB0`,
`+0xB4` = pitch, yaw, distance); both were read out of the supplied `client.dll` - the
`CInput` vtable slots that set and clear the flag, and the getter/setter pair that copies the
vec3, are listed in `docs/interface-verification.md`. The object itself is the `dwInput`
pattern's operand, validated at setup by checking that its vtable pointer lies in
`client.dll`'s `.rdata`.

Nothing has to be hidden from the renderer any more: the engine draws the local model in its
own third-person view, which is what `main` relies on too. The `DrawModelExecute` skip that
this build used to do is gone with the `OverrideView` route.

The world fov slider is absolute and is skipped while scoped - the weapon owns both fov
fields during the zoom animation - and "Remove zoom" / "Force second zoom" are the two items
that operate on the scoped value instead. The zoom field of view itself is not modelled: the
zoom table lives in the weapon's scripts and is not part of anything this build reads, so the
scope code observes the value the game writes and interpolates from there. A scope session's
first observed level is its reference, which is what "Force second zoom" keeps.

**Not implemented, and why:**

- The viewmodel X/Y/Z/roll/yaw/pitch sliders and "Disable landing bob" - `main` writes them
  from its own viewmodel calculation; here they would need a `CBaseViewModel` / view-render
  hook that has not been verified for this client.
- Grenade prediction, grenade proximity warning, penetration crosshair, force crosshair,
  spread circle, hitmarker, bullet tracers, impacts, out-of-FOV, "Remove scope" overlay:
  all of them are 2D or material output. The pattern here is the same as the ESP world
  group: the render path for them (`esp_object_render` in `main`) was never ported, so
  implementing the settings alone would produce checkboxes that draw nothing.

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
- Ping spike does one arithmetic pass per datagram plus two ConVar reads behind range
  checks; nothing is allocated and nothing is formatted unless the one-shot log fires.
- The three hooks do nothing at all while their switch is off: the chat hook returns on
  the message type, the sound hook on the sample name, the console hooks on a flag they
  read before formatting anything.
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
- The chat hook only reads. It validates the payload pointer before using it, bounds every
  protobuf read by `data_bytes`, and never writes to the read object or the payload, so a
  build whose `bf_read` differs cannot be corrupted.
- The decoded message is re-checked (name, sender, team) before a line is printed and falls back
  to the original message whenever the name scan or the round-trip fails, so a payload
  this build encodes differently is never reinterpreted.
- `Misc::Shutdown` removes the OTHER hooks (sound, both console paths) before the clan tag
  is restored, so an unload cannot leave a detour pointing into freed code.
- Flash is cleared only for a positive duration, which also rejects NaN. Punch is
  zeroed when any component is non-zero, which includes NaN.
- Buy bot: an unresolved `game_rules` signature, a null game-rules object or a freeze byte
  that is not a bool disables the feature with one warning - it never guesses. The
  `IEngineClient` command slot is checked to point inside `engine.dll`'s `.text` once
  before the first call, because a wrong vtable index there is a wild call. A throw inside
  the tick stops purchases for the session and logs once.
- Buy bot: the attempt counter is per freeze window and resets when freeze time ends, so a
  round without a buy window (deathmatch, warmup) sends nothing at all instead of a command
  every tick.
- Third person: the camera distance is `slider * trace fraction`, clamped to `[0, 1]`; a
  fraction of zero (something right behind the head, or a trace that threw) keeps the
  first-person camera instead of placing the camera inside geometry. The camera flag is only
  cleared when this module set it, so a demo or spec camera is never fought. An exception in
  the frame-stage call is caught by the stage's own handler and the camera is handed back.
- Fov: the player's own `m_iDefaultFOV` is captured before the first override and written
  back when the checkbox is cleared or the player scopes, so switching the feature off
  never leaves the game on a hard-coded 90. Non-finite slider values are ignored, and the
  write only happens when the value actually changes.
- The tag the player already owned is captured from `m_szClan` before the first write
  and restored when the feature is switched off or the DLL unloads, so toggling does not
  silently wipe a custom tag. If `m_szClan` cannot be read, the restore writes an empty
  tag (what every such feature does by default).

## Not implemented, and why

These controls stay inert. Each needs something this base does not have, and each
reports that once in the log when it is switched on instead of silently doing nothing:

- **Logs, hitsounds, indicators** - need a shot/hit pipeline (ragebot + event tracking);
  to be implemented together with the ragebot.
- **Rank reveal and the scoreboard equipment row** - removed on request: the injected
  Panorama row never looked right on this client, so the whole group went. The reason
  the unmatched-ranks item was never done is unchanged: it needs the engine's own rank
  lookup for players
  the server never sent a skill group for; not implemented by request.
- **sv_cheats bypass** - `main` implements exactly one narrow case: a hook on the
  `sv_cheats` ConVar's `GetInt` that returns 1 only when the caller is `cam_think`'s
  return address. A general "always 1" hook would lie to every client caller, and the
  return-address anchor (`return_addr_cam_think`) has not been verified for this build.
  The ConVar path itself now exists (`sdk/convars.hpp`), so this is a matter of porting
  that anchor, not of missing plumbing.
- **sv_pure bypass** - `main` hooks `IBaseFileSystem::CanLoadThirdpartyFiles` and returns
  1. Porting it needs that filesystem vtable index for this build; the filesystem
  interface is captured (`Interfaces::BaseFileSystem`) but the slot has not been
  verified against a public source that matches, and an off-by-one is a wild call into
  the file system of a live game.
- **Removals 1-7 and 9** (smoke, particles, post processing, water, blood, decals,
  ragdolls, shadows) - material, effect-dispatch and entity-visibility manipulation;
  each needs its own validated hook, and a wrong one is a visible or crashing change.

## Checks

Portable tests:

- `tests/buybot_test.cpp` - `Configured`, the exact command line for every weapon/armor
  entry, the ordering of a full loadout, the window edges and `ShouldAttempt` (dead, not
  in game, closed window, retry delay, the three-attempt budget, a restarted clock).
- `tests/visuals_misc_test.cpp` - the third-person plan (off, not in game, dead with and
  without "Force when dead", grenade held, distance clamping), the trace clamp, and both
  fov policies (scoped, dead, slider limits, non-finite values).
- `tests/misc_radar_test.cpp` - `ShouldSpot` for dormant, dead, local, spectator and
  wrong-team targets.
- `tests/misc_hooks_test.cpp` - `ShouldRevealChat` (team names rewritten, all-chat names
  kept, malformed and null names refused), `ScalePercent` clamping,
  `ShouldScale` (ceiling, negative and NaN velocity), `IsAcceptSound`, and
  `ShouldFilter` (the five prefixes match, everything else is kept), `IsSequenceNumber`
  (the net-channel write is refused outside the counter range), the SayText2 decoder
  (`ParseSayText2` on a real protobuf, truncated and nameless messages refused,
  `FindPayloadField` on a read object whose pointer sits in an arbitrary word and is left
  unchanged by the lookup, `IsEnemyChat`, `ShouldAnnounce`'s 0.4 s window), and the ping
  arithmetic: `TicksForSeconds` at 64/128 tick with its zero/negative guards,
  `SpikeSeconds` (latency and lerp subtracted, unlag ceiling, negative readings ignored)
  and `ShouldSpike`.
- `tests/misc_clantag_test.cpp` - `ShouldApply` transitions (first enable, steady state,
  tag change, new local entity, interval, `GetTickCount` wraparound, disabled, no local
  player, null pointers), `ShouldRestore` exactly-once semantics, and the tag length.
- `tests/removals_test.cpp` - the combo box bit order, `FromMask`, and the flash/punch
  write rules including NaN and infinity.
- `tests/movement_test.cpp` - the yaw helpers, main's strafe wish (gain, clamping, side
  sign, smoothing, non-finite velocity), the bhop release/force cycle and its ladder/noclip
  guards, the counter-strafe vector and its guards, and the edge decision.
- `tests/misc_hooks_test.cpp` - the reveal rules above (team names, all-chat exclusion,
  the per-sender 0.4 s window, the recycling table), the printed line in every shape
  (alive/dead, place known/unknown, control codes stripped, malformed input refused),
  `SayText2` decoding and the read-object field probe.

The engine-facing code is guarded but cannot be verified in a live process here: check
in game that the log has no `misc: frame-stage exception` and that the tag, radar,
removals and the chat reveal behave.
