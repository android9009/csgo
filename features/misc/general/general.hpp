#pragma once
// The first group box of the Misc tab ("MISC", group id misc>general).
//
// One folder per menu group box, ONE header for the whole folder and one translation
// unit per feature: this file declares everything the folder does and the portable
// rules the features are built on, while all the logic lives in the five .cpp files next
// to it. Nothing here includes Windows or engine types, so the portable tests can use
// any of it on a host that has no game.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Façade: the only part the menu bridge and Misc::FrameStage talk to.
// ---------------------------------------------------------------------------
namespace Misc::General {
struct Settings {
    bool radar{};
    bool clantag{};

    // "Auto-accept" lives in Misc::Other::Settings: its checkbox is in the OTHER group.
    bool reveal_enemy_chat{};  // "Reveal enemy chat"

    bool ping_spike{};
    float ping_spike_ms{}; // "Ping spike > Amount", milliseconds
};

// Called from the menu bridge on the render thread; forwards each value to the feature
// that owns it.
void Publish(const Settings& settings);

// Called on the game thread from Misc::FrameStage.
void FrameStage(int stage);

// Called once during unload.
void Shutdown();
} // namespace Misc::General

// ---------------------------------------------------------------------------
// Radar (radar.cpp): marks players as spotted so they appear on the local radar.
// ---------------------------------------------------------------------------
namespace Misc::Radar {
// Playable teams in CS:GO: T = 2, CT = 3.
constexpr int kTeamTerrorist = 2;
constexpr int kTeamCounterTerrorist = 3;

struct Target {
    bool dormant{};
    bool alive{};
    bool local{};
    int team{};
};

// Only living, non-dormant players of a playable team are worth writing to: the local
// player is already on his own radar, and dormant entities carry stale data.
inline bool ShouldSpot(const Target& target) {
    return !target.dormant && target.alive && !target.local &&
           (target.team == kTeamTerrorist || target.team == kTeamCounterTerrorist);
}

void SetEnabled(bool enabled);
void FrameStage(int stage); // stage 4 (net update end) only
} // namespace Misc::Radar

// ---------------------------------------------------------------------------
// Clan tag (clantag.cpp): writes the tag through engine.dll's SetClanTag.
// ---------------------------------------------------------------------------
namespace Misc::ClanTag {
// The Misc tab exposes a plain on/off switch, so the tag itself lives here.
// The engine limit is 15 characters.
inline constexpr const char* kTag = "pastavar";

// How often the tag is re-asserted while in game. Every call is an engine string update
// that the server rate-limits, so this is deliberately coarse.
inline constexpr std::uint32_t kReapplyIntervalMs = 5000;

// Re-applied on a first enable, a tag change, a new local player entity (map change) or
// after the interval - never every frame.
inline bool ShouldApply(bool enabled, bool applied, const char* applied_tag,
                        const char* desired_tag, std::uintptr_t applied_local,
                        std::uintptr_t local, std::uint32_t now_ms, std::uint32_t last_ms,
                        std::uint32_t interval_ms) {
    if (!enabled || !local) return false;
    if (!applied) return true;
    if (!applied_tag || !desired_tag || std::strcmp(applied_tag, desired_tag) != 0) return true;
    if (applied_local != local) return true;
    // Unsigned subtraction keeps this correct across the 49-day tick wraparound.
    return static_cast<std::uint32_t>(now_ms - last_ms) >= interval_ms;
}

// The tag is restored exactly once, when the feature is switched off.
inline bool ShouldRestore(bool enabled, bool applied) { return applied && !enabled; }

void SetEnabled(bool enabled);
void FrameStage(int stage); // stages 0/4/5
void Shutdown();            // restores the tag this module owns
} // namespace Misc::ClanTag

