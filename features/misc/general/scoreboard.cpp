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
panorama::IUIPanel* g_hud{};
bool g_blocked{};
bool g_reported_panel{};
bool g_active_script{};
int g_panel_attempts{};
int g_script_failures{};
std::uint32_t g_last_search{};
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
bool EnsureEngine() {
    if (!Interfaces::Panorama) {
        LOG_WARN("scoreboard: panorama.dll interface PanoramaUIEngine001 missing; weapon reveal stays off");
        return false;
    }
    panorama::UIEngine* engine = nullptr;
    __try { engine = Interfaces::Panorama->AccessUIEngine(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { engine = nullptr; }
    if (!engine) {
        LOG_WARN("scoreboard: AccessUIEngine returned nothing; weapon reveal stays off");
        return false;
    }
    if (!panorama::ResolveRunScript(engine)) return false;
    g_ui = engine;
    return true;
}

// The HUD panel is reached by walking up from the last panel the game dispatched an
// event to. Both the lookup and the walk call unverifiable vtable slots, so the whole
// thing sits behind SEH and is bounded.
panorama::IUIPanel* FindHudPanel(panorama::UIEngine* engine) {
    panorama::IUIPanel* found = nullptr;
    __try {
        auto* panel = engine->GetLastTargetPanel();
        for (int depth = 0; panel && depth < 64; ++depth) {
            if (!engine->IsValidPanelPointer(panel)) break;
            const char* id = panel->GetID();
            if (!id) break;
            if (std::strcmp(id, "CSGOHud") == 0) {
                found = panel;
                break;
            }
            if (std::strcmp(id, "CSGOMainMenu") == 0) break;
            panel = panel->GetParent();
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    return found;
}

bool PanelStillValid(panorama::UIEngine* engine, panorama::IUIPanel* panel) {
    bool valid = false;
    __try { valid = engine->IsValidPanelPointer(panel); }
    __except (EXCEPTION_EXECUTE_HANDLER) { valid = false; }
    return valid;
}

bool RunScriptSafe(const std::string& script) {
    if (!g_ui || !g_hud || script.empty()) return false;
    bool ok = true;
    __try { panorama::RunScript(g_ui, g_hud, script.c_str(), "panorama/layout/base.xml"); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    if (ok) return true;

    ++g_script_failures;
    LOG_ERROR("scoreboard: panorama script call threw (failure %d/%d)", g_script_failures, 3);
    if (g_script_failures >= 3) {
        g_blocked = true;
        g_hud = nullptr;
        g_active_script = false;
        LOG_ERROR("scoreboard: weapon reveal disabled for this session");
    }
    return false;
}

// Reads the equipment of every live player once per feed. Everything here is game
// memory, so the whole pass is guarded and a failure just skips the frame.
bool CollectEquipment(std::vector<Equipment>& out) {
    __try {
        if (!Interfaces::Engine || !Interfaces::EntityList) return false;
        const auto weapons_offset = PropManager::Get().GetOffset("DT_BaseCombatCharacter", "m_hMyWeapons");
        if (weapons_offset <= 0) return false;
        const int max_clients = Interfaces::Engine->GetMaxClients();
        for (int index = 1; index <= max_clients && index <= 64; ++index) {
            auto* player = static_cast<CCSPlayer*>(Interfaces::EntityList->GetClientEntity(index));
            if (!player || player->IsDormant()) continue;

            Equipment equipment;
            equipment.slot = index;
            equipment.armor = player->m_ArmorValue() > 0 ? (player->m_bHasHelmet() ? 2 : 1) : 0;
            equipment.defuser = player->m_bHasDefuser();

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
    const bool enabled = Enabled(mask, Bits::RevealWeapon);

    if (!enabled) {
        if (g_active_script) RunScriptSafe(BuildResetScript());
        g_active_script = false;
        g_hud = nullptr;
        g_ui = nullptr;
        g_panel_attempts = 0;
        g_script_failures = 0;
        g_blocked = false;
        g_reported_panel = false;
        return;
    }
    if (g_blocked) return;

    if (!ShouldSearchPanel(true, ctx.in_game, g_hud != nullptr, g_panel_attempts)) {
        if (ctx.in_game && !g_hud && g_panel_attempts >= kPanelSearchAttempts && !g_reported_panel) {
            g_reported_panel = true;
            LOG_WARN("scoreboard: CSGOHud panel not found after %d attempts; weapon reveal idle "
                     "until re-enabled (open the scoreboard once)", kPanelSearchAttempts);
        }
        return;
    }

    if (!g_ui && !EnsureEngine()) {
        g_blocked = true;
        return;
    }

    // A map change or a HUD reload invalidates the panel pointer.
    if (g_hud && !PanelStillValid(g_ui, g_hud)) {
        g_hud = nullptr;
        g_active_script = false;
    }

    if (!g_hud) {
        if (static_cast<std::uint32_t>(now - g_last_search) < kPanelSearchIntervalMs) return;
        g_last_search = now;
        ++g_panel_attempts;
        g_hud = FindHudPanel(g_ui);
        if (!g_hud) return;
        LOG_INFO("scoreboard: CSGOHud panel found after %d attempt(s)", g_panel_attempts);
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

    if (RunScriptSafe(BuildFeedScript(players, scales))) g_active_script = true;
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
        const auto reveal = ResolveReveal();
        if (reveal) {
            if (ShouldRevealRanks(true, ctx.in_game, now, g_last_send, kRevealIntervalMs)) {
                g_last_send = now;
                if (CallReveal(reveal) < 0) {
                    g_failed = true;
                    LOG_ERROR("scoreboard: server_rank_reveal_all threw; rank reveal stopped");
                } else if (!g_rank_reported) {
                    // One line per session: how many slots actually carry a skill group
                    // afterwards. The server decides what it fills in, so this is the only
                    // honest confirmation.
                    g_rank_reported = true;
                    LOG_INFO("scoreboard: rank reveal requested (slots with a skill group: %d)",
                             ReadRankedSlots());
                }
            }
        }
    }

    WeaponFrame(now);
}
} // namespace Misc::Scoreboard
