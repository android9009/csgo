// Portable checks for the Misc rules that decide whether a hook touches the game: revealing
// enemy chat and filtering the console, plus the message the reveal prints. All of them are
// pure functions so they can be pinned here without Windows or the engine.
#include "features/misc/general/general.hpp"
#include "features/misc/other/other.hpp"
#include <cassert>
#include <cstring>

int main() {
    // ---- reveal enemy chat: team names are printed, all-chat names are not ----------
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
    // Already visible to everybody: printing it again would duplicate the line.
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

    // ---- only the other team's lines are announced --------------------------------
    using Misc::Chat::IsEnemyChat;
    assert(IsEnemyChat(true, 2, 3));   // T speaking, local player on CT
    assert(IsEnemyChat(true, 3, 2));
    assert(!IsEnemyChat(true, 2, 2));  // own team: the HUD draws it
    assert(!IsEnemyChat(true, 0, 3));  // spectator or server
    assert(!IsEnemyChat(true, 1, 3));  // no team
    assert(!IsEnemyChat(false, 2, 3));

    // ---- the same line arriving twice is printed once -----------------------------
    using Misc::Chat::ChatGate;
    using Misc::Chat::ShouldAnnounce;
    const auto hash = Misc::Chat::HashChatText("gg ez");
    assert(hash != Misc::Chat::HashChatText("gg ez2"));
    assert(hash == Misc::Chat::HashChatText("gg ez"));
    assert(Misc::Chat::HashChatText(nullptr) == Misc::Chat::HashChatText(""));
    using Misc::Chat::RecordAnnounce;
    ChatGate gate;
    assert(ShouldAnnounce(gate, 5, hash, 1000));      // first line
    RecordAnnounce(gate, 5, hash, 1000);
    assert(!ShouldAnnounce(gate, 5, hash, 1200));     // same player, same line, inside 0.4 s
    assert(ShouldAnnounce(gate, 5, hash, 1400));      // the window has passed
    assert(ShouldAnnounce(gate, 5, Misc::Chat::HashChatText("other"), 1100)); // different line
    // The Lua keeps one stamp per player, so a second speaker is not silenced by the first.
    RecordAnnounce(gate, 6, hash, 1100);
    RecordAnnounce(gate, 5, hash, 1110);
    assert(!ShouldAnnounce(gate, 5, hash, 1200));
    assert(!ShouldAnnounce(gate, 6, hash, 1200));
    assert(ShouldAnnounce(gate, 7, hash, 1200));
    // A full table recycles instead of growing: 40 senders, all of them remembered later.
    for (int i = 0; i < 40; ++i) RecordAnnounce(gate, 100 + i, hash, 2000 + i);
    assert(!ShouldAnnounce(gate, 139, hash, 2100));   // the newest speaker is still there
    assert(ShouldAnnounce(gate, 100, hash, 2100));    // the oldest one was recycled

    // ---- the printed line is the game's own, not an invented one -------------------
    using Misc::Chat::FormatChatLine;
    // Alive: the "Loc" spelling, with the place after the name.
    assert(FormatChatLine(2, true, "Dan", "Bombsite A", "gg") == "(T) Dan @ Bombsite A : gg");
    assert(FormatChatLine(3, true, "Jim", "CT Spawn", "nice") == "(CT) Jim @ CT Spawn : nice");
    // Dead: the "Dead" spelling, and no place at all.
    assert(FormatChatLine(2, false, "Dan", "Bombsite A", "gg") == "*DEAD*(T) Dan : gg");
    assert(FormatChatLine(3, false, "Jim", "", "gg") == "*DEAD*(CT) Jim : gg");
    // The Lua localizes an empty place name to UI_Unknown, which this client shows as
    // "Unknown".
    assert(FormatChatLine(2, true, "Dan", "", "gg") == "(T) Dan @ Unknown : gg");
    assert(FormatChatLine(2, true, "Dan", nullptr, "gg") == "(T) Dan @ Unknown : gg");
    // Control codes and padding from the game string never reach the console.
    // (The test literal is split because "\x07B" would be read as one hex escape.)
    assert(FormatChatLine(2, true, "Dan", "\x07" "Bombsite\x01" " A  ", "gg") ==
           "(T) Dan @ Bombsite A : gg");
    // A line that cannot be a chat line is refused instead of printed half-way.
    assert(FormatChatLine(0, true, "Dan", "A", "gg").empty());
    assert(FormatChatLine(1, true, "Dan", "A", "gg").empty());
    assert(FormatChatLine(2, true, "", "A", "gg").empty());
    assert(FormatChatLine(2, true, nullptr, "A", "gg").empty());
    assert(FormatChatLine(2, true, "Dan", "A", nullptr).empty());

    // ---- SayText2: the payload is decoded, never rewritten -------------------------
    {
        // The protobuf CS:GO actually sends: ent_idx (field 1), chat flag (2), msg_name (3),
        // params (4) with the speaker and the line.
        unsigned char payload[128]{};
        int pos = 0;
        auto varint = [&payload, &pos](int value) {
            while (value >= 0x80) {
                payload[pos++] = static_cast<unsigned char>((value & 0x7F) | 0x80);
                value >>= 7;
            }
            payload[pos++] = static_cast<unsigned char>(value & 0x7F);
        };
        auto field_string = [&payload, &pos, &varint](int field, const char* text) {
            const int length = static_cast<int>(std::strlen(text));
            payload[pos++] = static_cast<unsigned char>((field << 3) | 2);
            varint(length);
            for (int i = 0; i < length; ++i) payload[pos++] = static_cast<unsigned char>(text[i]);
        };
        payload[pos++] = (1 << 3) | 0; // ent_idx
        varint(7);
        payload[pos++] = (2 << 3) | 0; // chat
        varint(1);
        field_string(3, "Cstrike_Chat_T_Loc");
        field_string(4, "enemy");
        field_string(4, "gg ez");
        payload[pos++] = (5 << 3) | 0; // text_all_chat
        varint(0);
        const int payload_size = pos;

        Misc::Chat::SayText2 message;
        assert(Misc::Chat::ParseSayText2(payload, payload_size, message));
        assert(message.ent_index == 7);
        assert(std::strcmp(message.name, "Cstrike_Chat_T_Loc") == 0);
        assert(message.name_length == 18);
        assert(std::strcmp(message.speaker, "enemy") == 0);
        assert(std::strcmp(message.text, "gg ez") == 0);
        assert(ShouldRevealChat(true, message.name, message.name_length));
        assert(IsEnemyChat(true, 2, 3));

        // The name is also found by scanning, for the scan the read object needs.
        assert(Misc::Chat::FindChatNameOffset(payload, payload_size) == 5);
        unsigned char junk[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
        assert(Misc::Chat::FindChatNameOffset(junk, sizeof(junk)) == -1);
        assert(Misc::Chat::FindChatNameOffset(nullptr, 16) == -1);
        assert(Misc::Chat::FindChatNameOffset(payload, 4) == -1);

        // Truncated or malformed messages are refused, not read past their end.
        Misc::Chat::SayText2 broken;
        assert(!Misc::Chat::ParseSayText2(payload, 3, broken));
        assert(!Misc::Chat::ParseSayText2(payload, 0, broken));
        assert(!Misc::Chat::ParseSayText2(nullptr, payload_size, broken));
        assert(!Misc::Chat::ParseSayText2(junk, 16, broken));
        // A message without params has no line to print.
        unsigned char name_only[32]{};
        int at = 0;
        name_only[at++] = (3 << 3) | 2;
        name_only[at++] = 16;
        for (const char* p = "Cstrike_Chat_All"; *p; ++p) name_only[at++] = static_cast<unsigned char>(*p);
        assert(!Misc::Chat::ParseSayText2(name_only, at, broken));

        // A read object whose payload pointer sits in its third word resolves itself.
        std::uintptr_t fields[Misc::Chat::kReadFieldCount]{};
        fields[0] = 0; // empty fields are skipped instead of dereferenced
        fields[1] = 0;
        fields[2] = reinterpret_cast<std::uintptr_t>(payload);
        fields[3] = static_cast<std::uintptr_t>(payload_size);
        assert(Misc::Chat::FindPayloadField(fields, Misc::Chat::kReadFieldCount, payload_size) == 2);
        // A read object that claims nothing is refused.
        std::uintptr_t empty[Misc::Chat::kReadFieldCount]{};
        assert(Misc::Chat::FindPayloadField(empty, Misc::Chat::kReadFieldCount, payload_size) == -1);
        assert(Misc::Chat::FindPayloadField(nullptr, Misc::Chat::kReadFieldCount, payload_size) == -1);
        // Resolving the payload is read-only: the words keep their values.
        const std::uintptr_t before[4] = {fields[0], fields[1], fields[2], fields[3]};
        (void) Misc::Chat::FindPayloadField(fields, Misc::Chat::kReadFieldCount, payload_size);
        for (int i = 0; i < 4; ++i) assert(fields[i] == before[i]);
    }

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

    // A value outside the counter range is refused, not subtracted from.
    assert(IsSequenceNumber(0));
    assert(IsSequenceNumber(12345));
    assert(IsSequenceNumber(kMaxSequence));
    assert(!IsSequenceNumber(-1));
    assert(!IsSequenceNumber(kMaxSequence + 1));

    // The sequence number is lowered only while there is room left.
    assert(ShouldSpike(100, 6));
    assert(!ShouldSpike(6, 6));
    assert(!ShouldSpike(5, 6));
    assert(!ShouldSpike(100, 0));
    assert(!ShouldSpike(100, -3));
    assert(!ShouldSpike(0, 6));
    return 0;
}
