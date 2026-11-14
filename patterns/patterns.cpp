#include "patterns.hpp"
#include "../utils/memory.hpp"
#include "../utils/log.hpp"
#include <array>
#include <cstring>
#include <string>
#include <unordered_map>
#include <windows.h>
namespace Patterns {
static const std::vector<Definition> kDefinitions = {
    {"dwClientState", "engine.dll", "A1 ? ? ? ? 33 D2 6A 00 6A 00 33 C9 89 B0", {1, 0, 0, 0}, 1, true, 0},
    {"dwClientState_GetLocalPlayer", "engine.dll", "8B 80 ? ? ? ? 40 C3", {2, 0, 0, 0}, 1, false, 0},
    {"dwClientState_IsHLTV", "engine.dll", "80 BF ? ? ? ? ? 0F 84 ? ? ? ? 32 DB", {2, 0, 0, 0}, 1, false, 0},
    {"dwClientState_Map", "engine.dll", "05 ? ? ? ? C3 CC CC CC CC CC CC CC A1", {1, 0, 0, 0}, 1, false, 0},
    {"dwClientState_MapDirectory", "engine.dll", "B8 ? ? ? ? C3 05 ? ? ? ? C3", {7, 0, 0, 0}, 1, false, 0},
    {"dwClientState_MaxPlayer", "engine.dll", "A1 ? ? ? ? 8B 80 ? ? ? ? C3 CC CC CC CC 55 8B EC 8A 45 08", {7, 0, 0, 0}, 1, false, 0},
    {"dwClientState_PlayerInfo", "engine.dll", "8B 89 ? ? ? ? 85 C9 0F 84 ? ? ? ? 8B 01", {2, 0, 0, 0}, 1, false, 0},
    {"dwClientState_State", "engine.dll", "83 B8 ? ? ? ? ? 0F 94 C0 C3", {2, 0, 0, 0}, 1, false, 0},
    {"dwClientState_ViewAngles", "engine.dll", "F3 0F 11 86 ? ? ? ? F3 0F 10 44 24 ? F3 0F 11 86", {4, 0, 0, 0}, 1, false, 0},
    {"clientstate_delta_ticks", "engine.dll", "C7 87 ? ? ? ? ? ? ? ? FF 15 ? ? ? ? 83 C4 08", {2, 0, 0, 0}, 1, false, 0},
    {"clientstate_last_outgoing_command", "engine.dll", "8B 8F ? ? ? ? 8B 87 ? ? ? ? 41", {2, 0, 0, 0}, 1, false, 0},
    {"clientstate_choked_commands", "engine.dll", "8B 87 ? ? ? ? 41", {2, 0, 0, 0}, 1, false, 0},
    {"clientstate_net_channel", "engine.dll", "8B 8F ? ? ? ? 8B 01 8B 40 18", {2, 0, 0, 0}, 1, false, 0},
    {"dwEntityList", "client.dll", "BB ? ? ? ? 83 FF 01 0F 8C ? ? ? ? 3B F8", {1, 0, 0, 0}, 1, true, 0},
    {"dwForceAttack", "client.dll", "89 0D ? ? ? ? 8B 0D ? ? ? ? 8B F2 8B C1 83 CE 04", {2, 0, 0, 0}, 1, true, 0},
    {"dwForceAttack2", "client.dll", "89 0D ? ? ? ? 8B 0D ? ? ? ? 8B F2 8B C1 83 CE 04", {2, 0, 0, 0}, 1, true, 12},
    {"dwForceBackward", "client.dll", "55 8B EC 51 53 8A 5D 08", {287, 0, 0, 0}, 1, true, 0},
    {"dwForceForward", "client.dll", "55 8B EC 51 53 8A 5D 08", {245, 0, 0, 0}, 1, true, 0},
    {"dwForceJump", "client.dll", "8B 0D ? ? ? ? 8B D6 8B C1 83 CA 02", {2, 0, 0, 0}, 1, true, 0},
    {"dwForceLeft", "client.dll", "55 8B EC 51 53 8A 5D 08", {465, 0, 0, 0}, 1, true, 0},
    {"dwForceRight", "client.dll", "55 8B EC 51 53 8A 5D 08", {512, 0, 0, 0}, 1, true, 0},
    {"dwGameDir", "engine.dll", "68 ? ? ? ? 8D 85 ? ? ? ? 50 68 ? ? ? ? 68", {1, 0, 0, 0}, 1, true, 0},
    {"dwGameRulesProxy", "client.dll", "A1 ? ? ? ? 85 C0 0F 84 ? ? ? ? 80 B8 ? ? ? ? ? 74 7A", {1, 0, 0, 0}, 1, true, 0},
    {"dwGetAllClasses", "client.dll", "A1 ? ? ? ? C3 CC CC CC CC CC CC CC CC CC CC A1 ? ? ? ? B9", {1, 0, 0, 0}, 2, true, 0},
    {"dwGlobalVars", "engine.dll", "68 ? ? ? ? 68 ? ? ? ? FF 50 08 85 C0", {1, 0, 0, 0}, 1, true, 0},
    {"dwGlowObjectManager", "client.dll", "A1 ? ? ? ? A8 01 75 4B", {1, 0, 0, 0}, 1, true, 4},
    {"dwInput", "client.dll", "B9 ? ? ? ? F3 0F 11 04 24 FF 50 10", {1, 0, 0, 0}, 1, true, 0},
    {"dwInterfaceLinkList", "client.dll", "8B 35 ? ? ? ? 57 85 F6 74 ? 8B 7D 08 8B 4E 04 8B C7 8A 11 3A 10", {0, 0, 0, 0}, 0, true, 0},
    {"dwLocalPlayer", "client.dll", "8D 34 85 ? ? ? ? 89 15 ? ? ? ? 8B 41 08 8B 48 04 83 F9 FF", {3, 0, 0, 0}, 1, true, 4},
    {"dwMouseEnable", "client.dll", "B9 ? ? ? ? FF 50 34 85 C0 75 10", {1, 0, 0, 0}, 1, true, 48},
    {"dwMouseEnablePtr", "client.dll", "B9 ? ? ? ? FF 50 34 85 C0 75 10", {1, 0, 0, 0}, 1, true, 0},
    {"dwPlayerResource", "client.dll", "8B 3D ? ? ? ? 85 FF 0F 84 ? ? ? ? 81 C7", {2, 0, 0, 0}, 1, true, 0},
    {"dwRadarBase", "client.dll", "A1 ? ? ? ? 8B 0C B0 8B 01 FF 50 ? 46 3B 35 ? ? ? ? 7C EA 8B 0D", {1, 0, 0, 0}, 1, true, 0},
    {"dwSensitivity", "client.dll", "81 F9 ? ? ? ? 75 7E F3 0F 10 05", {2, 0, 0, 0}, 1, true, 0},
    {"dwSensitivityPtr", "client.dll", "81 F9 ? ? ? ? 75 7E F3 0F 10 05", {2, 0, 0, 0}, 1, true, 0},
    {"dwSetClanTag", "engine.dll", "53 56 57 8B DA 8B F9 FF 15", {0, 0, 0, 0}, 0, true, 0},
    {"dwViewMatrix", "client.dll", "0F 10 05 ? ? ? ? 8D 85 ? ? ? ? B9", {3, 0, 0, 0}, 1, true, 176},
    {"dwWeaponTable", "client.dll", "B9 ? ? ? ? 6A 00 FF 50 08 C3", {1, 0, 0, 0}, 1, true, 0},
    {"dwWeaponTableIndex", "client.dll", "39 86 ? ? ? ? 74 06 89 86 ? ? ? ? 8B 86", {2, 0, 0, 0}, 1, false, 0},
    {"dwYawPtr", "client.dll", "8B 44 24 1C 35 ? ? ? ? 89 44 24 14 EB 0D", {5, 0, 0, 0}, 1, true, 0},
    {"dwZoomSensitivityRatioPtr", "client.dll", "35 ? ? ? ? 89 45 FC EB 0A 8B 01 8B 40 30 FF D0 D9 5D FC A1", {1, 0, 0, 0}, 1, true, 0},
    {"dwbSendPackets", "engine.dll", "B3 01 8B 01 8B 40 10 FF D0 84 C0 74 0F 80 BF ? ? ? ? ? 0F 84", {0, 0, 0, 0}, 0, true, 1},
    {"dwppDirect3DDevice9", "shaderapidx9.dll", "A1 ? ? ? ? 50 8B 08 FF 51 0C", {1, 0, 0, 0}, 1, true, 0},
    {"m_pStudioHdr", "client.dll", "8B B6 ? ? ? ? 85 F6 74 05 83 3E 00 75 02 33 F6 F3 0F 10 44 24", {2, 0, 0, 0}, 1, false, 0},
    {"m_yawClassPtr", "client.dll", "8B 44 24 1C 35 ? ? ? ? 89 44 24 14 EB 0D", {5, 0, 0, 0}, 1, true, 0},
    {"m_pitchClassPtr", "client.dll", "B9 ? ? ? ? 89 44 24 40", {1, 0, 0, 0}, 1, true, 0},
    {"interface_engine_cvar", "vstdlib.dll", "8B 0D ? ? ? ? C7 05", {2, 0, 0, 0}, 1, true, 0},
    {"convar_name_hash_table", "vstdlib.dll", "8B 3C 85", {3, 0, 0, 0}, 1, true, 0},
    {"m_bDormant", "client.dll", "8A 81 ? ? ? ? C3 32 C0", {2, 0, 0, 0}, 1, false, 8},
    {"model_ambient_min", "engine.dll", "F3 0F 10 0D ? ? ? ? F3 0F 11 4C 24 ? 8B 44 24 20 35 ? ? ? ? 89 44 24 0C", {4, 0, 0, 0}, 1, true, 0},
    {"set_abs_angles", "client.dll", "55 8B EC 83 E4 F8 83 EC 64 53 56 57 8B F1 E8", {0, 0, 0, 0}, 0, true, 0},
    {"set_abs_origin", "client.dll", "55 8B EC 83 E4 F8 51 53 56 57 8B F1 E8", {0, 0, 0, 0}, 0, true, 0},
    {"is_c4_owner", "client.dll", "56 8B F1 85 F6 74 31", {0, 0, 0, 0}, 0, true, 0},
    {"force_update_spectator_glow", "client.dll", "74 07 8B CB E8 ? ? ? ? 83 C7 10", {0, 0, 0, 0}, 0, true, 0},
    {"anim_overlays", "client.dll", "8B 89 ? ? ? ? 8D 0C D1", {2, 0, 0, 0}, 1, false, 0},
    {"m_flSpawnTime", "client.dll", "89 86 ? ? ? ? E8 ? ? ? ? 80 BE ? ? ? ? ?", {2, 0, 0, 0}, 1, false, 0},
    {"find_hud_element", "client.dll", "55 8B EC 53 8B 5D 08 56 57 8B F9 33 F6 39 77 28", {0, 0, 0, 0}, 0, false, 0},
    {"m_bIsLocalPlayer", "client.dll", "80 BF ? ? ? ? ? F2 0F 10 1D", {2, 0, 0, 0}, 1, false, 0},
};
static std::unordered_map<std::string, std::uintptr_t> s_values;
static std::vector<std::string_view> s_missing;
const std::vector<Definition>& Definitions() { return kDefinitions; }
const std::vector<std::string_view>& Missing() { return s_missing; }
std::uintptr_t Get(std::string_view name) {
    auto it = s_values.find(std::string(name));
    return it == s_values.end() ? 0u : it->second;
}
static bool ReadU32(std::uintptr_t address, std::uint32_t& out) {
    if (!address) return false;
#if defined(_MSC_VER)
    __try { out = *reinterpret_cast<const std::uint32_t*>(address); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { out = 0; return false; }
#else
    std::memcpy(&out, reinterpret_cast<const void*>(address), sizeof(out));
    return true;
#endif
}
bool Init() {
    s_values.clear(); s_missing.clear();
    for (const auto& def : kDefinitions) {
        const auto match = MEM::FindPattern(def.module, def.signature);
        if (!match) { s_missing.push_back(def.name); continue; }
        std::uintptr_t value = match;
        bool valid = true;
        for (unsigned i = 0; i < def.offset_count; ++i) {
            value += static_cast<std::uintptr_t>(def.offsets[i]);
            std::uint32_t next = 0;
            // All listed CS:GO patterns are x86 and point to 32-bit immediates.
            if (!ReadU32(value, next)) { valid = false; break; }
            value = next;
        }
        if (!valid) { s_missing.push_back(def.name); continue; }
        auto* module = static_cast<std::uint8_t*>(MEM::GetModuleBase(def.module));
        if (def.relative && module) value = value - reinterpret_cast<std::uintptr_t>(module);
        value += static_cast<std::uintptr_t>(def.extra);
        s_values.emplace(std::string(def.name), value);
    }
    // Core pointers required for a usable base. Optional signatures can remain unavailable.
    if (!s_missing.empty()) LOG_WARN("%zu pattern(s) not found; pinned fallback offsets will be used where available.", s_missing.size());
    return MEM::GetModuleBase("client.dll") && MEM::GetModuleBase("engine.dll");
}
} // namespace Patterns
