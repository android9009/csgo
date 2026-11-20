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
#include <unordered_map>
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
// ECstrike15UserMessages_CS_UM_ServerRankRevealAll, from the CS:GO protobuf definition
// (cstrike15_usermessages.proto) and CSGOSimple's Enums.hpp: the client's own handler for
// this message is what asks the server for the rank table.
constexpr int kServerRankRevealAll = 50;
// Candidate signatures for that handler. The first is the one CSGOSimple publishes; the
// other two are the variants UC threads report for earlier and later client builds. A
// build only ever matches one of them, and a miss is not fatal.
constexpr const char* kRevealNames[] = {"server_rank_reveal_all",
                                        "server_rank_reveal_all_alt1",
                                        "server_rank_reveal_all_alt2"};

RevealFn g_reveal{};
bool g_reveal_resolved{};
bool g_failed{};
bool g_rank_sent{};
bool g_rank_verified{};
bool g_rank_unreadable{};
std::uint32_t g_rank_request_ms{};
std::uint32_t g_last_send{};

// ---- equipment (Panorama) ------------------------------------------------------
panorama::UIEngine* g_ui{};
bool g_blocked{};
bool g_probe_sent{};
bool g_installed{};      // the engine script is in this HUD
bool g_reported_no_panel{};
bool g_reported_enable{};
bool g_reported_updates{};
int g_no_panel_feeds{};
bool g_um_reported{};
int g_script_failures{};
std::uint32_t g_last_feed{};
std::uint32_t g_last_install{};

// What was last sent per row key, so an unchanged loadout costs nothing: the reference
// client caches exactly this and skips the message when the state is identical.
struct Sent {
    std::string signature;
    std::uint64_t xuid{};
    std::string nickname;
};
std::unordered_map<std::string, Sent> g_sent;

// A row is identified by the steam id the scoreboard itself uses; bots have none, so their
// nickname is the key (and the script matches rows by nickname too).
std::string CacheKey(const Equipment& player) {
    if (player.xuid) return std::to_string(player.xuid);
    if (player.name[0]) return std::string("n:") + player.name;
    return {};
}

// Everything the row shows, flattened: identical signature means nothing to send.
std::string Signature(const Equipment& player) {
    std::string signature;
    for (int i = 0; i < player.weapon_count; ++i) {
        signature += player.weapons[i];
        signature += ':';
        signature += std::to_string(player.weapon_types[i]);
        signature += ';';
    }
    signature += '|';
    signature += std::to_string(player.selected);
    signature += '|';
    signature += std::to_string(player.armor);
    signature += player.defuser ? '1' : '0';
    signature += '|';
    signature += std::to_string(player.money);
    return signature;
}

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
    if (!base) {
        LOG_WARN("scoreboard: client.dll missing; rank reveal stays off");
        return nullptr;
    }
    for (const char* name : kRevealNames) {
        const auto rva = offsets::Get(name);
        if (!rva) continue;
        if (!InsideModule("client.dll", rva)) {
            LOG_WARN("scoreboard: %s outside client.dll .text; refusing to call it", name);
            continue;
        }
        g_reveal = reinterpret_cast<RevealFn>(base + rva);
        LOG_INFO("scoreboard: rank reveal handler from %s at client.dll+0x%X", name,
                 static_cast<unsigned>(rva));
        return g_reveal;
    }
    LOG_WARN("scoreboard: no rank reveal signature matched this client; falling back to the "
             "CS_UM_ServerRankRevealAll user message (%d)", kServerRankRevealAll);
    return nullptr;
}

