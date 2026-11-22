#include "other.hpp"
#include "../../../sdk/interfaces/interfaces.hpp"
#include "../../../utils/detourhook.hpp"
#include "../../../utils/log.hpp"
#include "../../../utils/memory.hpp"
#include "../../../utils/patterns/patterns.hpp"
#include <atomic>
#include <cstdint>
#include <intrin.h>

namespace Misc::Other {
namespace {
// IEngineClient::IsHLTV - slot 93 (this repo's `main` interfaces.hpp: VFUNC(is_hltv, ..., 93)).
constexpr std::size_t kIsHlTVSlot = 93;
using IsHlTVFn = bool(__fastcall*)(void*, void*);

VMT_Hook g_hook;
bool g_attach_tried = false;

// The return address of the radar's IsHLTV() call site, resolved from the pattern catalogue
// (`main` offsets.cpp, two build candidates). 0 until resolved / on a build without it.
std::uintptr_t ShowRadarReturnAddr() {
    static std::uintptr_t addr = 0;
    static bool resolved = false;
    if (!resolved) {
        resolved = true;
        const auto base = reinterpret_cast<std::uintptr_t>(MEM::GetModuleBase("client.dll"));
        auto rva = Patterns::Get("return_addr_show_radar");
        if (!rva) rva = Patterns::Get("return_addr_show_radar_alt");
        if (base && rva) {
            addr = base + rva;
            LOG_INFO("other radar: show-radar return address at client.dll+0x%X",
                     static_cast<unsigned int>(rva));
        } else {
            LOG_WARN("other radar: return_addr_show_radar did not resolve; force radar stays off");
        }
    }
    return addr;
}

// `main` vmt_hooks.cpp is_hltv: the minimap only draws enemy dots for HLTV spectators, so
// answering true from the radar's call site shows them while alive and on a team.
bool __fastcall IsHlTV(void* ecx, void* edx) {
    const auto original = g_hook.get_original<IsHlTVFn>();
    if (Detail::Radar()) {
        const auto target = ShowRadarReturnAddr();
        if (target && static_cast<std::uintptr_t>(_ReturnAddress()) == target)
            return true;
    }
    return original ? original(ecx, edx) : false;
}
} // namespace

namespace Detail {
// Attached lazily, once the engine interface is up and the user turned the item on. The hook
// stays attached afterwards (it just forwards to the original while the item is off).
void RadarFrameStage(int) {
    if (!Detail::Radar() || g_attach_tried) return;
    if (!Interfaces::Engine) return; // interface not up yet: retry next frame
    g_attach_tried = true;
    if (g_hook.attach(Interfaces::Engine, kIsHlTVSlot, reinterpret_cast<void*>(&IsHlTV))) {
        LOG_INFO("other radar: IEngineClient::IsHLTV (slot %zu) hooked", kIsHlTVSlot);
    } else {
        g_attach_tried = false; // allow a retry if the interface was only briefly unavailable
        LOG_WARN("other radar: could not hook IsHLTV slot %zu", kIsHlTVSlot);
    }
}

void RadarShutdown() { g_hook.detach(); }
} // namespace Detail
} // namespace Misc::Other
