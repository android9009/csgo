#include "general.hpp"
#include "../../../context.hpp"
#include "../../../sdk/entity/player.hpp"
#include "../../../sdk/interfaces/interfaces.hpp"
#include "../../../utils/log.hpp"
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace Misc::General::Detail {
namespace {
// `main` event_logs, but scoped to the events this build can resolve. The hit/miss split in
// `main` comes from the shot log (anti-aim); here the hit log is driven by `player_hurt`
// where the local player is the attacker, which carries the same damage/hitgroup/weapon the
// menu's detail toggles refer to.
int LocalIndex() {
    if (!Interfaces::Engine) return -1;
    return Interfaces::Engine->GetLocalPlayer();
}

CCSPlayer* ResolvePlayer(int user_id) {
    if (!Interfaces::Engine || !Interfaces::EntityList || !user_id) return nullptr;
    const int index = Interfaces::Engine->GetPlayerForUserID(user_id);
    if (index <= 0) return nullptr;
    return static_cast<CCSPlayer*>(Interfaces::EntityList->GetClientEntity(index));
}

bool IsLocalUser(int user_id) {
    if (!Interfaces::Engine || !user_id) return false;
    return Interfaces::Engine->GetPlayerForUserID(user_id) == LocalIndex();
}

bool IsExplosive(const char* weapon) {
    if (!weapon) return false;
    return !std::strcmp(weapon, "hegrenade") || !std::strcmp(weapon, "molotov") ||
           !std::strcmp(weapon, "incgrenade");
}

// `main` main_utils::hitgroup_to_string: CS:GO hitgroup names for the log lines.
const char* HitGroupName(int group) {
    switch (group) {
    case 1: return "head";
    case 2: return "chest";
    case 3: return "stomach";
    case 5: return "left arm";
    case 6: return "right arm";
    case 7: return "left leg";
    case 8: return "right leg";
    default: return "body";
    }
}

void Name(int index, char* out, size_t size) {
    out[0] = 0;
    if (!Interfaces::Engine || index <= 0) return;
    alignas(8) unsigned char info[512]{};
    if (Interfaces::Engine->GetPlayerInfo(index, info)) {
        std::memcpy(out, info + 16, size - 1);
        out[size - 1] = 0;
        for (char* p = out; *p; ++p)
            if (static_cast<unsigned char>(*p) < 32 && *p) *p = ' ';
        return;
    }
    std::snprintf(out, size, "Player %d", index);
}

int IndexOf(int user_id) {
    if (!Interfaces::Engine || !user_id) return -1;
    return Interfaces::Engine->GetPlayerForUserID(user_id);
}

// `main` c_cs_player::is_teammate: on the same combatant side as the local player. Sideless
// entities (spectators, round transitions) are never teammates.
bool IsTeammateIndex(int index) {
    if (index <= 0 || !Interfaces::EntityList || !ctx.local) return false;
    const auto entity = Interfaces::EntityList->GetClientEntity(index);
    if (!entity) return false;
    const int team = static_cast<CCSPlayer*>(entity)->m_iTeamNum();
    if (team != 2 && team != 3) return false;
    return team == ctx.local->m_iTeamNum();
}

void LogLine(const char* kind, const char* format, ...) {
    // `console_logs` gates the per-event line so the log file stays clean by default; the
    // type toggles already decide which kinds are produced at all.
    if (!ConsoleLogs()) return;

    char body[512]{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(body, sizeof(body), format, args);
    va_end(args);
    LOG_INFO("[log] %s: %s", kind, body);
}
} // namespace

void LogRoundStart() { /* per-event logging keeps no cross-round state in this build */ }

void LogPlayerHurt(const IGameEvent* event) {
    if (!event) return;
    const int victim_id = event->GetInt("userid");
    const int attacker_id = event->GetInt("attacker");
    const int dmg = event->GetInt("dmg_health");
    const int hitgroup = event->GetInt("hitgroup");
    const char* weapon = event->GetString("weapon");

    if (IsLocalUser(victim_id) && (LogHurt() || (LogNaded() && IsExplosive(weapon)))) {
        const int a_idx = IndexOf(attacker_id);
        char attacker_name[64];
        Name(a_idx, attacker_name, sizeof(attacker_name));
        LogLine(LogNaded() && IsExplosive(weapon) ? "naded" : "hurt",
                "took %d dmg from %s (%s) [hitgroup %d]", dmg, attacker_name, weapon ? weapon : "?",
                hitgroup);
    }

    // `main` c_event_logs::on_player_hurt: our hit, victim not a teammate, and the message
    // carries the hitgroup only when it is not a generic hit.
    if (IsLocalUser(attacker_id) && victim_id != attacker_id && LogHit()) {
        const int v_idx = IndexOf(victim_id);
        if (IsTeammateIndex(v_idx)) return;
        char victim_name[64];
        Name(v_idx, victim_name, sizeof(victim_name));
        if (hitgroup == 0 || hitgroup == 4) { // HITGROUP_GENERIC / HITGROUP_GEAR
            LogLine("hit", "Hit %s for %d (%d remaining)", victim_name, dmg,
                    event->GetInt("health"));
        } else {
            LogLine("hit", "Hit %s in the %s for %d (%d remaining)", victim_name,
                    HitGroupName(hitgroup), dmg, event->GetInt("health"));
        }
    }
}

void LogPlayerDeath(const IGameEvent* event) {
    if (!event) return;
    const int victim_id = event->GetInt("userid");
    const int attacker_id = event->GetInt("attacker");
    const char* weapon = event->GetString("weapon");

    if (IsLocalUser(victim_id) && LogNaded() && IsExplosive(weapon)) {
        const int a_idx = IndexOf(attacker_id);
        char attacker_name[64];
        Name(a_idx, attacker_name, sizeof(attacker_name));
        LogLine("naded", "killed by a %s from %s", weapon ? weapon : "grenade", attacker_name);
    }
}

void LogItemPurchase(const IGameEvent* event) {
    // `main` c_event_logs::on_item_purchase: enemy purchases only, "<name> bought <weapon>".
    if (!event || !LogBuy()) return;
    const int buyer_id = event->GetInt("userid");
    if (!buyer_id) return;
    const int buyer_idx = IndexOf(buyer_id);
    if (buyer_idx <= 0 || IsTeammateIndex(buyer_idx)) return;

    char buyer_name[64];
    Name(buyer_idx, buyer_name, sizeof(buyer_name));
    const char* weapon = event->GetString("weapon");
    LogLine("buy", "%s bought %s", buyer_name, weapon ? weapon : "an item");
}
} // namespace Misc::General::Detail
