#include "other.hpp"
#include "../../../sdk/interfaces/interfaces.hpp"
#include "../../../utils/detourhook.hpp"
#include "../../../utils/log.hpp"
#include "../../../utils/memory.hpp"
#include "../../../utils/patterns/catalog.hpp"
#include <atomic>
#include <cstdint>

namespace Misc::Other {
namespace {
// c_net_message::get_type - vtable slot 7 (this repo's `main` structs.hpp c_net_message:
// 0 dtor, 1 set_net_channel, 2 set_reliable, 3 process, 4 read_from_buffer, 5 write_to_buffer,
// 6 is_reliable, 7 get_type).
constexpr std::size_t kNetMsgGetTypeSlot = 7;
// SV_PUREREADY - the server->client message behind the "server is not pure" popup
// (main detour_hooks.cpp: msg.get_type() == 14).
constexpr int kSvMsgPure = 14;

using SendNetMsgFn = bool(__fastcall*)(void*, void*, void*, bool, bool);
using GetNetMsgTypeFn = int(__thiscall*)(void* const);

Detour_Hook g_hook;
void* g_original = nullptr;
bool g_attach_tried = false;

// `main` detour_hooks.cpp send_net_msg, verbatim logic: the message is dropped only on the
// local channel (engine->get_net_channel(), slot 78) and only while the bypass is selected.
bool __fastcall SendNetMsg(void* netchan, void* edx, void* msg, bool reliable, bool voice) {
    const auto original = reinterpret_cast<SendNetMsgFn>(g_original);
    if (Interfaces::Engine && netchan == Interfaces::Engine->GetNetChannelInfo() &&
        Detail::SvPure() && msg) {
        int type = 0;
        __try {
            const auto fn = reinterpret_cast<GetNetMsgTypeFn>(
                (*reinterpret_cast<void***>(msg))[kNetMsgGetTypeSlot]);
            if (fn) type = fn(msg);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        if (type == kSvMsgPure) return false;
    }
    return original ? original(netchan, edx, msg, reliable, voice) : false;
}
} // namespace

namespace Detail {
// Attached once, the same way `main` does (ADD_DETOUR at hook setup); the bypass gate lives
// inside the hook, so no attach/detour churn while the user toggles the item.
void SvPureFrameStage(int) {
    if (g_attach_tried) return;
    g_attach_tried = true;

    auto rva = Patterns::Get("send_net_msg");
    if (!rva) rva = Patterns::Get("send_net_msg_alt");
    const auto base = reinterpret_cast<std::uintptr_t>(MEM::GetModuleBase("engine.dll"));
    if (!rva || !base) {
        LOG_WARN("other sv_pure: send_net_msg did not resolve; the bypass stays off");
        return;
    }
    if (g_hook.attach(reinterpret_cast<void*>(base + rva),
                      reinterpret_cast<void*>(&SendNetMsg), &g_original)) {
        LOG_INFO("other sv_pure: SendNetMsg detour attached at engine.dll+0x%X",
                 static_cast<unsigned int>(rva));
    } else {
        g_attach_tried = false; // allow a retry
        LOG_WARN("other sv_pure: SendNetMsg detour could not be created");
    }
}

void SvPureShutdown() {
    if (g_attach_tried) {
        g_attach_tried = false;
        g_hook.detach();
    }
}
} // namespace Detail
} // namespace Misc::Other
