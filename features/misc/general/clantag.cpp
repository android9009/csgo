#include "general.hpp"
#include "../../../context.hpp"
#include "../../../sdk/interfaces/interfaces.hpp"
#include "../../../utils/memory.hpp"
#include "../../../utils/offsets/offsets.hpp"
#include "../../../utils/log.hpp"
#include <string>

namespace Misc::General::Detail {
namespace {
using SetClanTagFn = void(__fastcall*)(const char*, const char*);
const char* const kDefaultTag = " airflow "; // `main` c_clantag::tag_desc default

// `main` c_clantag::set_clan_tag: the game's SetClanTag(name, tag) at a pinned client.dll
// offset. The tag is rewritten roughly twice a second so teammates see it animate.
SetClanTagFn ResolveSetClanTag() {
    static SetClanTagFn fn = nullptr;
    if (!fn) {
        const auto offset = offsets::Get("dwSetClanTag");
        const auto base = reinterpret_cast<std::uintptr_t>(MEM::GetModuleBase("client.dll"));
        if (offset && base) fn = reinterpret_cast<SetClanTagFn>(base + offset);
    }
    return fn;
}
} // namespace

void ClantagFrameStage(int) {
    // `main` c_clantag::run, ported: the tag time is (int)(curtime*2)+ping, so the visible
    // tag changes about every 500ms; we re-assert it whenever that value changes.
    if (!ctx.in_game || !ctx.local) return;

    const auto fn = ResolveSetClanTag();
    if (!fn) return;

    static int last_change_time = 0;
    static bool reset_tag = true;

    // `main` adds HACKS->ping to the tag clock (vmt_hooks.cpp:558: the incoming net latency
    // plus the outgoing choke); the incoming latency is the observable part here, so the tag
    // re-asserts when the connection ping changes, not only on the 500ms tick.
    float ping = 0.f;
    if (Interfaces::Engine) {
        if (auto info = reinterpret_cast<INetChannelInfo*>(Interfaces::Engine->GetNetChannelInfo()))
            ping = info->GetLatency(0); // FLOW_INCOMING
    }
    const auto clantag_time = static_cast<int>(ctx.curtime * 2.f + ping);

    if (ClantagEnabled()) {
        reset_tag = false;
        if (clantag_time != last_change_time) {
            __try { fn(kDefaultTag, kDefaultTag); }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                static bool logged = false;
                if (!logged) { logged = true; LOG_WARN("clantag: SetClanTag raised an exception"); }
            }
            last_change_time = clantag_time;
        }
    } else if (!reset_tag) {
        if (clantag_time != last_change_time) {
            __try { fn("", ""); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
            reset_tag = true;
            last_change_time = clantag_time;
        }
    }
}
} // namespace Misc::General::Detail