// ---------------------------------------------------------------------------
// Reveal enemy chat (chat.cpp): hooks IBaseClientDll::DispatchUserMessage (slot 38, the
// vtable that also holds FrameStageNotify 37) and prints the enemy team chat the client
// received but refuses to draw.
//
// The message is read, never written. The gamesense "Enemy team chat revealer" Lua works
// the same way: it takes the chat the cheat already has, keeps only lines from enemies,
// remembers when each player last spoke so nothing is printed twice, and renders the
// line itself. The engine does deliver team chat to every client - that is why the client
// carries cl_mute_enemy_team to hide it - so nothing has to be forged server-side.
// ---------------------------------------------------------------------------
namespace Misc::Chat {
// A chat name is a length prefix plus the name itself ("Cstrike_Chat_CT_Loc"). The name is
// looked for in the first bytes of the payload, because the published descriptions of this
// message disagree about everything that precedes it.
constexpr int kNameScanWindow = 32;
// bf_read is an object, not a buffer: the payload pointer lives in one of its fields and the
// field offset differs between client builds. Nine machine words (36 bytes) covers the
// published layouts - the pointer sits at +0x18..+0x20 - and deliberately stops there:
// every word past the real object is somebody else's stack slot, so nothing past it is
// looked at and nothing at all is written back.
constexpr int kReadFieldCount = 9;

// Locates the length-prefixed message name in a SayText2 payload. Returns the offset of the
// length byte, or -1 when the bytes do not look like a chat name.
inline int FindChatNameOffset(const unsigned char* data, int size) {
    if (!data || size < 3) return -1;
    const int limit = size < kNameScanWindow ? size : kNameScanWindow;
    for (int offset = 0; offset + 2 < limit; ++offset) {
        const int length = data[offset];
        if (length < 14 || length > 40) continue; // shorter than "Cstrike_Chat_T"
        if (offset + 1 + length > size) continue;
        if (std::strncmp(reinterpret_cast<const char*>(data + offset + 1), "Cstrike_Chat_", 13) != 0)
            continue;
        bool printable = true;
        for (int i = 0; i < length; ++i) {
            const unsigned char c = data[offset + 1 + i];
            if (c < 0x20 || c > 0x7E) { printable = false; break; }
        }
        if (printable) return offset;
    }
    return -1;
}

// Finds the field of a bf_read object that points at a buffer starting with a chat name.
// Every field is tried and each candidate is validated against the message length the caller
// already knows, so a build with a different layout resolves itself instead of being
// hardcoded. Returns the field index, or -1. Read-only: the object is never modified.
inline int FindPayloadField(const std::uintptr_t* fields, int field_count, int data_bytes) {
    if (!fields || data_bytes <= 0) return -1;
    for (int i = 0; i < field_count; ++i) {
        const auto candidate = reinterpret_cast<const unsigned char*>(fields[i]);
        if (!candidate) continue;
        if (FindChatNameOffset(candidate, data_bytes) >= 0) return i;
    }
    return -1;
}

// A SayText2 payload is CCSUsrMsg_SayText2: ent_idx (field 1, varint), chat (2), msg_name
// (3, string), params (4, repeated string) - the speaker in params[0] and the line itself in
// params[1]. Only those four fields are decoded, and every read is bounded by the message
// length the dispatcher already handed us.
struct SayText2 {
    int ent_index{};
    char name[40]{};
    int name_length{};
    char speaker[64]{};
    int speaker_length{};
    char text[256]{};
    int text_length{};
};

inline void CopyChatField(char* destination, int capacity, const unsigned char* source, int length) {
    if (!destination || capacity <= 1 || !source || length <= 0) return;
    const int copy = length < capacity - 1 ? length : capacity - 1;
    std::memcpy(destination, source, static_cast<std::size_t>(copy));
    destination[copy] = '\0';
}

inline bool ParseSayText2(const unsigned char* data, int size, SayText2& out) {
    if (!data || size <= 1) return false;
    int cursor = 0;
    int param = 0;
    while (cursor < size) {
        const int tag = data[cursor++];
        const int field = tag >> 3;
        const int wire = tag & 7;
        if (field == 0) return false;
        if (wire == 0) {
            unsigned long long value = 0;
            int shift = 0;
            bool ended = false;
            while (cursor < size && shift <= 63) {
                const int byte = data[cursor++];
                value |= static_cast<unsigned long long>(byte & 0x7F) << shift;
                shift += 7;
                if (!(byte & 0x80)) { ended = true; break; }
            }
            if (!ended) return false;
            if (field == 1) out.ent_index = static_cast<int>(value);
        } else if (wire == 2) {
            if (cursor >= size) return false;
            int length = data[cursor++];
            if (length & 0x80) { // a chat message never needs more than two length bytes
                if (cursor >= size) return false;
                length = (length & 0x7F) | (data[cursor++] << 7);
            }
            if (length < 0 || cursor + length > size) return false;
            if (field == 3) {
                CopyChatField(out.name, static_cast<int>(sizeof(out.name)), data + cursor, length);
                out.name_length = std::strlen(out.name);
            } else if (field == 4) {
                if (param == 0) {
                    CopyChatField(out.speaker, static_cast<int>(sizeof(out.speaker)), data + cursor, length);
                    out.speaker_length = std::strlen(out.speaker);
                } else if (param == 1) {
                    CopyChatField(out.text, static_cast<int>(sizeof(out.text)), data + cursor, length);
                    out.text_length = std::strlen(out.text);
                }
                ++param;
            }
            cursor += length;
        } else {
            return false;
        }
    }
    return out.name_length > 0 && out.text_length > 0;
}

// Team chat is worth printing; all-chat is already drawn by the HUD, so printing it again
// would show every line twice - the same reason the Lua banner-mutes the all-chat names.
inline bool ShouldRevealChat(bool enabled, const char* name, int name_length) {
    if (!enabled || !name || name_length <= 0) return false;
    if (name_length <= 13 || std::strncmp(name, "Cstrike_Chat_", 13) != 0) return false;
    // "Cstrike_Chat_All", "Cstrike_Chat_All_Dead", "Cstrike_Chat_All_Spec".
    if (name_length >= 16 && std::strncmp(name + 13, "All", 3) == 0) return false;
    return true;
}

// Only lines from the other team: the local team's chat is drawn by the HUD already, and
// a message from a spectator or the server has no team to compare.
inline bool IsEnemyChat(bool enabled, int sender_team, int local_team) {
    if (!enabled) return false;
    if (sender_team != 2 && sender_team != 3) return false; // T = 2, CT = 3
    return local_team != sender_team;
}

// The Lua keeps a table of "when did this player last speak" and drops a repeat inside
// 0.4 s, because the same line can reach the client twice (the team copy and the
// spectator copy). One slot per sender mirrors that table without allocating.
constexpr unsigned long long kChatRepeatMs = 400;
constexpr int kChatGateSlots = 32;

struct ChatGate {
    struct Entry {
        int sender{-1};
        unsigned long long text_hash{};
        unsigned long long stamp_ms{};
    };
    Entry entries[kChatGateSlots]{};

