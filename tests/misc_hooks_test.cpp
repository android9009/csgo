// Portable checks for the three Misc rules that decide whether a hook touches the game:
// revealing enemy chat, scaling ragdolls, and filtering the console. All three are
// pure predicates so they can be pinned here without Windows or the engine.
#include "features/misc/general/general.hpp"
#include "features/misc/other/other.hpp"
#include <cassert>

int main() {
    // ---- reveal enemy chat: team names are re-addressed, all-chat names are not -----
    using Misc::Chat::ShouldRevealChat;
    const char* team_t = "Cstrike_Chat_T";
    const char* team_ct = "Cstrike_Chat_CT";
    const char* team_loc = "Cstrike_Chat_CT_Loc";
    const char* team_dead = "Cstrike_Chat_T_Dead";
    const char* all = "Cstrike_Chat_All";
    const char* all_dead = "Cstrike_Chat_All_Dead";
    assert(ShouldRevealChat(true, team_t, 14));
    assert(ShouldRevealChat(true, team_ct, 15));
    assert(ShouldRevealChat(true, team_loc, 19));
    assert(ShouldRevealChat(true, team_dead, 19));
    // Already visible to everybody: rewriting would duplicate the line.
    assert(!ShouldRevealChat(true, all, 16));
    assert(!ShouldRevealChat(true, all_dead, 21));
    // Off, unparsed, or not a chat name at all.
    assert(!ShouldRevealChat(false, team_t, 14));
    assert(!ShouldRevealChat(true, nullptr, 14));
    assert(!ShouldRevealChat(true, team_t, 0));
    assert(!ShouldRevealChat(true, "Cstrike_Chat_", 13));       // too short to be a name
    assert(!ShouldRevealChat(true, "Cstrike_Chat", 12));
    assert(!ShouldRevealChat(true, "Cstrike_Kill", 12));
    assert(!ShouldRevealChat(true, "#Cstrike_Chat_T", 15));
    // The team prefix is what matters, not a word anywhere in the name.
    assert(ShouldRevealChat(true, "Cstrike_Chat_T_All", 18));

    // ---- ragdoll thrust: the slider is clamped, and fast ragdolls are left alone --
    assert(Misc::Ragdoll::ScalePercent(0) == 1.f);
    assert(Misc::Ragdoll::ScalePercent(1) == 1.f);
    assert(Misc::Ragdoll::ScalePercent(100) == 100.f);
    assert(Misc::Ragdoll::ScalePercent(1000) == 1000.f);
    assert(Misc::Ragdoll::ScalePercent(5000) == 1000.f);
    assert(Misc::Ragdoll::ScalePercent(-20) == 1.f);

    using Misc::Ragdoll::ShouldScale;
    const float limit = Misc::Ragdoll::kSpeedLimit;
    assert(ShouldScale(0.f, limit));
    assert(ShouldScale(limit - 0.5f, limit));
    assert(!ShouldScale(limit, limit));
    assert(!ShouldScale(limit + 1.f, limit));
    assert(!ShouldScale(-1.f, limit));    // unreadable velocity
    assert(!ShouldScale(0.f / 0.f, limit)); // NaN from an uninitialised vector

    // ---- auto-accept triggers on exactly one sample -------------------------------
    using Misc::Other::Accept::IsAcceptSound;
    assert(IsAcceptSound("UIPanorama.popup_accept_match_beep"));
    assert(!IsAcceptSound("UIPanorama.popup_accept_match"));
    assert(!IsAcceptSound("UIPanorama.popup_accept_match_beep_extra"));
    assert(!IsAcceptSound(""));
    assert(!IsAcceptSound(nullptr));

    // ---- console filter: prefix matches only, and never an empty line -------------
    using Misc::Other::ConsoleFilter::ShouldFilter;
    assert(ShouldFilter("Connecting to 155.133.248.41:27015..."));
    assert(ShouldFilter("Server using protocol version 13785"));
    assert(ShouldFilter("Connection to Steam servers successful."));
    assert(ShouldFilter("Assigned anonymous gameserver 92.223.105.5:27015."));
    assert(ShouldFilter("Assigned persistent gameserver 92.223.105.5:27015."));
    // Anything else is kept: this must not hide real output.
    assert(!ShouldFilter("Connecting"));
    assert(!ShouldFilter("ctrlc: Connecting to a server"));
    assert(!ShouldFilter("Unknown command: net_graph"));
    assert(!ShouldFilter(""));
    assert(!ShouldFilter(nullptr));
    return 0;
}
