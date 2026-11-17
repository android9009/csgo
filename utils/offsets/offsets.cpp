#include "offsets.hpp"
#include "../patterns/patterns.hpp"
#include "../log.hpp"
#include <string>

namespace offsets {
static std::unordered_map<std::string, std::uintptr_t> s_values;
std::uintptr_t Get(std::string_view name) {
    auto it = s_values.find(std::string(name));
    return it == s_values.end() ? 0u : it->second;
}
const std::unordered_map<std::string, std::uintptr_t>& All() { return s_values; }
bool Init() {
    s_values.clear();
    // Prefer patterns resolved against the live process; use the pinned build data only as fallback.
#define REGISTER_OFFSET(name) s_values.emplace(#name, Patterns::Get(#name) ? Patterns::Get(#name) : fallback::name)
    REGISTER_OFFSET(dwClientState); REGISTER_OFFSET(dwClientState_GetLocalPlayer);
    REGISTER_OFFSET(dwClientState_IsHLTV); REGISTER_OFFSET(dwClientState_Map);
    REGISTER_OFFSET(dwClientState_MapDirectory); REGISTER_OFFSET(dwClientState_MaxPlayer);
    REGISTER_OFFSET(dwClientState_PlayerInfo); REGISTER_OFFSET(dwClientState_State);
    REGISTER_OFFSET(dwClientState_ViewAngles); REGISTER_OFFSET(clientstate_delta_ticks);
    REGISTER_OFFSET(clientstate_last_outgoing_command); REGISTER_OFFSET(clientstate_choked_commands);
    REGISTER_OFFSET(clientstate_net_channel); REGISTER_OFFSET(dwEntityList); REGISTER_OFFSET(dwForceAttack);
    REGISTER_OFFSET(dwForceAttack2); REGISTER_OFFSET(dwForceBackward); REGISTER_OFFSET(dwForceForward);
    REGISTER_OFFSET(dwForceJump); REGISTER_OFFSET(dwForceLeft); REGISTER_OFFSET(dwForceRight);
    REGISTER_OFFSET(dwGameDir); REGISTER_OFFSET(dwGameRulesProxy); REGISTER_OFFSET(dwGetAllClasses);
    REGISTER_OFFSET(dwGlobalVars); REGISTER_OFFSET(dwGlowObjectManager); REGISTER_OFFSET(dwInput);
    REGISTER_OFFSET(dwInterfaceLinkList); REGISTER_OFFSET(dwLocalPlayer); REGISTER_OFFSET(dwMouseEnable);
    REGISTER_OFFSET(dwMouseEnablePtr); REGISTER_OFFSET(dwPlayerResource); REGISTER_OFFSET(dwRadarBase);
    REGISTER_OFFSET(dwSensitivity); REGISTER_OFFSET(dwSensitivityPtr); REGISTER_OFFSET(dwSetClanTag);
    REGISTER_OFFSET(dwViewMatrix); REGISTER_OFFSET(dwWeaponTable); REGISTER_OFFSET(dwWeaponTableIndex);
    REGISTER_OFFSET(dwYawPtr); REGISTER_OFFSET(dwZoomSensitivityRatioPtr); REGISTER_OFFSET(dwbSendPackets);
    REGISTER_OFFSET(dwppDirect3DDevice9); REGISTER_OFFSET(m_pStudioHdr); REGISTER_OFFSET(m_yawClassPtr);
    REGISTER_OFFSET(m_pitchClassPtr); REGISTER_OFFSET(interface_engine_cvar); REGISTER_OFFSET(convar_name_hash_table);
    REGISTER_OFFSET(m_bDormant); REGISTER_OFFSET(model_ambient_min); REGISTER_OFFSET(set_abs_angles);
    REGISTER_OFFSET(set_abs_origin); REGISTER_OFFSET(is_c4_owner); REGISTER_OFFSET(force_update_spectator_glow);
    REGISTER_OFFSET(anim_overlays); REGISTER_OFFSET(m_flSpawnTime); REGISTER_OFFSET(find_hud_element);
    REGISTER_OFFSET(m_bIsLocalPlayer);
// New prediction bindings intentionally have no timestamp-specific fallback.
    for(const auto* name:{"visual_smoke_count","input_thirdperson_flag","input_camera_offset","calc_viewmodel_view","prediction_current_command","prediction_last_command","prediction_random_seed","prediction_player","prediction_move_helper","prediction_forced_buttons","prediction_disabled_buttons","prediction_buttons","prediction_last_buttons","prediction_pressed_buttons","prediction_released_buttons","prediction_md5_fastcall","server_rank_reveal_handler"})
        s_values.emplace(name,Patterns::Get(name));
#undef REGISTER_OFFSET
    const bool ok = Get("dwClientState") && Get("dwEntityList") && Get("dwLocalPlayer");
    if (!ok) LOG_ERROR("Offset initialization did not resolve critical values.");
    return ok;
}
} // namespace offsets
