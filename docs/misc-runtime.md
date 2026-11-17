# Misc runtime

For the current exhaustive menu inventory and prediction status, see
[misc-menu-status.md](misc-menu-status.md). The chronological notes below describe
earlier stages and are superseded by that inventory.

World controls and World runtime are unchanged.

Implemented: bunnyhop (preserves server autobhop), instant unduck through IN_BULLRUSH,
ground quick stop (the existing Legit quick-stop setting), view/key air strafing,
easy strafe and smoothing, optional collision-plane wall avoidance, spotted radar,
spawn/round buybot with buy-zone gate and event deduplication, hit/kill audio,
hit/hurt/grenade-damage/purchase logs. Logs retain eight messages, expire after five
seconds, use per-category colors, and optionally echo sanitized text to console.
Purchase logs respect teammate/enemy/local/all selection. No network commands are
constructed from unsanitized event strings. Failed purchases are left to the server;
there is no retry command flood. Radar restores recorded spotted flags on disable
and shutdown while the entity still matches.

The supplied client.dll datamap was inspected: byte m_MoveType=0x25C and byte
m_nWaterLevel=0x25E. Movement skips ladder, noclip, observer and deep water. Radar
and buy-zone offsets come from live recv tables (no blind offset fallback).
Event registration uses the same ABI as shot chams. Engine ClientCmd slot 7,
ISurface PlaySound slot 82 and ConVar FindVar14/GetInt13 follow the reference SDK.
No speculative autoaccept/clantag signatures were installed.

Reference: reversed2603/fatality_source, dee7327e3b55c03ee93e9eaa0c4dcada64e49110,
features/misc.cpp, features/autostrafer.cpp, features/hitmarker.h and misc/hitsounds.h.
Seven original embedded WAV payloads are stored as immutable arrays in
sdk/third_party/fatality/resources/misc_sounds.h; Arena switch uses a game sound.
No LICENSE was identified for that reference; these assets are not represented as
original or independently licensed. Skeet material reference used in the previous
change does not establish movement/prediction compatibility with this client.

Not implemented here: miss attribution, aimed damage/hitchance/backtrack/safe-point
log fields, prediction-dependent edge jump/edge bug/jump bug/pixel surf/long jump,
peek assist, grenade compensation, autoaccept, clantag, scoreboard reveals,
sv_pure/sv_cheats bypasses and other unconnected Misc controls. No-fall-damage remains
UI-only: a client toggle cannot guarantee bypassing authoritative fall damage.
Existing controls are not evidence of a working runtime implementation.

Portable test: `g++ -std=c++17 -Wall -Wextra -Werror -pthread tests/misc_state_test.cpp -o /tmp/misc-state-test && /tmp/misc-state-test`.
Windows builds do not replace in-game testing: verify all movement modes on a local
server, movement while menu is open, round/spawn purchase dedup, CT/T loadouts,
hit/death audio priority, log filtering, radar disable/disconnect and DLL unload.
Wall avoidance is a short hull trace, not a prediction-based surf/edge solver.

## Skeet-reference movement follow-up

Reference pinned at makavtoJJ1/Skeet-hook-source
`1bab8213bd866f4c366a2071a09b27d6ff76820e` (the supplied repository, not a claim
of authenticated original Skeet). Reviewed `movement/{quick_stop,speed_control,
acceleration,bunny_hop,air_strafe,air_input,air_steering,quick_peek,edge_jump}.cpp`,
`misc/{no_fall_damage,auto_accept_hook,auto_accept_owner}.cpp`,
`engine/build_misc_engine_bindings.cpp`, and `esp/player_engine_draw.cpp`.
No LICENSE identified there; no wholesale native owner framework imported.

This section supersedes the earlier unimplemented list for the features below:

- Quick stop now writes full reverse movement, rather than merely negating the
  current speed. Uses live sv_friction/sv_stopspeed/sv_accelerate and tick interval;
  preserves manual input and skips jump/air/water/ladder. Matches the reference's
  low-speed friction/acceleration exit and 0.01 squared-input threshold. Surface
  friction and weapon-specific speed/slowdown exceptions are not yet captured;
  portable physics currently defaults to surface=1, max-speed=250.
- Bhop's ground/server-autobhop gates match the reviewed suppression stage. Uses
  ConVar GetInt instead of importing reference-specific XOR/parent pointer offsets.
- Air steering uses the reference's alternating ideal-angle/correction branches
  and smoothing rather than blending arbitrary forward/side input. The internal
  movement yaw is transformed back into the camera basis; aim angles stay intact.
  It is not the complete reference prediction/wall/weapon solver. Easy strafe and
  collision-plane avoidance remain the local adaptations.
- Radar's existing spotted write is semantically the same as the reference's
  non-suspended player path. Current runtime additionally records/restores flags.
- Peek assist captures a grounded anchor, returns on local weapon_fire or (if
  selected) movement-key release, compensates stopping position, brakes at the
  anchor and resets on disable/death/command discontinuity/player change. Distance
  limits slide the anchor; negative distance means unlimited. A short hull trace
  blocks/slides return movement. Full 32-command history, acknowledged shot history,
  cooldown cancellation, multi-probe obstacle search and visual ring modes are not
  imported. It is deliberately not advertised as a 1:1 native owner transplant.
- No fall damage is now an **experimental landing assist**, not UI-only. Adapts
  phase2's world-only down trace, walkable floor test, 9..11 unit window,
  -580 velocity threshold and half-gravity duck/jump forecast. Simulation is bounded
  and rejects non-finite input. Does not modify health or authoritative damage;
  success depends on server physics, tick alignment and map geometry. Reference
  HUD discs/arrows are not implemented.
- Edge jump uses the reference's `!jump && was_grounded && !now_grounded` rule, but
  supplies `now_grounded` from a conservative one-tick hull forecast, **not engine
  prediction**. It may differ on stairs, moving platforms and at changed velocity.
  A full same-command prediction/resimulation pipeline remains required for parity.
- Autoaccept uses the reviewed Panorama receiver path (service11 -> engine25,
  event slot1, destructor slot0), the reference event hash and a known beep-name
  fallback. Original callbacks are forwarded; unload/destruction restores slots.
  The actual ready function was independently inspected in received client.dll:
  RVA `0x601760`, full body ending `ret 4`, string operand `deferred`, matching
  JavaScript `SetLocalPlayerReady` bridge. Runtime scans the full relocation-masked
  function body and rechecks the string; does not call a hard-coded RVA. Calls
  stdcall with `accept`, deduplicates for 5 seconds, only outside a game and only
  while enabled. Fails closed with a log when callback/receiver resolution fails.
  Panorama itself was not supplied for binary verification; receiver ABI follows
  the pinned reference and needs an actual match-found test. No automatic repeated
  reinstallation if the receiver is destroyed during a UI reload.

No changes to World controls/runtime. Native and portable compile checks do not
prove landing timing, edge forecast accuracy, Panorama events or in-game behavior.
