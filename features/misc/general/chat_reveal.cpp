#include "general.hpp"
#include "../../../context.hpp"
#include "../../../sdk/entity/player.hpp"
#include "../../../sdk/interfaces/interfaces.hpp"
#include "../../../utils/detourhook.hpp"
#include "../../../utils/log.hpp"
#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <windows.h>

namespace Misc::General::Detail {
namespace {
// The script's timings: a say is only judged 0.2s after it happened (by then its
// visible SayText2, if it had one, is long dispatched), and a visible-chat stamp
// within the last 0.4s still counts as normal chat. The per-player reveal
// dedupe keeps the same 0.4s the script used.
constexpr float kDecideDelay = 0.2f;
constexpr float kVisibleWindow = 0.4f;
constexpr float kRevealDedupe = 0.4f;
constexpr int kMaxPlayers = 64;
constexpr float kNoStamp = -1e3f;
constexpr size_t kMaxPending = 128;
constexpr size_t kMaxToasts = 32;
constexpr size_t kMaxLine = 240;

// SayText2 is user message type 6 (CCSUsrMsg_SayText2).
constexpr int kSayText2MsgType = 6;

// IBaseClientDLL::DispatchUserMessage is slot 38: the slot directly after
// FrameStageNotify (37), the one hooks.cpp already hooks on Interfaces::Client.
// The decompiled CHLClient (cstrike15_src, public/cdll_int.h) lists
// DispatchUserMessage(int, int32, int, const void*) immediately after
// FrameStageNotify(ClientFrameStage_t) with nothing between; the interface adds
// exactly one slot (IN_SetSampleTime) versus the public SDK the old
// "slot 37" numbering followed. The vtable patch itself is this build's
// in-place VMT_Hook, so a second slot on the same object is safe.
constexpr std::size_t kDispatchUserMessageSlot = 38;

std::atomic<bool> g_enabled{false};

// Game-thread state only: user messages and frame stage both run on the game
// thread, so the stamps and the pending list need no locking.
float g_visible_stamp[kMaxPlayers] = {}; // curtime of the last visible (chat==true) SayText2, per entity index - 1
float g_last_reveal[kMaxPlayers] = {};   // curtime of the last reveal per entity index - 1

struct Pending {
    int index; // 1-based entity index of the speaker
    std::string text;
    float decide_at;
};
std::vector<Pending> g_pending;

// Game thread -> render thread is the only crossing, through this pair.
std::mutex g_toast_mutex;
std::vector<ChatToast> g_toasts;

VMT_Hook g_hook;
bool g_attach_tried = false;

float Now() { return Interfaces::Globals ? Interfaces::Globals->curtime : 0.f; }

int LocalIndex() { return Interfaces::Engine ? Interfaces::Engine->GetLocalPlayer() : -1; }

// The speaker's name from the engine's player info (cl_playerinfo_t); the name
// field sits at +16 in this build, the same tested read as event_logs::Name.
void Name(int index, char* out, size_t size) {
    out[0] = 0;
    if (!Interfaces::Engine || index <= 0) return;
    alignas(8) unsigned char info[512]{};
    if (Interfaces::Engine->GetPlayerInfo(index, info)) {
        std::memcpy(out, info + 16, size - 1);
        out[size - 1] = 0;
        for (char* p = out; *p; ++p)
            if (static_cast<unsigned char>(*p) < 32 && *p) *p = ' ';
        return;
    }
    std::snprintf(out, size, "Player %d", index);
}

// SayText2 payload (CCSUsrMsg_SayText2): f1 varint ent_idx, f2 varint chat,
// f3 string msg_name, f4 repeated string params, f5 varint textallchat. Only
// ent_idx and chat matter here, so a bounded wire walk is enough; any
// inconsistency stops the parse instead of reading past the payload.
void ParseSayText2(const void* msg, int size, int& ent_idx, bool& chat) {
    ent_idx = 0;
    chat = false;
    if (!msg || size <= 0) return;
    const auto* p = static_cast<const uint8_t*>(msg);
    const auto* end = p + size;

    auto read_varint = [&](uint32_t& out) -> bool {
        out = 0;
        int shift = 0;
        while (p < end) {
            const uint8_t byte = *p++;
            out |= static_cast<uint32_t>(byte & 0x7Fu) << shift;
            if (!(byte & 0x80u)) return true;
            if ((shift += 7) >= 32) return true;
        }
        return false;
    };

    while (p < end) {
        uint32_t tag = 0;
        if (!read_varint(tag)) return;
        const uint32_t field = tag >> 3;
        const uint32_t wire = tag & 7u;
        if (wire == 0u) {
            uint32_t value = 0;
            if (!read_varint(value)) return;
            if (field == 1u) ent_idx = static_cast<int>(value);
            else if (field == 2u) chat = value != 0;
        } else if (wire == 2u) {
            uint32_t length = 0;
            if (!read_varint(length)) return;
            if (static_cast<uint32_t>(end - p) < length) return;
            p += length;
        } else if (wire == 5u) {
            if (static_cast<uint32_t>(end - p) < 4) return;
            p += 4;
        } else if (wire == 1u) {
            if (static_cast<uint32_t>(end - p) < 8) return;
            p += 8;
        } else {
            return; // unknown wire type: do not trust the payload
        }
    }
}

using DispatchUserMessageFn = bool (__thiscall*)(void*, int, std::int32_t, int, const void*);

// IBaseClientDLL::DispatchUserMessage hook: stamp the player whenever a VISIBLE
// (chat==true) SayText2 goes by. The original is always called, last, so the
// HUD receives every message no matter what this does.
bool __thiscall DispatchUserMessage(void* self, int msg_type, std::int32_t flags, int size,
                                    const void* msg) {
    if (msg_type == kSayText2MsgType && g_enabled.load(std::memory_order_relaxed)) {
        int ent_idx = 0;
        bool chat = false;
        ParseSayText2(msg, size, ent_idx, chat);
        if (chat && ent_idx >= 1 && ent_idx < kMaxPlayers)
            g_visible_stamp[ent_idx - 1] = Now();
    }
    if (const auto original = g_hook.get_original<DispatchUserMessageFn>())
        return original(self, msg_type, flags, size, msg);
    return true;
}

void ResetStamps() {
    for (auto& stamp : g_visible_stamp) stamp = kNoStamp;
    for (auto& stamp : g_last_reveal) stamp = kNoStamp;
}
} // namespace

void ChatRevealPublish(bool enabled) {
    const bool was = g_enabled.exchange(enabled, std::memory_order_relaxed);
    if (!enabled && was) g_pending.clear(); // never judge a say once the feature is off
}

void ChatRevealOnPlayerSay(const IGameEvent* event) {
    if (!event || !g_enabled.load(std::memory_order_relaxed)) return;
    const int user_id = event->GetInt("userid");
    const char* text = event->GetString("text");
    if (user_id <= 0 || !text || !*text) return; // 0 = console say: nothing to reveal
    if (!Interfaces::Engine || !Interfaces::EntityList) return;

    const int index = Interfaces::Engine->GetPlayerForUserID(user_id);
    if (index <= 0 || index == LocalIndex()) return; // not a player, or our own say

    auto* entity = Interfaces::EntityList->GetClientEntity(index);
    const auto* speaker = static_cast<CCSPlayer*>(entity);
    if (!speaker) return;

    // Combatant sides only, same rule as event_logs::IsTeammateIndex (teams 2/3
    // in CS:GO). Our own team's team chat is already visible to us in-game, so
    // only an enemy speaker can carry a hidden line.
    const int team = speaker->m_iTeamNum();
    if (team != 2 && team != 3) return;
    if (ctx.local && team == ctx.local->m_iTeamNum()) return;

    if (g_pending.size() >= kMaxPending) return;
    g_pending.push_back(Pending{index, text, Now() + kDecideDelay});
}

void ChatRevealOnRoundStart() {
    // curtime is continuous across rounds but restarts per map; stale stamps
    // would otherwise suppress the first reveal after a map change.
    ResetStamps();
    g_pending.clear();
}

void ChatRevealFrameStage(int /*stage*/) {
    // Lazy attach, the auto-accept pattern: once the feature is on and the
    // client interface exists, hook slot 38; the hook itself stays attached and
    // just early-outs while the feature is off.
    if (g_enabled.load(std::memory_order_relaxed) && !g_attach_tried) {
        if (!Interfaces::Client) {
            static bool logged = false;
            if (!logged) {
                logged = true;
                LOG_WARN("chat reveal: IBaseClientDLL not captured yet; retrying");
            }
            return;
        }
        g_attach_tried = true;
        if (!g_hook.attach(Interfaces::Client, kDispatchUserMessageSlot,
                           reinterpret_cast<void*>(&DispatchUserMessage))) {
            g_attach_tried = false; // allow a retry next frame
            LOG_WARN("chat reveal: could not hook DispatchUserMessage slot %zu",
                     kDispatchUserMessageSlot);
            return;
        }
        LOG_INFO("chat reveal: DispatchUserMessage (slot %zu) hooked", kDispatchUserMessageSlot);
    }

    if (g_pending.empty()) return;
    if (!g_enabled.load(std::memory_order_relaxed)) {
        g_pending.clear();
        return;
    }
    const float now = Now();
    if (now <= 0.f) return;

    for (size_t i = 0; i < g_pending.size();) {
        Pending& item = g_pending[i];
        if (now < item.decide_at) {
            ++i;
            continue;
        }
        // Move out before erasing: the erase invalidates the reference.
        Pending done = std::move(item);
        g_pending.erase(g_pending.begin() + i);
        if (done.index < 1 || done.index >= kMaxPlayers) continue;
        const int slot = done.index - 1;

        // A visible (all-chat or own-team) SayText2 for this player went by
        // moments ago: the server fires player_say for every say, so a normal
        // chat shows up here too and is dropped.
        if (now - g_visible_stamp[slot] <= kVisibleWindow) continue;

        // The script's per-player dedupe: one reveal line per player per 0.4s.
        if (now - g_last_reveal[slot] < kRevealDedupe) continue;
        g_last_reveal[slot] = now;

        char name[64]{};
        Name(done.index, name, sizeof(name));

        auto* entity = Interfaces::EntityList ? Interfaces::EntityList->GetClientEntity(item.index)
                                              : nullptr;
        const char* location =
            entity ? static_cast<CCSPlayer*>(entity)->LastPlaceName() : "";

        std::string line = name;
        line += ' ';
        line += done.text;
        if (location && *location) {
            line += " @ ";
            line += location;
        }
        if (line.size() > kMaxLine) line.resize(kMaxLine);

        const std::lock_guard lock(g_toast_mutex);
        if (g_toasts.size() < kMaxToasts)
            g_toasts.push_back(ChatToast{"Enemy team chat", std::move(line)});
    }
}

void DrainChatToasts(std::vector<ChatToast>& out) {
    const std::lock_guard lock(g_toast_mutex);
    out.swap(g_toasts);
}

void ChatRevealShutdown() {
    __try {
        g_hook.detach();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    g_attach_tried = false;
    g_pending.clear();
    const std::lock_guard lock(g_toast_mutex);
    g_toasts.clear();
}
} // namespace Misc::General::Detail
