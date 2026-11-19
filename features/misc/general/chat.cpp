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
// FrameStageNotify (37), which this build already hooks. The signature is
// (this, int msg_type, int data_bytes, bf_read& msg): the third argument is the read
// object, *not* the raw payload, and there is no fourth argument.
constexpr std::size_t kDispatchSlot = 38;
constexpr int kSayText2 = 6; // ECstrike15UserMessages_CS_UM_SayText2
// One usermessage cannot carry more than this; anything larger is not chat.
constexpr int kMaxPayload = 512;
// Fields of the read object this feature is willing to look at. The published layouts
// keep the payload pointer at +0x18..+0x20, i.e. inside the first nine words.
constexpr int kObjectWords = kReadFieldCount;
constexpr const char* kAllChatName = "Cstrike_Chat_All";

using DispatchFn = bool(__fastcall*)(void*, void*, int, int, const void*);

VMT_Hook g_hook;
std::atomic<bool> g_enabled{false};
std::atomic<bool> g_want{false};
bool g_attach_tried{};
bool g_reported{};
bool g_layout_reported{};
int g_payload_field{-1};

// Copies the read object into our own buffer and repoints the copy at the rebuilt
// payload. The game's object is never written to: a field outside the real object would
// otherwise be somebody else's stack slot, and corrupting that is a crash far away from
// here. Returns the number of bytes to pass on, or -1 to leave the message alone.
int PrepareRewrite(const void* read_object, int data_bytes, unsigned char* rebuilt, int total,
                   std::uintptr_t* copy) {
    std::memcpy(copy, read_object, sizeof(std::uintptr_t) * kObjectWords);
    const auto* payload = reinterpret_cast<const unsigned char*>(copy[g_payload_field]);
    if (!payload) return -1;
    const Repoint repoint = RepointRead(copy, kObjectWords, payload, data_bytes, rebuilt, total);
    if (!repoint.ok()) return -1;
    if (!g_reported) {
        g_reported = true;
        LOG_INFO("misc: reveal enemy chat - message rewritten (%d bytes -> %d, %d pointer "
                 "field(s), %d size field(s), payload at word %d)",
                 data_bytes, total, repoint.pointer_fields, repoint.length_fields,
                 g_payload_field);
    }
    return total;
}

bool __fastcall Dispatch(void* self, void* edx, int type, int data_bytes, const void* read_object) {
    const auto original = g_hook.get_original<DispatchFn>();
    if (!original) return false;
    if (!g_enabled.load(std::memory_order_relaxed) || type != kSayText2 || !read_object ||
        data_bytes <= 2 || data_bytes > kMaxPayload)
        return original(self, edx, type, data_bytes, read_object);

    // Everything below touches memory the game owns, so one guard covers the whole
    // analysis: a surprise means the message is passed through untouched.
    std::uintptr_t copy[kObjectWords]{};
    unsigned char rebuilt[kMaxPayload + 8]{};
    int total = -1;
    __try {
        const auto* fields = static_cast<const std::uintptr_t*>(read_object);
        if (g_payload_field < 0) {
            const int found = FindPayloadField(fields, kObjectWords, data_bytes);
            if (found < 0) total = -1;
            else {
                g_payload_field = found;
                if (!g_layout_reported) {
                    g_layout_reported = true;
                    LOG_INFO("misc: reveal enemy chat - bf_read payload field +0x%X",
                             found * static_cast<int>(sizeof(std::uintptr_t)));
                }
            }
        }
        if (g_payload_field >= 0) {
            const auto* bytes = reinterpret_cast<const unsigned char*>(fields[g_payload_field]);
            const int name_offset = bytes ? FindChatNameOffset(bytes, data_bytes) : -1;
            if (name_offset >= 0) {
                const int name_length = bytes[name_offset];
                const char* name = reinterpret_cast<const char*>(bytes + name_offset + 1);
                if (ShouldRevealChat(true, name, name_length)) {
                    // Only the name field changes; every other byte is copied verbatim.
                    const int new_length = static_cast<int>(std::strlen(kAllChatName));
                    const int suffix_offset = name_offset + 1 + name_length;
                    const int suffix = data_bytes - suffix_offset;
                    const int new_total = name_offset + 1 + new_length + suffix;
                    if (suffix >= 0 && new_total > 0 &&
                        new_total <= static_cast<int>(sizeof(rebuilt))) {
                        std::memcpy(rebuilt, bytes, name_offset);
                        rebuilt[name_offset] = static_cast<unsigned char>(new_length);
                        std::memcpy(rebuilt + name_offset + 1, kAllChatName, new_length);
                        std::memcpy(rebuilt + name_offset + 1 + new_length,
                                    bytes + suffix_offset, suffix);
                        // Round-trip: the rebuilt buffer must still parse, now as all-chat.
                        if (FindChatNameOffset(rebuilt, new_total) == name_offset) {
                            total = PrepareRewrite(read_object, data_bytes, rebuilt, new_total, copy);
                        }
                    }
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        total = -1;
    }

    if (total <= 0 || copy[g_payload_field] == 0)
        return original(self, edx, type, data_bytes, read_object);

    // The copy is the object the game reads from here on; it lives until the call returns.
    return original(self, edx, type, total, copy);
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
