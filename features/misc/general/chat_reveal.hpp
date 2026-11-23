#pragma once
// Enemy team chat reveal - the port of gamesense's "Enemy team chat revealer" script
// (the primordials carry the same lua). The CS2-only pieces of that script (XUID
// mutes, Panorama, its CHudChat FFI print thunk) have no CS:GO equivalent; what
// survives is the reveal mechanic itself, against CS:GO's own chat delivery:
//
//   * The server fires the `player_say` game event to EVERY client for both `say`
//     and `say_team` - in the decompiled CS:GO server the event is built after the
//     per-recipient SayText2 loop, unfiltered (game/server/client.cpp).
//   * The visible chat line is a SayText2 protobuf user message (msg type 6) sent
//     only to recipients that PlayerCanHearChat() accepts - for say_team that is
//     the sender's own team. Enemy team chat is never a visible message for us,
//     which is exactly why the in-game chat window cannot show it.
//
// So the hook side and the event side are combined, same as the script does:
//   * hook IBaseClientDLL::DispatchUserMessage - slot 38 of Interfaces::Client,
//     the slot right after FrameStageNotify (37), the one this build already hooks
//     (the decompiled CHLClient lists DispatchUserMessage directly after
//     FrameStageNotify) - and stamp a time when a player's SayText2 arrives with
//     chat==true, i.e. when the say is visible chat;
//   * on `player_say` from an enemy, wait 0.2s (the script's delay) and, with no
//     fresh visible-chat stamp within the 0.4s window, conclude the say was
//     say_team and push a line onto the toast queue the menu drains into the gui
//     notification system.
#include <cstdint>
#include <string>
#include <vector>

#include "../../../sdk/datatypes/game_event.hpp"

namespace Misc::General {
// One revealed enemy team chat line, for the menu to turn into an in-game toast.
struct ChatToast {
    std::string header;
    std::string text;
};

namespace Detail {
// Menu bridge (called every frame from the render thread): the feature switch.
void ChatRevealPublish(bool enabled);

// Game thread. The global game-event listener forwards `player_say` here.
void ChatRevealOnPlayerSay(const IGameEvent* event);
// Game thread. The round_start reset (stale stamps must not leak across maps).
void ChatRevealOnRoundStart();
// Game thread. Drives the lazy hook attach and the 0.2s-late reveal decisions.
void ChatRevealFrameStage(int stage);
// Unload: detach the hook, drop queued state.
void ChatRevealShutdown();

// Render thread (menu): takes the revealed lines out of the queue.
void DrainChatToasts(std::vector<ChatToast>& out);
} // namespace Detail
} // namespace Misc::General