// The handler is a game function: guard the call so a mismatched build stops this
// feature instead of taking the whole Misc runtime down with it.
int CallReveal(RevealFn fn) {
    int out[4]{};
    __try { return static_cast<int>(fn(out)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// Fallback when no signature matched: dispatch the client's own ServerRankRevealAll user
// message. The read object is zeroed, so a handler that tries to read a field sees an
// empty message instead of a dangling pointer.
int CallRevealViaUserMessage() {
    if (!Interfaces::Client) {
        LOG_WARN("scoreboard: IBaseClientDll missing; rank reveal stays off");
        return -1;
    }
    using Fn = bool(__fastcall*)(void*, void*, int, int, void*);
    auto fn = reinterpret_cast<Fn>((*reinterpret_cast<void***>(Interfaces::Client))[38]);
    if (!fn) return -1;
    alignas(std::uintptr_t) unsigned char read_object[sizeof(std::uintptr_t) * 12]{};
    __try {
        return fn(Interfaces::Client, nullptr, kServerRankRevealAll, 0, read_object) ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
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
        g_installed = false;
        g_sent.clear();
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
// player_info_t is ~344 bytes in CS:GO (two 8-byte fields, name[128], userid, guid[33],
// friendsid, friendsname[128], two flags, four custom files, one counter). The engine writes
// the whole struct, so the buffer is deliberately larger than the published layout: a short
// buffer here is a stack overflow inside a detour, which is the worst place to have one.
struct PlayerInfoBuffer {
    unsigned char raw[512];
};
constexpr std::size_t kPlayerInfoNameOffset = 16; // name[128] right after the two 8-byte fields

// Copies at most sizeof(destination) - 1 printable characters; the source is the engine's
// struct and is never trusted to be terminated inside the region this feature reads.
void CopyNickname(char (&destination)[64], const unsigned char* info) {
    if (!info) return;
    const char* source = reinterpret_cast<const char*>(info + kPlayerInfoNameOffset);
    int written = 0;
    for (int i = 0; i < 127 && written < 63; ++i) {
        const unsigned char c = static_cast<unsigned char>(source[i]);
        if (c == 0) break;
        if (c < 0x20 || c == 0x7F) continue;
        destination[written++] = static_cast<char>(c);
    }
    destination[written] = '\0';
}

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
            // The nickname is only a fallback row key, so a layout surprise costs nothing:
            // the copy is bounded and stops at the first NUL.
            PlayerInfoBuffer info{};
            if (Interfaces::Engine->GetPlayerInfo(index, &info)) {
                CopyNickname(equipment.name, info.raw);
                // xuid lives at +0x8 in player_info_t: it is the row key the scoreboard
                // itself uses, so it is the key the reference implementation sends.
                equipment.xuid = *reinterpret_cast<const std::uint64_t*>(info.raw + 8);
            }
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
            for (int i = 0; i < 64 && equipment.weapon_count < kEquipmentSlots; ++i) {
                if (!handles[i]) continue;
                auto* weapon = static_cast<CBaseWeapon*>(
                    Interfaces::EntityList->GetClientEntityFromHandle(handles[i]));
                if (!weapon) continue;
                const char* icon = WeaponIcon(weapon->m_iItemDefinitionIndex());
                if (!icon) continue;
                if (weapon == active) equipment.selected = equipment.weapon_count;
                equipment.weapons[equipment.weapon_count] = icon;
                equipment.weapon_types[equipment.weapon_count] = WeaponTypeOf(icon);
                ++equipment.weapon_count;
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
        if (g_installed) RunScriptSafe(BuildResetScript(), false);
        g_installed = false;
        g_sent.clear();
        g_reported_updates = false;
        g_last_install = 0;
        g_ui = nullptr;
        g_no_panel_feeds = 0;
        g_reported_no_panel = false;
        g_reported_enable = false;
        g_script_failures = 0;
        g_blocked = false;
        return;
    }
    if (g_blocked || !ctx.in_game) return;
    if (!g_installed && !g_reported_enable) {
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
    if (!CollectEquipment(players)) return;

    Scales scales;
    scales.x = g_scale_x.load(std::memory_order_relaxed);
    scales.y = g_scale_y.load(std::memory_order_relaxed);
    scales.weapon = g_scale_weapon.load(std::memory_order_relaxed);

    // The installer goes in once (and again after a HUD reload, which drops the objects it
    // defines). Everything after that is a small message per changed player, the way the
    // reference client works: re-injecting the whole feature every frame was wasteful and
    // made every feed rebuild every row.
    const std::uint32_t install_due = static_cast<std::uint32_t>(now - g_last_install);
    if (!g_installed || install_due >= kReinstallMs) {
        if (!RunScriptSafe(BuildEngineScript(scales), !g_installed)) return; // retried next tick
        if (!g_installed)
            LOG_INFO("scoreboard: equipment engine installed (%zu player(s) in view, %zu bytes)",
                     players.size(), BuildEngineScript(scales).size());
        g_installed = true;
        g_last_install = now;
        g_sent.clear(); // a reinstall forgets the DOM it knew about; resend everything once
    }

    std::string batch;
    std::size_t changed = 0;
    std::vector<std::string> alive;
    alive.reserve(players.size());
    for (const auto& player : players) {
        const std::string key = CacheKey(player);
        if (key.empty()) continue;
        alive.push_back(key);
        const std::string signature = Signature(player);
        const auto found = g_sent.find(key);
        if (found != g_sent.end() && found->second.signature == signature) continue;
        Sent sent;
        sent.signature = signature;
        sent.xuid = player.xuid;
        sent.nickname = player.name;
        g_sent[key] = sent;
        auto message = BuildUpdateScript(player, scales);
        // A player who lost everything gets an explicit clear, exactly like the reference
        // client does; leaving the last loadout on screen would be a lie.
        if (message.empty()) message = BuildClearScript(player.xuid, player.name);
        if (message.empty()) continue;
        batch += message;
        batch += "\n";
        ++changed;
    }

    // Players who left the server or lost their equipment: the row would otherwise keep
    // showing the last loadout it was told about.
    for (auto it = g_sent.begin(); it != g_sent.end();) {
        bool present = false;
        for (const auto& key : alive) if (key == it->first) { present = true; break; }
        if (present) { ++it; continue; }
        const auto clear = BuildClearScript(it->second.xuid, it->second.nickname.c_str());
        if (!clear.empty()) { batch += clear; batch += "\n"; }
        it = g_sent.erase(it);
    }

    if (batch.empty()) {
        g_no_panel_feeds = 0;
        return;
    }
    if (RunScriptSafe(batch, false)) {
        g_no_panel_feeds = 0;
        if (!g_reported_updates) {
            g_reported_updates = true;
            LOG_INFO("scoreboard: equipment updates flowing (%zu player(s) in the first batch)",
                     changed);
        }
        return;
    }

    // No panel yet: the game has not dispatched a UI event since the HUD loaded, so the
    // script has no context to run in. Harmless, but say it once.
    if (!g_reported_no_panel && ++g_no_panel_feeds > 20) {
        g_reported_no_panel = true;
        LOG_WARN("scoreboard: no Panorama panel to run the equipment script in; "
                 "open the scoreboard once or re-enable the feature");
    }
}

} // namespace

void Publish(std::uint64_t mask, int scale_x, int scale_y, int scale_weapon) {
    // Switching the bit off and on again re-arms the check: the verdict belongs to the
    // session that asked for it, not to the process.
    if (!Enabled(mask, Bits::RevealRanks)) {
        g_rank_sent = false;
        g_rank_verified = false;
        g_rank_unreadable = false;
        g_rank_request_ms = 0;
        g_last_send = 0;
    }
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
        if (ShouldRevealRanks(true, ctx.in_game, now, g_last_send, kRevealIntervalMs)) {
            g_last_send = now;
            const int result = reveal ? CallReveal(reveal) : CallRevealViaUserMessage();
            if (!reveal && !g_um_reported) {
                g_um_reported = true;
                LOG_INFO("scoreboard: rank reveal sent through the user message path");
            }
            if (result < 0) {
                g_failed = true;
                LOG_ERROR("scoreboard: server_rank_reveal_all threw; rank reveal stopped");
            } else if (!g_rank_sent) {
                g_rank_sent = true;
                g_rank_request_ms = now;
                LOG_INFO("scoreboard: rank reveal requested (handler result %d); the answer "
                         "arrives on a later frame", result);
            }
        }
    }

    // The verdict is taken out here, not next to the request: the skill groups come back
    // from the server a few frames later, so reading them in the same tick reports zero on
    // every server, including the ones that do publish ranks.
    if (Enabled(mask, Bits::RevealRanks) && ctx.in_game && !g_rank_verified &&
        RankVerdictDue(now, g_rank_request_ms, kRankVerifyMs)) {
        const int ranked = ReadRankedSlots();
        const bool deadline = RankVerdictDue(now, g_rank_request_ms, kRankVerdictDeadlineMs);
        if (ranked > 0) {
            g_rank_verified = true;
            LOG_INFO("scoreboard: rank reveal confirmed - %d slot(s) carry a skill group", ranked);
        } else if (ranked == 0 || deadline) {
            g_rank_verified = true;
            if (ranked < 0) {
                // The resource was never readable: say that instead of blaming the server.
                g_rank_unreadable = true;
                LOG_WARN("scoreboard: CCSPlayerResource never became readable (dwPlayerResource); "
                         "ranks cannot be checked, they may still appear on the scoreboard");
            } else {
                LOG_WARN("scoreboard: this server publishes no skill groups (checked %u ms after "
                         "the reveal) - ranks stay blank here, on official matchmaking they appear",
                         static_cast<unsigned>(now - g_rank_request_ms));
            }
        }
        // ranked < 0 and still inside the deadline: try again on the next frame.
    }

    WeaponFrame(now);
}
} // namespace Misc::Scoreboard
