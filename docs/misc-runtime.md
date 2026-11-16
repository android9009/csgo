# Misc runtime

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
third_party/fatality/resources/misc_sounds.h; Arena switch uses a game sound.
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
