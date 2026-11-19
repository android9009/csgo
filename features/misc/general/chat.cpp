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
constexpr const char* kAllChatName = "Cstrike_Chat_All";

using DispatchFn = bool(__fastcall*)(void*, void*, int, int, const void*);

VMT_Hook g_hook;
std::atomic<bool> g_enabled{false};
std::atomic<bool> g_want{false};
bool g_attach_tried{};
bool g_reported{};
bool g_layout_reported{};

// Reads the payload pointer out of the bf_read object. The field is found by validation
// (see Misc::Chat::FindPayloadField), so a client build whose bf_read keeps the buffer
// somewhere else resolves itself instead of writing into the wrong bytes.
const unsigned char* Payload(std::uintptr_t* fields, int data_bytes) {
    if (!fields || data_bytes <= 0) return nullptr;
    int index = -1;
    __try {
        index = FindPayloadField(fields, kReadFieldCount, data_bytes);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        index = -1;
    }
    if (index < 0) return nullptr;
    if (!g_layout_reported) {
        g_layout_reported = true;
        LOG_INFO("misc: reveal enemy chat - bf_read payload field +0x%X",
                 index * static_cast<int>(sizeof(std::uintptr_t)));
    }
    return reinterpret_cast<const unsigned char*>(fields[index]);
}

bool __fastcall Dispatch(void* self, void* edx, int type, int data_bytes, const void* read_object) {
    const auto original = g_hook.get_original<DispatchFn>();
    if (!original) return false;
    if (!g_enabled.load(std::memory_order_relaxed) || type != kSayText2 || !read_object ||
        data_bytes <= 2 || data_bytes > kMaxPayload)
        return original(self, edx, type, data_bytes, read_object);

    auto* fields = const_cast<std::uintptr_t*>(static_cast<const std::uintptr_t*>(read_object));
    const auto* bytes = Payload(fields, data_bytes);
    if (!bytes) return original(self, edx, type, data_bytes, read_object);

    const int name_offset = FindChatNameOffset(bytes, data_bytes);
    if (name_offset < 0) return original(self, edx, type, data_bytes, read_object);

    const int name_length = bytes[name_offset];
    const char* name = reinterpret_cast<const char*>(bytes + name_offset + 1);
    if (!ShouldRevealChat(true, name, name_length))
        return original(self, edx, type, data_bytes, read_object);

    // Only the name field changes; the prefix and everything after the name are copied
    // byte for byte, so the rest of the payload keeps its exact original encoding.
    const int new_length = static_cast<int>(std::strlen(kAllChatName));
    unsigned char buffer[kMaxPayload + 8]{};
    const int prefix = name_offset;
    const int suffix_offset = name_offset + 1 + name_length;
    const int suffix = data_bytes - suffix_offset;
    const int total = prefix + 1 + new_length + suffix;
    if (total > static_cast<int>(sizeof(buffer)))
        return original(self, edx, type, data_bytes, read_object);

    std::memcpy(buffer, bytes, prefix);
    buffer[prefix] = static_cast<unsigned char>(new_length);
    std::memcpy(buffer + prefix + 1, kAllChatName, new_length);
    std::memcpy(buffer + prefix + 1 + new_length, bytes + suffix_offset, suffix);

    // Round-trip: the rebuilt buffer must still parse to the same field, now all-chat.
    if (FindChatNameOffset(buffer, total) != prefix)
        return original(self, edx, type, data_bytes, read_object);

    // The game's own read object is repointed at the rebuilt payload; every field that
    // holds the buffer start, its end or its size is updated. If the layout does not
    // match what the object actually contains, the message goes through unchanged.
    Repoint repoint;
    __try {
        repoint = RepointRead(fields, kReadFieldCount, bytes, data_bytes, buffer, total);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        repoint = Repoint{};
    }
    if (!repoint.ok())
        return original(self, edx, type, data_bytes, read_object);

    if (!g_reported) {
        g_reported = true;
        LOG_INFO("misc: reveal enemy chat - \"%.*s\" re-addressed as all-chat "
                 "(%d bytes -> %d, %d pointer field(s), %d size field(s))",
                 name_length, name, data_bytes, total, repoint.pointer_fields,
                 repoint.length_fields);
    }
    return original(self, edx, type, total, read_object);
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
