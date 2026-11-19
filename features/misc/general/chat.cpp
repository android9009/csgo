#include "general.hpp"
#include "../../../sdk/interfaces/interfaces.hpp"
#include "../../../utils/detourhook.hpp"
#include "../../../utils/log.hpp"
#include <atomic>
#include <cstring>
#include <windows.h>

namespace Misc::Chat {
namespace {
// IBaseClientDll::DispatchUserMessage - slot 38, the same table numbering as
// FrameStageNotify (37), which this build already hooks.
constexpr std::size_t kDispatchSlot = 38;
constexpr int kSayText2 = 6; // ECstrike15UserMessages_CS_UM_SayText2
// The buffer handed to the hook is one usermessage payload; anything larger is not chat.
constexpr int kMaxPayload = 512;
// A chat message name is a one-byte length plus printable ASCII ("Cstrike_Chat_CT_Loc").
constexpr int kNameScanWindow = 32;
constexpr const char* kAllChatName = "Cstrike_Chat_All";

using DispatchFn = bool(__fastcall*)(void*, void*, int, int, int, const void*);

VMT_Hook g_hook;
std::atomic<bool> g_enabled{false};
std::atomic<bool> g_want{false};
bool g_attach_tried{};
bool g_reported{};

// Finds the length-prefixed message name without assuming a field order: the public
// descriptions of this payload disagree on the header, but every variant carries the
// name as "<length byte><name>", and the name always starts with "Cstrike_Chat_".
// Returns the offset of the length byte, or -1.
int FindNameOffset(const unsigned char* data, int size) {
    if (!data || size < 3) return -1;
    const int limit = size < kNameScanWindow ? size : kNameScanWindow;
    for (int offset = 0; offset + 2 < limit; ++offset) {
        const int length = data[offset];
        if (length < 13 || length > 40) continue; // shorter than "Cstrike_Chat_T"
        if (offset + 1 + length > size) continue;
        if (std::strncmp(reinterpret_cast<const char*>(data + offset + 1), "Cstrike_Chat_", 13) != 0)
            continue;
        bool printable = true;
        for (int i = 0; i < length; ++i) {
            const unsigned char c = data[offset + 1 + i];
            if (c < 0x20 || c > 0x7E) {
                printable = false;
                break;
            }
        }
        if (!printable) continue;
        return offset;
    }
    return -1;
}

bool __fastcall Dispatch(void* self, void* edx, int type, int pass_through, int length,
                         const void* data) {
    const auto original = g_hook.get_original<DispatchFn>();
    if (!original) return false;
    if (!g_enabled.load(std::memory_order_relaxed) || type != kSayText2 || !data || length <= 2 ||
        length > kMaxPayload)
        return original(self, edx, type, pass_through, length, data);

    const auto* bytes = static_cast<const unsigned char*>(data);
    const int name_offset = FindNameOffset(bytes, length);
    if (name_offset < 0) return original(self, edx, type, pass_through, length, data);

    const int name_length = bytes[name_offset];
    const char* name = reinterpret_cast<const char*>(bytes + name_offset + 1);
    if (!ShouldRevealChat(true, name, name_length))
        return original(self, edx, type, pass_through, length, data);

    // Only the name field changes; the prefix and everything after the name are copied
    // byte for byte, so the rest of the payload keeps its exact original encoding.
    const int new_length = static_cast<int>(std::strlen(kAllChatName));
    unsigned char buffer[kMaxPayload + 8]{};
    const int prefix = name_offset;
    const int suffix_offset = name_offset + 1 + name_length;
    const int suffix = length - suffix_offset;
    const int total = prefix + 1 + new_length + suffix;
    if (total > static_cast<int>(sizeof(buffer))) return original(self, edx, type, pass_through, length, data);

    std::memcpy(buffer, bytes, prefix);
    buffer[prefix] = static_cast<unsigned char>(new_length);
    std::memcpy(buffer + prefix + 1, kAllChatName, new_length);
    std::memcpy(buffer + prefix + 1 + new_length, bytes + suffix_offset, suffix);

    // Round-trip: the rebuilt buffer must still parse to the same field, now all-chat.
    if (FindNameOffset(buffer, total) != prefix)
        return original(self, edx, type, pass_through, length, data);

    if (!g_reported) {
        g_reported = true;
        LOG_INFO("misc: reveal enemy chat - re-addressed \"%.*s\" as all-chat", name_length, name);
    }
    return original(self, edx, type, pass_through, total, buffer);
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
        LOG_WARN("misc: IBaseClientDll missing; reveal enemy chat cannot hook DispatchUserMessage");
        return;
    }
    if (!g_hook.attach(Interfaces::Client, kDispatchSlot, reinterpret_cast<void*>(&Dispatch))) {
        LOG_WARN("misc: reveal enemy chat could not hook DispatchUserMessage slot %zu",
                 kDispatchSlot);
        g_attach_tried = false;
        return;
    }
    LOG_INFO("misc: reveal enemy chat hooked DispatchUserMessage (slot %zu)", kDispatchSlot);
}

void Shutdown() { g_hook.detach(); }
} // namespace Misc::Chat
