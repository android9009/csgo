#include "general.hpp"
#include "../../../context.hpp"
#include "../../../sdk/netvars.hpp"
#include "../../../utils/log.hpp"
#include "../../../utils/memory.hpp"
#include "../../../utils/offsets/offsets.hpp"
#include <atomic>
#include <cstring>
#include <windows.h>

namespace Misc::ClanTag {
namespace {
// SetClanTag lives in engine.dll; the published offset is already an RVA.
using SetClanTagFn = int(__fastcall*)(const char* tag, const char* name);

constexpr std::size_t kClanFieldSize = 16; // char m_szClan[16]

std::atomic<bool> g_enabled{false};
SetClanTagFn g_function{};
bool g_resolved{};
bool g_applied{};
char g_applied_tag[32]{};
char g_original_tag[32]{};
std::uintptr_t g_applied_local{};
std::uint32_t g_last_apply{};

void Resolve() {
    if (g_resolved) return;
    g_resolved = true;
    const auto base = reinterpret_cast<std::uintptr_t>(MEM::GetModuleBase("engine.dll"));
    const auto rva = offsets::Get("dwSetClanTag");
    if (!base || !rva) {
        LOG_WARN("misc: dwSetClanTag unresolved; clan tag stays disabled");
        return;
    }
    g_function = reinterpret_cast<SetClanTagFn>(base + rva);
}

// Reads the tag the player already owns so disabling the feature restores it instead
// of blanking it. m_szClan is a fixed 16-byte field, copied with an explicit
// terminator rather than strncpy, which would over-read a non-terminated field.
bool ReadCurrentTag(char* out, std::size_t size) {
    static const std::ptrdiff_t offset = PropManager::Get().GetOffset("DT_CSPlayer", "m_szClan");
    if (!ctx.local || offset <= 0 || size < kClanFieldSize + 1) return false;
    char field[kClanFieldSize]{};
    std::memcpy(field, reinterpret_cast<const void*>(reinterpret_cast<std::uintptr_t>(ctx.local) + offset),
                sizeof(field));
    std::memcpy(out, field, sizeof(field));
    out[kClanFieldSize] = 0;
    return out[0] != 0;
}
} // namespace

void SetEnabled(bool enabled) { g_enabled.store(enabled, std::memory_order_relaxed); }

void FrameStage(int stage) {
    if (stage != 0 && stage != 4 && stage != 5) return;
    const auto local = reinterpret_cast<std::uintptr_t>(ctx.local);
    const bool want = g_enabled.load(std::memory_order_relaxed) && ctx.in_game && local != 0;
    const auto now = static_cast<std::uint32_t>(GetTickCount());
    if (ShouldApply(want, g_applied, g_applied_tag, kTag, g_applied_local, local, now,
                    g_last_apply, kReapplyIntervalMs)) {
        Resolve();
        if (!g_function) return;
        if (!g_applied) ReadCurrentTag(g_original_tag, sizeof(g_original_tag));
        g_function(kTag, kTag);
        std::strncpy(g_applied_tag, kTag, sizeof(g_applied_tag) - 1);
        g_applied_tag[sizeof(g_applied_tag) - 1] = 0;
        g_applied_local = local;
        g_last_apply = now;
        g_applied = true;
        return;
    }
    if (!ShouldRestore(want, g_applied)) return;
    g_function(g_original_tag, g_original_tag);
    g_applied = false;
    g_applied_tag[0] = 0;
    g_applied_local = 0;
    g_last_apply = 0;
}

void Shutdown() {
    if (!g_applied) return;
    if (g_function) g_function(g_original_tag, g_original_tag);
    g_applied = false;
}
} // namespace Misc::ClanTag
