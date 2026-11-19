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
#undef REGISTER_OFFSET

    // Signatures ported from the `main` branch (airflow/opaihook, shared block).
    // There are no pinned numeric fallbacks for these, so an unmatched entry
    // publishes 0 and shows up in Patterns::Missing().
#define REGISTER_SIGNATURE(name) s_values.emplace(#name, Patterns::Get(#name))
    REGISTER_SIGNATURE(accumulate_layers); REGISTER_SIGNATURE(add_dependencies);
    REGISTER_SIGNATURE(add_renderable); REGISTER_SIGNATURE(add_view_model_bob);
    REGISTER_SIGNATURE(allow_extrapolation); REGISTER_SIGNATURE(attachments_helper);
    REGISTER_SIGNATURE(calc_absolute_position); REGISTER_SIGNATURE(calc_chase_cam_view);
    REGISTER_SIGNATURE(calc_shotgun_spread); REGISTER_SIGNATURE(calc_view_model_bob);
    REGISTER_SIGNATURE(client_side_animation_list); REGISTER_SIGNATURE(client_state);
    REGISTER_SIGNATURE(clip_ray_to_hitbox); REGISTER_SIGNATURE(compute_hitbox_surrounding_box);
    REGISTER_SIGNATURE(construct_voice_data_message); REGISTER_SIGNATURE(copy_from_follow);
    REGISTER_SIGNATURE(copy_to_follow); REGISTER_SIGNATURE(create_animstate);
    REGISTER_SIGNATURE(direct_present); REGISTER_SIGNATURE(direct_reset);
    REGISTER_SIGNATURE(disable_post_process); REGISTER_SIGNATURE(do_extra_bone_processing);
    REGISTER_SIGNATURE(draw_hitbox); REGISTER_SIGNATURE(draw_models); REGISTER_SIGNATURE(engine_paint);
    REGISTER_SIGNATURE(estimate_abs_velocity); REGISTER_SIGNATURE(eye_angles);
    REGISTER_SIGNATURE(file_system_ptr); REGISTER_SIGNATURE(force_update);
    REGISTER_SIGNATURE(get_bone_merge); REGISTER_SIGNATURE(get_color_modulation);
    REGISTER_SIGNATURE(get_exposure_range); REGISTER_SIGNATURE(get_name);
    REGISTER_SIGNATURE(get_pose_parameter); REGISTER_SIGNATURE(get_unverified_file_hashes);
    REGISTER_SIGNATURE(global_vars); REGISTER_SIGNATURE(glow_object);
    REGISTER_SIGNATURE(index_from_anim_tag_name); REGISTER_SIGNATURE(interpolate);
    REGISTER_SIGNATURE(interpolate_player); REGISTER_SIGNATURE(interpolate_server_entities);
    REGISTER_SIGNATURE(invalidate_bone_cache); REGISTER_SIGNATURE(invalidate_physics_recursive);
    REGISTER_SIGNATURE(is_breakable_entity); REGISTER_SIGNATURE(item_system);
    REGISTER_SIGNATURE(line_goes_through_smoke); REGISTER_SIGNATURE(load_from_buffer);
    REGISTER_SIGNATURE(load_named_sky); REGISTER_SIGNATURE(local); REGISTER_SIGNATURE(lookup_bone);
    REGISTER_SIGNATURE(model_renderable_animating); REGISTER_SIGNATURE(move_helper);
    REGISTER_SIGNATURE(notify_on_layer_change_sequence); REGISTER_SIGNATURE(notify_on_layer_change_weight);
    REGISTER_SIGNATURE(on_latch_interpolated_variables); REGISTER_SIGNATURE(perform_screen_overlay);
    REGISTER_SIGNATURE(post_data_update); REGISTER_SIGNATURE(post_process_data);
    REGISTER_SIGNATURE(prediction_player); REGISTER_SIGNATURE(prediction_random_seed);
    REGISTER_SIGNATURE(process_interpolated_list); REGISTER_SIGNATURE(process_packet);
    REGISTER_SIGNATURE(put_attachments); REGISTER_SIGNATURE(read_packets);
    REGISTER_SIGNATURE(read_packets_return_addr); REGISTER_SIGNATURE(remove_smoke);
    REGISTER_SIGNATURE(render_glow_boxes); REGISTER_SIGNATURE(reset_animstate);
    REGISTER_SIGNATURE(reset_latched); REGISTER_SIGNATURE(return_addr_accumulate_layers);
    REGISTER_SIGNATURE(return_addr_apply_shake); REGISTER_SIGNATURE(return_addr_drift_pitch);
    REGISTER_SIGNATURE(return_addr_extrapolation); REGISTER_SIGNATURE(return_addr_maintain_seq_transitions);
    REGISTER_SIGNATURE(return_addr_reevaluate_anim_lod); REGISTER_SIGNATURE(return_addr_setup_bones);
    REGISTER_SIGNATURE(return_addr_setup_velocity); REGISTER_SIGNATURE(send_move_addr);
    REGISTER_SIGNATURE(server_edict); REGISTER_SIGNATURE(set_collision_bounds);
    REGISTER_SIGNATURE(setup_aim_matrix); REGISTER_SIGNATURE(setup_bones);
    REGISTER_SIGNATURE(setup_clr_modulation); REGISTER_SIGNATURE(setup_movement);
    REGISTER_SIGNATURE(should_skip_anim_frame); REGISTER_SIGNATURE(standard_blending_rules);
    REGISTER_SIGNATURE(start_sound_immediate); REGISTER_SIGNATURE(studio_hdr_ptr);
    REGISTER_SIGNATURE(temp_entities); REGISTER_SIGNATURE(thread_id_allocated);
    REGISTER_SIGNATURE(trace_filter); REGISTER_SIGNATURE(trace_filter_skip_entities);
    REGISTER_SIGNATURE(update_animstate); REGISTER_SIGNATURE(update_client_side_animation);
    REGISTER_SIGNATURE(update_merge_cache); REGISTER_SIGNATURE(update_visibility);
    REGISTER_SIGNATURE(weapon_shootpos); REGISTER_SIGNATURE(weapon_system);
    REGISTER_SIGNATURE(write_user_cmd);
#undef REGISTER_SIGNATURE

    // Single-feature signatures kept apart from the ported block (see
    // utils/patterns/catalog.hpp). Zero means "not found"; the owning feature then
    // logs once and keeps itself off.
    s_values.emplace("server_rank_reveal_all", Patterns::Get("server_rank_reveal_all"));
    s_values.emplace("set_local_player_ready", Patterns::Get("set_local_player_ready"));
    s_values.emplace("send_datagram", Patterns::Get("send_datagram"));
    s_values.emplace("send_datagram_alt", Patterns::Get("send_datagram_alt"));
    const bool ok = Get("dwClientState") && Get("dwEntityList") && Get("dwLocalPlayer");
    if (!ok) LOG_ERROR("Offset initialization did not resolve critical values.");
    return ok;
}
} // namespace offsets
