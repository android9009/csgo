#pragma once
// The second Misc group box (id misc>other): the bypass / console utilities. One header for
// the folder, one translation unit per feature.
//
//   sv_pure.cpp     - "Bypass SV Pure": drops the type-14 (SV_PUREREADY) net message in a
//                     CNetChan::SendNetMsg detour (ported from this repo's `main` branch).
//   radar.cpp       - "Radar" (force engine radar): IEngineClient::IsHLTV VMT hook that
//                     answers true from the radar's call site (ported from `main`).
//   autoaccept.cpp  - "Auto-accept": IEngineSound::EmitSound VMT hook that auto-runs
//                     SetLocalPlayerReady when the match-accept popup sound plays.
//   other.cpp       - the group facade + "Console filter" (con_filter_enable cvar) and the
//                     one utility that stays documented-inactive in this build (sv_cheats).
namespace Misc::Other {
struct Settings {
    bool sv_pure{};
    bool sv_cheats{};
    bool console_filter{};
    bool autoaccept{};
    bool radar{};
};

void Publish(const Settings& settings);
void FrameStage(int stage);
void Shutdown();

namespace Detail {
// Published setting accessors, defined in other.cpp next to the atomics they read.
bool SvPure();
bool SvCheats();
bool ConsoleFilter();
bool AutoAccept();
bool Radar();

void RadarFrameStage(int stage);    // radar.cpp
void RadarShutdown();               // radar.cpp
void SvPureFrameStage(int stage);   // sv_pure.cpp
void SvPureShutdown();              // sv_pure.cpp
void AutoAcceptFrameStage(int stage); // autoaccept.cpp
void AutoAcceptShutdown();          // autoaccept.cpp
} // namespace Detail
} // namespace Misc::Other