    Entry* Slot(int sender) {
        Entry* free_slot = nullptr;
        for (auto& entry : entries) {
            if (entry.sender == sender) return &entry;
            if (free_slot == nullptr && entry.sender < 0) free_slot = &entry;
        }
        if (free_slot != nullptr) { free_slot->sender = sender; return free_slot; }
        // Table full: recycle the oldest speaker, the Lua's table would simply grow.
        Entry* oldest = &entries[0];
        for (auto& entry : entries) if (entry.stamp_ms < oldest->stamp_ms) oldest = &entry;
        oldest->sender = sender;
        oldest->text_hash = 0;
        oldest->stamp_ms = 0;
        return oldest;
    }
};

inline unsigned long long HashChatText(const char* text) {
    unsigned long long hash = 1469598103934665603ull; // FNV-1a
    if (!text) return hash;
    for (const char* it = text; *it; ++it) {
        hash ^= static_cast<unsigned char>(*it);
        hash *= 1099511628211ull;
    }
    return hash;
}

// True when this line was not already printed for this sender within the window.
inline bool ShouldAnnounce(ChatGate& gate, int sender, unsigned long long text_hash,
                           unsigned long long now_ms) {
    const ChatGate::Entry* entry = gate.Slot(sender);
    if (entry->text_hash != text_hash) return true;
    if (now_ms < entry->stamp_ms) return true;
    return now_ms - entry->stamp_ms >= kChatRepeatMs;
}

inline void RecordAnnounce(ChatGate& gate, int sender, unsigned long long text_hash,
                           unsigned long long now_ms) {
    ChatGate::Entry* entry = gate.Slot(sender);
    entry->text_hash = text_hash;
    entry->stamp_ms = now_ms;
}

// ---- rendering ----------------------------------------------------------
// The Lua does not print the raw line: it rebuilds the game's own localization string, so
// the reveal reads exactly like the message the client refused to draw.
//   Cstrike_Chat_T_Loc / Cstrike_Chat_CT_Loc ......... "(T) name @ place : text"
//   Cstrike_Chat_T_Dead / Cstrike_Chat_CT_Dead ....... "*DEAD*(T) name : text"
// The Lua picks the "Loc" spelling whenever the speaker is alive, and localizes an empty
// place name to UI_Unknown; "Unknown" is what that string says in this client's language.
inline const char* ChatTeamTag(int team) {
    if (team == 2) return "T";
    if (team == 3) return "CT";
    return nullptr;
}

inline const char* ChatDeadTag() { return "*DEAD*"; }
inline const char* ChatUnknownPlace() { return "Unknown"; }

// Copy a game string (place name) into place, dropping control codes and anything past the
// capacity. Returns false when nothing usable was left, which the caller renders as
// "Unknown".
inline bool SanitiseChatPlace(const char* source, char* place, int capacity) {
    if (!place || capacity < 2) return false;
    place[0] = '\0';
    if (!source) return false;
    int written = 0;
    for (const char* it = source; *it && written < capacity - 1; ++it) {
        const unsigned char c = static_cast<unsigned char>(*it);
        if (c < 0x20) continue;
        place[written++] = static_cast<char>(c);
    }
    place[written] = '\0';
    while (written > 0 && place[written - 1] == ' ') place[--written] = '\0';
    return written > 0;
}

// The line the chat panel would have shown. Returns an empty string when the caller passed
// something that cannot be a chat line (no team, no name or no text), so a malformed payload
// can never print half a line.
inline std::string FormatChatLine(int team, bool alive, const char* name, const char* place,
                                  const char* text) {
    const char* tag = ChatTeamTag(team);
    if (!tag || !name || !name[0] || !text) return {};
    std::string line;
    if (!alive) { line += ChatDeadTag(); }
    line += "(";
    line += tag;
    line += ") ";
    line += name;
    if (alive) {
        char safe_place[64]{};
        const bool has_place = SanitiseChatPlace(place, safe_place, static_cast<int>(sizeof(safe_place)));
        line += " @ ";
        line += has_place ? safe_place : ChatUnknownPlace();
    }
    line += " : ";
    line += text;
    return line;
}

void SetEnabled(bool enabled);
// Installs the vtable patch from the game thread once the switch was published.
void FrameStage(int stage);
void Shutdown();
} // namespace Misc::Chat

