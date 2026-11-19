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

    // ---- SayText2 payload: the name is found without assuming a field order ---------
    {
        // A minimal payload: one unknown byte, the sender, three unknown bytes, the name,
        // the player, the text and the flag.
        unsigned char payload[64]{};
        int pos = 0;
        payload[pos++] = 0x00;
        payload[pos++] = 0x07; // sender slot
        payload[pos++] = 0x00;
        payload[pos++] = 0x00;
        payload[pos++] = 0x00;
        const char* name = "Cstrike_Chat_T";
        const int name_len = 14;
        payload[pos++] = static_cast<unsigned char>(name_len);
        for (int i = 0; i < name_len; ++i) payload[pos++] = static_cast<unsigned char>(name[i]);
        const char* player = "player";
        payload[pos++] = 7;
        for (int i = 0; i < 7; ++i) payload[pos++] = static_cast<unsigned char>(player[i]);
        payload[pos++] = 0;
        const int payload_size = pos;

        assert(Misc::Chat::FindChatNameOffset(payload, payload_size) == 5);
        // A payload without a chat name is refused rather than searched further.
        unsigned char junk[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
        assert(Misc::Chat::FindChatNameOffset(junk, sizeof(junk)) == -1);
        assert(Misc::Chat::FindChatNameOffset(nullptr, 16) == -1);
        assert(Misc::Chat::FindChatNameOffset(payload, 4) == -1);

        // A read object whose payload pointer sits in its third word resolves itself.
        std::uintptr_t fields[Misc::Chat::kReadFieldCount]{};
        fields[0] = 0; // empty fields are skipped instead of dereferenced
        fields[1] = 0;
        fields[2] = reinterpret_cast<std::uintptr_t>(payload);
        fields[3] = static_cast<std::uintptr_t>(payload_size);
        assert(Misc::Chat::FindPayloadField(fields, Misc::Chat::kReadFieldCount, payload_size) == 2);
        // Nothing plausible: no field claims the payload.
        std::uintptr_t empty[Misc::Chat::kReadFieldCount]{};
        assert(Misc::Chat::FindPayloadField(empty, Misc::Chat::kReadFieldCount, payload_size) == -1);

        // Repointing updates the buffer start and its size, and leaves everything else.
        unsigned char rebuilt[64]{};
        const int rebuilt_size = payload_size - name_len + 16; // "Cstrike_Chat_All"
        const auto repoint = Misc::Chat::RepointRead(fields, Misc::Chat::kReadFieldCount, payload,
                                                    payload_size, rebuilt, rebuilt_size);
        assert(repoint.ok());
        assert(repoint.pointer_fields == 1);
        assert(repoint.length_fields == 1);
        assert(fields[2] == reinterpret_cast<std::uintptr_t>(rebuilt));
        assert(fields[3] == static_cast<std::uintptr_t>(rebuilt_size));
        assert(fields[0] == 0 && fields[1] == 0);
        // A null or empty payload never repoints anything.
        const auto none_a = Misc::Chat::RepointRead(fields, Misc::Chat::kReadFieldCount, nullptr,
                                                    payload_size, rebuilt, rebuilt_size);
        assert(!none_a.ok());
        const auto none_b = Misc::Chat::RepointRead(fields, Misc::Chat::kReadFieldCount, payload, 0,
                                                    rebuilt, rebuilt_size);
        assert(!none_b.ok());
    }

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
    // ---- ping spike: the shift is clamped to the unlag budget ---------------------
    using namespace Misc::Ping;
    // TIME_TO_TICKS at the two common tick rates, and the guards.
    assert(TicksForSeconds(0.1f, 1.f / 64.f) == 6);
    assert(TicksForSeconds(0.1f, 1.f / 128.f) == 13);
    assert(TicksForSeconds(0.f, 1.f / 64.f) == 0);
    assert(TicksForSeconds(-0.1f, 1.f / 64.f) == 0);
    assert(TicksForSeconds(0.1f, 0.f) == 0); // interval not known yet
    assert(TicksForSeconds(0.1f, -1.f) == 0);

    // Requested 100 ms with nothing known about the connection: the whole amount is used.
    assert(SpikeSeconds(100.f, 0.f, 0.f, kDefaultMaxUnlag) > 0.0999f);
    // Existing latency and interpolation are subtracted, not ignored.
    assert(SpikeSeconds(100.f, 0.02f, 0.03f, kDefaultMaxUnlag) > 0.0499f);
    assert(SpikeSeconds(100.f, 0.02f, 0.03f, kDefaultMaxUnlag) < 0.0501f);
    // Already slower than the requested amount: nothing to add.
    assert(SpikeSeconds(30.f, 0.02f, 0.03f, kDefaultMaxUnlag) == 0.f);
    assert(SpikeSeconds(0.f, 0.f, 0.f, kDefaultMaxUnlag) == 0.f);
    // The server's unlag budget is the hard ceiling.
    assert(SpikeSeconds(200.f, 0.f, 0.f, 0.2f) == 0.2f);
    // A negative latency reading is ignored rather than subtracted.
    assert(SpikeSeconds(100.f, -0.5f, 0.f, kDefaultMaxUnlag) > 0.0999f);

    // The sequence number is lowered only while there is room left.
    assert(ShouldSpike(100, 6));
    assert(!ShouldSpike(6, 6));
    assert(!ShouldSpike(5, 6));
    assert(!ShouldSpike(100, 0));
    assert(!ShouldSpike(100, -3));
    assert(!ShouldSpike(0, 6));
    return 0;
}
