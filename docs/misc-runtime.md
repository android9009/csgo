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
    chat.cpp               reveal enemy chat (DispatchUserMessage hook)
    ragdoll.cpp            ragdoll thrust
    ping.cpp               ping spike (SendDatagram / ProcessPacket hooks)
  other/                   second Misc group box ("OTHER", id misc>other)
    other.hpp              the folder's single header
    other.cpp              façade: fan out to auto-accept and the console filter
    accept.cpp             auto-accept (EmitSound1 hook + SetLocalPlayerReady)
    console_filter.cpp     console filter (tier0 ConMsg / ConColorMsg hooks)
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
| Reveal money | `cfg.misc.scoreboard_options` bit 3 | 4 | adds each player's `m_iAccount` to the same row |
| Remove flash | `cfg.misc.removals` bit 0 | 0/5 | zeroes `m_flFlashDuration` on the local player |
| Visual punch | `cfg.misc.removals` bit 8 | 0/5 | zeroes `m_aimPunchAngle` and `m_viewPunchAngle` on the local player |
| Reveal enemy chat | `cfg.misc.reveal_enemy_chat` | hook | re-addresses team chat as all-chat so the HUD prints it |
| Ragdoll thrust | `cfg.misc.ragdoll_thrust_enabled` + `cfg.visuals.ragdoll_thrust` | 4 (every 4th frame) | scales `m_vecForce` and `m_vecRagdollVelocity` on every `CCSRagdoll` |
| Ping spike | `cfg.misc.ping_spike_enabled` + `cfg.aim.ping_spike_amt` | hook | lowers the incoming sequence number so the server measures a higher ping |
| Auto-accept | `cfg.misc.autoaccept` | hook | accepts the match when the accept popup beeps |
| Console filter | `cfg.misc.console_filter` | hook | drops five documented connection/queue lines from the console |

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

**Ragdoll thrust** (`general/ragdoll.cpp`) walks the entity list above the player slots
on stage 4 of every fourth frame, matches `m_pNetworkName == "CCSRagdoll"` (the class id
differs between builds, the name does not), and multiplies `m_vecForce` and
`m_vecRagdollVelocity` by the slider percentage. Both netvars are resolved through
`DT_CSRagdoll`, falling back to `CBaseAnimating` for `m_vecForce`; a missing netvar logs
once and leaves the feature idle. A ragdoll already moving faster than `kSpeedLimit` is
skipped, because the write is not idempotent and a per-frame multiply would launch it.

## Scoreboard

The Scoreboard combo box has three items; two are wired:

- **Reveal ranks** (bit 0) — the client ships a handler for the `ServerRankRevealAll`
  user message. Calling it makes the server send the skill-group table, and the native
  scoreboard then draws ranks for everybody it will ever show. The handler is looked up
  through three candidate signatures (`server_rank_reveal_all`, `_alt1`, `_alt2`: the
  CSGOSimple one plus the two variants UC threads report for older and newer clients),
  and when none of them matches this build the feature dispatches
  `CS_UM_ServerRankRevealAll` (50, from the CS:GO protobuf and CSGOSimple's `Enums.hpp`)
  through `IBaseClientDll::DispatchUserMessage` with a zeroed read object instead of
  dropping the feature. Both paths log which one is in use. It is called from
  `Misc::FrameStage` stage 4, at most once per second (`kRevealIntervalMs`): each call is
  a user message, and flooding one would be both useless and measurable. Once per
  session the log prints how many slots actually carry a skill group afterwards — the
  server decides what it fills in, so that line is the only honest confirmation.
- **Reveal money** (bit 3) — feeds the player's money into the same injected row (a
  `Label` that only appears when a plausible `m_iAccount` was read). Same injection, same
  failure policy.
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
- Ping spike does one arithmetic pass per datagram plus two ConVar reads behind range
  checks; nothing is allocated and nothing is formatted unless the one-shot log fires.
- Ragdoll thrust sweeps the entity list only on every fourth stage-4 frame and stops at
  the highest entity index, so a full round of ragdolls costs four sweeps per second.
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
- The tag the player already owned is captured from `m_szClan` before the first write
  and restored when the feature is switched off or the DLL unloads, so toggling does not
  silently wipe a custom tag. If `m_szClan` cannot be read, the restore writes an empty
  tag (what every such feature does by default).

## Not implemented, and why

These controls stay inert. Each needs something this base does not have, and each
reports that once in the log when it is switched on instead of silently doing nothing:

- **Logs, hitsounds, indicators** - need a shot/hit pipeline (ragebot + event tracking);
  to be implemented together with the ragebot.
- **Scoreboard "Reveal unmatched ranks"** - needs the engine's own rank lookup for players
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
- **Movement and Buybot** - next phases. Movement needs a `CreateMove` detour, which
  `sdk/hooks/detours.cpp` intentionally leaves empty; Buybot needs the convar /
  `ClientCmd` path, i.e. the same `ICvar` work as above.

## Checks

Portable tests:

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
- `tests/scoreboard_test.cpp` - the Scoreboard combo box bit order (ranks, weapon,
  unmatched, money), the rank reveal gate (disabled, not in game, zero interval, first
  send, inside the interval, and the `GetTickCount` wrap).
- `tests/scoreboard_script_test.cpp` - scale clamping and the scaled layout/precision
  values, the icon table, and the feed payload: no player without equipment, no slot 0,
  balanced braces, self-contained engine, and icon names restricted to plain identifiers
  (which is what makes injection through the payload impossible).

The engine-facing code is guarded but cannot be verified in a live process here: check
in game that the log has no `misc: frame-stage exception` and that the tag, radar and
removals behave.
