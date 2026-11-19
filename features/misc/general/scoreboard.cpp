#include "general.hpp"
#include "../../../context.hpp"
#include "../../../sdk/entity/player.hpp"
#include "../../../sdk/entity/player_resource.hpp"
#include "../../../sdk/entity/weapon.hpp"
#include "../../../sdk/interfaces/interfaces.hpp"
#include "../../../sdk/panorama/panorama.hpp"
#include "../../../utils/log.hpp"
#include "../../../utils/memory.hpp"
#include "../../../utils/offsets/offsets.hpp"
#include <atomic>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>

namespace Misc::Scoreboard {
namespace {
std::atomic<std::uint64_t> g_mask{0};
std::atomic<int> g_scale_x{kScaleDefault}, g_scale_y{kScaleDefault}, g_scale_weapon{kScaleDefault};

// ---- rank reveal ---------------------------------------------------------------
// char __cdecl(int*): the client's ServerRankRevealAll handler - it builds and
// dispatches the rank reveal user message and writes its result into the out array.
using RevealFn = char(__cdecl*)(int*);

RevealFn g_reveal{};
bool g_reveal_resolved{};
bool g_failed{};
bool g_rank_reported{};
std::uint32_t g_last_send{};

// ---- equipment (Panorama) ------------------------------------------------------
panorama::UIEngine* g_ui{};
bool g_blocked{};
bool g_probe_sent{};
bool g_active_script{};
bool g_reported_no_panel{};
bool g_reported_enable{};
int g_no_panel_feeds{};
int g_script_failures{};
std::uint32_t g_last_feed{};

bool InsideModule(const char* module, std::uintptr_t rva) {
    const auto base = reinterpret_cast<std::uintptr_t>(MEM::GetModuleBase(module));
    std::uintptr_t text = 0;
    std::uintptr_t size = 0;
    if (!base || !MEM::GetSection(base, ".text", &text, &size)) return false;
    return rva >= text - base && rva < text - base + size;
}

// A resolved signature is an RVA into client.dll. Anything that does not land in the
// client's .text section is refused, so a stale pattern cannot become a wild call.
RevealFn ResolveReveal() {
    if (g_reveal_resolved) return g_reveal;
    g_reveal_resolved = true;

    const auto base = reinterpret_cast<std::uintptr_t>(MEM::GetModuleBase("client.dll"));
    const auto rva = offsets::Get("server_rank_reveal_all");
    if (!base || !rva) {
        LOG_WARN("scoreboard: server_rank_reveal_all not resolved; rank reveal stays off");
        return nullptr;
    }
    if (!InsideModule("client.dll", rva)) {
        LOG_WARN("scoreboard: server_rank_reveal_all outside client.dll .text; rank reveal stays off");
        return nullptr;
    }

    g_reveal = reinterpret_cast<RevealFn>(base + rva);
    LOG_INFO("scoreboard: rank reveal handler at client.dll+0x%X", static_cast<unsigned>(rva));
    return g_reveal;
}

// The handler is a game function: guard the call so a mismatched build stops this
// feature instead of taking the whole Misc runtime down with it.
int CallReveal(RevealFn fn) {
    int out[4]{};
    __try { return static_cast<int>(fn(out)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// Same guard for the diagnostic read: the resource pointer belongs to the game.
int ReadRankedSlots() {
    __try {
        auto* resource = PlayerResource();
        if (!resource || !Interfaces::Engine) return -1;
        return resource->RankedSlots(Interfaces::Engine->GetMaxClients());
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// ---- Panorama helpers ----------------------------------------------------------
//
// Only three native calls are made - AccessUIEngine, GetLastTargetPanel and RunScript -
// and each one logs the step *before* performing it, so if the client build disagrees
// with an index the last line of the log names the call that killed the process.
// Panel lookups by id happen in JavaScript instead (docs/panorama.md).
bool EnsureEngine() {
    if (!Interfaces::Panorama) {
        LOG_WARN("scoreboard: panorama.dll interface PanoramaUIEngine001 missing; weapon reveal stays off");
        return false;
    }
    panorama::UIEngine* engine = nullptr;
    LOG_INFO("scoreboard: panorama step 1/3 - AccessUIEngine");
    __try { engine = Interfaces::Panorama->AccessUIEngine(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { engine = nullptr; }
    if (!engine) {
        LOG_WARN("scoreboard: AccessUIEngine returned nothing; weapon reveal stays off");
        return false;
    }
    if (!panorama::ResolveRunScript(engine)) return false;
    g_ui = engine;
    LOG_INFO("scoreboard: panorama step 1/3 ok (engine %p)", static_cast<void*>(engine));
    return true;
}

// Re-fetched on every feed: the pointer the game hands out is a panel from the last
// dispatched UI event, and caching it across a map change would mean using a dangling
// pointer. A panel is only ever passed back to the engine, never dereferenced here.
void* LastTargetPanel() {
    void* panel = nullptr;
    __try { panel = g_ui->GetLastTargetPanel(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { panel = nullptr; }
    return panel;
}

bool RunScriptSafe(const std::string& script, bool verbose) {
    if (!g_ui || script.empty()) return false;

    if (verbose) LOG_INFO("scoreboard: panorama step 2/3 - GetLastTargetPanel");
    void* panel = LastTargetPanel();
    if (!panel) {
        // Normal before the game has dispatched any UI event; retried, not an error.
        if (verbose) LOG_INFO("scoreboard: panorama step 2/3 - no panel yet (the HUD has not "
                              "dispatched a UI event); retrying");
        return false;
    }

    if (verbose) LOG_INFO("scoreboard: panorama step 3/3 - RunScript (%zu bytes)", script.size());
    bool ok = true;
    __try { panorama::RunScript(g_ui, panel, script.c_str(), "panorama/layout/base.xml"); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    if (ok) return true;

    ++g_script_failures;
    LOG_ERROR("scoreboard: panorama script call threw (failure %d/3)", g_script_failures);
    if (g_script_failures >= 3) {
        g_blocked = true;
        g_ui = nullptr;
        g_active_script = false;
        LOG_ERROR("scoreboard: weapon reveal disabled for this session");
    }
    return false;
}

// Money is not in the pinned netvar set, so the table it lives on is probed once and
// cached. A client build without the prop simply shows no money instead of guessing an
// offset.
std::ptrdiff_t MoneyOffset() {
    static const std::ptrdiff_t offset = [] {
        const std::pair<const char*, const char*> candidates[] = {
            {"DT_CSPlayer", "m_iAccount"},
            {"DT_CSPlayerMoney", "m_iAccount"},
            {"DT_BasePlayer", "m_iAccount"},
        };
        for (const auto& candidate : candidates) {
            const auto found = PropManager::Get().GetOffset(candidate.first, candidate.second);
            if (found) return found;
        }
        LOG_WARN("scoreboard: m_iAccount netvar not found; reveal money stays hidden");
        return std::ptrdiff_t{0};
    }();
    return offset;
}

// Reads the equipment of every live player once per feed. Everything here is game
// memory, so the whole pass is guarded and a failure just skips the frame.
bool CollectEquipment(std::vector<Equipment>& out) {
    __try {
        if (!Interfaces::Engine || !Interfaces::EntityList) return false;
        const auto weapons_offset = PropManager::Get().GetOffset("DT_BaseCombatCharacter", "m_hMyWeapons");
        if (weapons_offset <= 0) return false;
        const bool want_money = Enabled(g_mask.load(std::memory_order_relaxed), Bits::RevealMoney);
        const auto money_offset = want_money ? MoneyOffset() : std::ptrdiff_t{0};
        const int max_clients = Interfaces::Engine->GetMaxClients();
        for (int index = 1; index <= max_clients && index <= 64; ++index) {
            auto* player = static_cast<CCSPlayer*>(Interfaces::EntityList->GetClientEntity(index));
            if (!player || player->IsDormant()) continue;

            Equipment equipment;
            equipment.slot = index;
            equipment.armor = player->m_ArmorValue() > 0 ? (player->m_bHasHelmet() ? 2 : 1) : 0;
            equipment.defuser = player->m_bHasDefuser();
            if (money_offset) {
                const int money = *reinterpret_cast<const int*>(
                    reinterpret_cast<std::uintptr_t>(player) + money_offset);
                // Anything outside a plausible match range means the prop moved.
                if (money >= 0 && money <= 100000) equipment.money = money;
            }

            auto* active = player->GetActiveWeapon();
            const auto* handles = reinterpret_cast<const std::uint32_t*>(
                reinterpret_cast<std::uintptr_t>(player) + weapons_offset);
            for (int i = 0; i < 64 && equipment.weapon_count < 8; ++i) {
                if (!handles[i]) continue;
                auto* weapon = static_cast<CBaseWeapon*>(
                    Interfaces::EntityList->GetClientEntityFromHandle(handles[i]));
                if (!weapon) continue;
                const char* icon = WeaponIcon(weapon->m_iItemDefinitionIndex());
                if (!icon) continue;
                if (weapon == active) equipment.selected = equipment.weapon_count;
                equipment.weapons[equipment.weapon_count++] = icon;
            }
            if (equipment.weapon_count > 0) out.push_back(equipment);
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void WeaponFrame(std::uint32_t now) {
    const auto mask = g_mask.load(std::memory_order_relaxed);
    const bool enabled = Enabled(mask, Bits::RevealWeapon) || Enabled(mask, Bits::RevealMoney);

    if (!enabled) {
        if (g_active_script) RunScriptSafe(BuildResetScript(), false);
        g_active_script = false;
        g_ui = nullptr;
        g_no_panel_feeds = 0;
        g_reported_no_panel = false;
        g_reported_enable = false;
        g_script_failures = 0;
        g_blocked = false;
        return;
    }
    if (g_blocked || !ctx.in_game) return;
    if (!g_active_script && !g_reported_enable) {
        g_reported_enable = true;
        LOG_INFO("scoreboard: reveal on - panorama=%p in_game=%d local=%p", static_cast<void*>(Interfaces::Panorama),
                 ctx.in_game ? 1 : 0, static_cast<void*>(ctx.local));
    }

    if (!g_ui && !EnsureEngine()) {
        g_blocked = true;
        return;
    }

    if (static_cast<std::uint32_t>(now - g_last_feed) < kFeedIntervalMs) return;
    g_last_feed = now;

    std::vector<Equipment> players;
    players.reserve(16);
    if (!CollectEquipment(players)) return;

    Scales scales;
    scales.x = g_scale_x.load(std::memory_order_relaxed);
    scales.y = g_scale_y.load(std::memory_order_relaxed);
    scales.weapon = g_scale_weapon.load(std::memory_order_relaxed);

    if (RunScriptSafe(BuildFeedScript(players, scales), !g_active_script)) {
        if (!g_active_script)
            LOG_INFO("scoreboard: equipment script accepted by the HUD (%zu players)",
                     players.size());
        g_active_script = true;
        g_no_panel_feeds = 0;
        return;
    }

    // No panel yet: the game has not dispatched a UI event since the HUD loaded, so the
    // script has no context to run in. Harmless, but say it once.
    if (!g_active_script && !g_reported_no_panel && ++g_no_panel_feeds > 20) {
        g_reported_no_panel = true;
        LOG_WARN("scoreboard: no Panorama panel to run the equipment script in; "
                 "open the scoreboard once or re-enable the feature");
    }
}

} // namespace

void Publish(std::uint64_t mask, int scale_x, int scale_y, int scale_weapon) {
    g_mask.store(mask, std::memory_order_relaxed);
    g_scale_x.store(ClampScale(scale_x), std::memory_order_relaxed);
    g_scale_y.store(ClampScale(scale_y), std::memory_order_relaxed);
    g_scale_weapon.store(ClampScale(scale_weapon), std::memory_order_relaxed);
}

void FrameStage(int stage) {
    if (stage != 4 || g_failed) return;

    const auto mask = g_mask.load(std::memory_order_relaxed);
    const auto now = GetTickCount();

    if (Enabled(mask, Bits::RevealRanks)) {
        // ResolveReveal logs why when it fails, so a null handler needs no second line.
        const auto reveal = ResolveReveal();
        if (reveal && ShouldRevealRanks(true, ctx.in_game, now, g_last_send, kRevealIntervalMs)) {
            g_last_send = now;
            const int result = CallReveal(reveal);
            if (result < 0) {
                g_failed = true;
                LOG_ERROR("scoreboard: server_rank_reveal_all threw; rank reveal stopped");
            } else {
                const int ranked = ReadRankedSlots();
                if (!g_rank_reported) {
                    g_rank_reported = true;
                    LOG_INFO("scoreboard: rank reveal sent (handler result %d, slots with a skill "
                             "group: %d)", result, ranked);
                    if (ranked == 0) {
                        LOG_WARN("scoreboard: the server sent no skill groups - rank reveal only "
                                 "shows what the server is willing to publish");
                    } else if (ranked < 0) {
                        LOG_WARN("scoreboard: could not read CCSPlayerResource (dwPlayerResource); "
                                 "ranks may still appear on the scoreboard");
                    }
                }
            }
        }
    }

    WeaponFrame(now);
}
} // namespace Misc::Scoreboard
