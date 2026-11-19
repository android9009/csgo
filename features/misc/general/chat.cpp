#include "general.hpp"
#include "../../../context.hpp"
#include "../../../sdk/entity/player.hpp"
#include "../../../sdk/interfaces/interfaces.hpp"
#include "../../../utils/detourhook.hpp"
#include "../../../utils/log.hpp"
#include <atomic>
#include <cstring>
#include <windows.h>

namespace Misc::Chat {
namespace {
// IBaseClientDll::DispatchUserMessage - slot 38, the same table numbering as
// FrameStageNotify (37), which this build already hooks. The signature is
// (this, int msg_type, int data_bytes, bf_read& msg): the third argument is the read
// object, *not* the raw payload, and there is no fourth argument.
constexpr std::size_t kDispatchSlot = 38;
constexpr int kSayText2 = 6; // ECstrike15UserMessages_CS_UM_SayText2
constexpr int kMaxPayload = 1024;

// tier0's own print entry points, decorated by MSVC. Resolving them keeps the reveal
// independent of the console filter hook, which patches the same exports.
struct Color {
    unsigned char r, g, b, a;
};
using MsgFn = void(__cdecl*)(const char*, ...);
using ColorMsgFn = void(__cdecl*)(const Color&, const char*, ...);

MsgFn ResolveMsg() {
    static MsgFn fn = [] () -> MsgFn {
        const HMODULE tier0 = GetModuleHandleA("tier0.dll");
        if (!tier0) return nullptr;
        for (const char* name : {"?Msg@@YAXPBDZZ", "Msg", "?ConMsg@@YAXPBDZZ", "ConMsg"}) {
            if (auto* address = reinterpret_cast<MsgFn>(GetProcAddress(tier0, name))) return address;
        }
        return nullptr;
    }();
    return fn;
}

ColorMsgFn ResolveColorMsg() {
    static ColorMsgFn fn = [] () -> ColorMsgFn {
        const HMODULE tier0 = GetModuleHandleA("tier0.dll");
        if (!tier0) return nullptr;
        for (const char* name : {"?ConColorMsg@@YAXABVColor@@PBDZZ", "ConColorMsg"}) {
            if (auto* address = reinterpret_cast<ColorMsgFn>(GetProcAddress(tier0, name)))
                return address;
        }
        return nullptr;
    }();
    return fn;
}

VMT_Hook g_hook;
std::atomic<bool> g_enabled{false};
std::atomic<bool> g_want{false};
bool g_attach_tried{};
bool g_reported_hook{};
bool g_layout_reported{};
bool g_announce_reported{};
int g_payload_field{-1};
ChatGate g_gate;

// Prints one revealed line. The prefix says where it came from, because this line did not
// travel through the HUD and therefore does not show up in the chat panel.
void PrintLine(const SayText2& message) {
    Color color{170, 210, 255, 255};
    if (const auto color_msg = ResolveColorMsg()) {
        color_msg(color, "\x07[chat] %s : %s\n", message.speaker, message.text);
        return;
    }
    if (const auto msg = ResolveMsg()) msg("[chat] %s : %s\n", message.speaker, message.text);
}

int TeamOf(int index) {
    if (!Interfaces::EntityList || index <= 0) return 0;
    auto* player = static_cast<CCSPlayer*>(Interfaces::EntityList->GetClientEntity(index));
    return player ? player->m_iTeamNum() : 0;
}

// Everything below only *reads* game memory, so a surprise means the message is passed on
// untouched; the guarded region exists to survive a build whose read object looks nothing
// like the published layouts.
void Analyse(const void* read_object, int data_bytes) {
    const auto* fields = static_cast<const std::uintptr_t*>(read_object);
    if (g_payload_field < 0) {
        const int found = FindPayloadField(fields, kReadFieldCount, data_bytes);
        if (found < 0) return;
        g_payload_field = found;
        if (!g_layout_reported) {
            g_layout_reported = true;
            LOG_INFO("misc: reveal enemy chat - bf_read payload field +0x%X",
                     found * static_cast<int>(sizeof(std::uintptr_t)));
        }
    }
    const auto* bytes = reinterpret_cast<const unsigned char*>(fields[g_payload_field]);
    if (!bytes) return;

    SayText2 message;
    if (!ParseSayText2(bytes, data_bytes, message)) return;
    if (!ShouldRevealChat(true, message.name, message.name_length)) return;
    const int local_index = Interfaces::Engine ? Interfaces::Engine->GetLocalPlayer() : 0;
    if (message.ent_index == local_index) return;
    const int local_team = ctx.local ? ctx.local->m_iTeamNum() : 0;
    const int sender_team = TeamOf(message.ent_index);
    if (!IsEnemyChat(true, sender_team, local_team)) return;

    const unsigned long long now = GetTickCount64();
    const unsigned long long hash = HashChatText(message.text);
    if (!ShouldAnnounce(g_gate, message.ent_index, hash, now)) return;
    g_gate.sender = message.ent_index;
    g_gate.text_hash = hash;
    g_gate.stamp_ms = now;

    PrintLine(message);
    if (!g_announce_reported) {
        g_announce_reported = true;
        LOG_INFO("misc: reveal enemy chat - printing enemy team chat (\"%s\" as \"%s\", %s)",
                 message.text, message.speaker, message.name);
    }
}

bool __fastcall Dispatch(void* self, void* edx, int type, int data_bytes, const void* read_object) {
    const auto original = g_hook.get_original<bool(__fastcall*)(void*, void*, int, int, const void*)>();
    if (!original) return false;
    if (type != kSayText2 || !read_object || data_bytes <= 2 || data_bytes > kMaxPayload ||
        !g_enabled.load(std::memory_order_relaxed))
        return original(self, edx, type, data_bytes, read_object);

    __try {
        Analyse(read_object, data_bytes);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    // The message itself is forwarded exactly as it arrived: this feature adds a line, it
    // does not change what the client does with the message.
    return original(self, edx, type, data_bytes, read_object);
}
} // namespace

void SetEnabled(bool enabled) {
    // The switch is published from the render thread; the vtable is only patched from the
    // game thread, in FrameStage, because the game thread is the one reading the slot.
    g_enabled.store(enabled, std::memory_order_relaxed);
    g_want.store(enabled, std::memory_order_relaxed);
}

void FrameStage(int /*stage*/) {
    if (!g_want.load(std::memory_order_relaxed) || g_attach_tried) return;
    g_attach_tried = true;
    if (!Interfaces::Client) {
        LOG_WARN("misc: reveal enemy chat missing; IBaseClientDll is not available");
        return;
    }
    if (!g_hook.attach(Interfaces::Client, kDispatchSlot, reinterpret_cast<void*>(&Dispatch))) {
        LOG_WARN("misc: reveal enemy chat could not hook DispatchUserMessage slot %zu",
                 kDispatchSlot);
        g_attach_tried = false;
        return;
    }
    LOG_INFO("misc: reveal enemy chat hooked DispatchUserMessage (slot %zu)", kDispatchSlot);
    if (!g_reported_hook) {
        g_reported_hook = true;
        if (!ResolveColorMsg() && !ResolveMsg())
            LOG_WARN("misc: reveal enemy chat - tier0 print functions are not available");
    }
}

void Shutdown() { g_hook.detach(); }
} // namespace Misc::Chat