// ---------------------------------------------------------------------------
// Ping spike (ping.cpp): hooks CNetChan::SendDatagram and CNetChan::ProcessPacket and
// lowers the incoming sequence number, so the server measures the local client as
// laggier than it is. Ported from this repository's `main` branch: detour_hooks.cpp
// (send_datagram / process_packet), fake_lag.cpp (c_ping_spike) and structs.hpp
// (c_net_channel offsets).
// ---------------------------------------------------------------------------
namespace Misc::Ping {
// The menu slider runs 0..200 ms; anything above the ceiling is clamped, because the
// server rejects a shift larger than its own unlag budget anyway.
constexpr int kMaxSpikeMs = 200;

// sv_maxunlag defaults to 1.0 s in CS:GO. It is read from the ConVar when the read is
// plausible; these bounds are what decides "plausible".
constexpr float kDefaultMaxUnlag = 1.0f;
constexpr float kMinMaxUnlag = 0.05f;
constexpr float kMaxMaxUnlag = 2.0f;

// TIME_TO_TICKS, with the interval guarded: before the first map load it is zero, and
// dividing by it would be undefined.
inline int TicksForSeconds(float seconds, float interval) {
    if (!(interval > 0.f) || !(seconds > 0.f)) return 0;
    return static_cast<int>(0.5f + seconds / interval);
}

// `main` turns the requested delay into a delay *on top of* what the connection already
// has (outgoing latency and interpolation), then clamps it to the server's unlag budget.
// Parts that could not be read arrive as 0 and are simply skipped.
inline float SpikeSeconds(float desired_ms, float outgoing_latency, float lerp_time,
                          float max_unlag) {
    if (!(desired_ms > 0.f)) return 0.f;
    float correct = desired_ms / 1000.f;
    if (outgoing_latency > 0.f) correct -= outgoing_latency;
    if (lerp_time > 0.f) correct -= lerp_time;
    if (!(correct > 0.f)) return 0.f;
    if (max_unlag > 0.f && correct > max_unlag) return max_unlag;
    return correct;
}

// A sequence counter this feature is willing to touch. A real CNetChan counter is a small
// positive number; anything outside this range means +0x1C resolved into the wrong field,
// and writing there would corrupt the connection instead of delaying it.
constexpr int kMaxSequence = 1'000'000;

inline bool IsSequenceNumber(int value) {
    return value >= 0 && value <= kMaxSequence;
}

// The incoming sequence number is only ever lowered, and only while there is room: a
// value below the shift would wrap into negative territory and reconnect the client.
inline bool ShouldSpike(int in_sequence_nr, int ticks) {
    return IsSequenceNumber(in_sequence_nr) && ticks > 0 && in_sequence_nr > ticks;
}

// Called from the menu bridge on the render thread.
void Publish(bool enabled, float milliseconds);

// Installs the two detours from the game thread once the switch was published.
void FrameStage(int stage);
void Shutdown();
} // namespace Misc::Ping
