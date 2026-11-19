#pragma once
// Signature catalogue and its resolution rules.
//
// This file is data plus pure arithmetic: it deliberately avoids Windows headers so
// the portable tests can validate every entry on any host.
//
// Three groups share one Definition type:
//   kLegacySnapshotCatalog - Hazedumper snapshot 2023-09-05 (58 entries). These have
//                            numeric fallbacks in utils/offsets/offsets.hpp.
//   kPortedCatalog         - signatures ported from this repository's `main` branch
//                            (airflow/opaihook offsets.cpp, shared block only, 99
//                            entries). No numeric fallbacks exist for these; an
//                            unmatched entry resolves to 0 and is reported as
//                            missing by Patterns::Init().
//   kReferenceCatalog      - signatures taken from public CS:GO sources to serve one
//                            specific feature (see docs/pattern-catalog.md). Kept in
//                            their own group so the provenance stays obvious; a
//                            missing entry disables that feature instead of guessing.
//
// Provenance, translation rules and the names that were intentionally NOT ported are
// documented in docs/pattern-catalog.md. Every ported address stays unverified until
// Patterns::Init() runs inside the target process.
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Patterns {
struct Definition {
    std::string_view name;
    std::string_view module;
    std::string_view signature;
    // Operand chain: add offsets[i] to the match, then read a 32-bit value from it.
    int offsets[4];
    unsigned char offset_count;
    // Publish a module-relative value (subtract the module base) when true.
    bool relative;
    // Constant added after resolution, e.g. for a return address inside a function.
    std::ptrdiff_t extra;
    // The match is an E8/E9 instruction and offsets[0] locates its rel32 displacement.
    bool call_target = false;
};

// Reads 32 bits from the target process. Returns false when the read is unsafe.
using Read32 = bool (*)(std::uintptr_t address, std::uint32_t& out);

// Turns a signature match into the value published by Patterns::Get().
// Address   : the match itself (plus `extra`).
// Pointer   : follow the operand chain; the read value is an absolute address.
// Offset    : follow the operand chain; the read value is a raw field offset.
// CallTarget: resolve the rel32 of the matched E8/E9 and use the callee.
inline bool Resolve(const Definition& definition, std::uintptr_t match,
                    std::uintptr_t module_base, Read32 read32, std::uintptr_t& out) {
    if (!match || !read32) return false;
    std::uintptr_t value = match;
    if (definition.call_target) {
        const auto operand = match + static_cast<std::uintptr_t>(definition.offsets[0]);
        std::uint32_t displacement = 0;
        if (!read32(operand, displacement)) return false;
        value = operand + sizeof(std::uint32_t) +
                static_cast<std::uintptr_t>(static_cast<std::int32_t>(displacement));
    } else {
        for (unsigned index = 0; index < definition.offset_count; ++index) {
            value += static_cast<std::uintptr_t>(definition.offsets[index]);
            std::uint32_t next = 0;
            if (!read32(value, next)) return false;
            value = next;
        }
    }
    if (definition.relative) {
        // "relative" means module-relative: without a module base there is nothing to
        // subtract, and a zero or out-of-module value means the operand was not
        // initialised yet, so the pattern is treated as unresolved.
        if (!module_base || !value || value < module_base) return false;
        value -= module_base;
    }
    out = static_cast<std::uintptr_t>(static_cast<std::intptr_t>(value) + definition.extra);
    return true;
}

// Hazedumper snapshot 2023-09-05. Runtime results are preferred; the constants in
// utils/offsets/offsets.hpp are only fallbacks for these names.
inline constexpr Definition kLegacySnapshotCatalog[] = {
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

// Ported from this repository's `main` branch (airflow/opaihook offsets.cpp, the
// block shared by every configuration - the `#ifdef LEGACY` block is deliberately
// excluded, it targets a different build with different signatures).
// Address entries publish the RVA of the matched instruction or function; Pointer
// entries publish the RVA of the operand target; CallTarget entries publish the RVA
// of the callee.
inline constexpr Definition kPortedCatalog[] = {
    // client.dll (80)
    {"accumulate_layers", "client.dll", "55 8B EC 57 8B F9 8B 0D ? ? ? ? 8B 01 8B 80", {0, 0, 0, 0}, 0, true, 0},
    {"add_dependencies", "client.dll", "55 8B EC 81 EC BC ? ? ? 53 56 57", {0, 0, 0, 0}, 0, true, 0},
    {"add_renderable", "client.dll", "55 8B EC 56 8B 75 08 57 FF 75 18", {0, 0, 0, 0}, 0, true, 0},
    {"add_view_model_bob", "client.dll", "55 8B EC A1 ? ? ? ? 83 EC 20 8B 40 34", {0, 0, 0, 0}, 0, true, 0},
    {"allow_extrapolation", "client.dll", "A2 ? ? ? ? 8B 45 E8", {1, 0, 0, 0}, 1, true, 0},
    {"attachments_helper", "client.dll", "55 8B EC 83 EC 48 53 8B 5D 08 89 4D F4", {0, 0, 0, 0}, 0, true, 0},
    {"calc_absolute_position", "client.dll", "55 8B EC 83 E4 ? 83 EC ? 80 3D ? ? ? ? ? 56 57 8B F9", {0, 0, 0, 0}, 0, true, 0},
    {"calc_chase_cam_view", "client.dll", "55 8B EC 83 E4 ? 81 EC ? ? ? ? 56 8B F1 57 8B 06", {0, 0, 0, 0}, 0, true, 0},
    {"calc_shotgun_spread", "client.dll", "E8 ? ? ? ? EB 38 83 EC 08", {1, 0, 0, 0}, 0, true, 0, true},
    {"calc_view_model_bob", "client.dll", "55 8B EC A1 ? ? ? ? 83 EC 10 56 8B F1 B9", {0, 0, 0, 0}, 0, true, 0},
    {"client_side_animation_list", "client.dll", "A1 ? ? ? ? F6 44", {1, 0, 0, 0}, 1, true, 0},
    {"clip_ray_to_hitbox", "client.dll", "55 8B EC 83 E4 F8 F3 0F 10 42", {0, 0, 0, 0}, 0, true, 0},
    {"compute_hitbox_surrounding_box", "client.dll", "55 8B EC 83 E4 F8 81 EC 24 04 00 00 ? ? ? ? ? ?", {0, 0, 0, 0}, 0, true, 0},
    {"copy_from_follow", "client.dll", "E8 ? ? ? ? F3 0F 10 45 ? 8D 84 24 ? ? ? ?", {1, 0, 0, 0}, 0, true, 0, true},
    {"copy_to_follow", "client.dll", "E8 ? ? ? ? 8B 87 ? ? ? ? 8D 8C 24 ? ? ? ? 8B 7C 24 18", {1, 0, 0, 0}, 0, true, 0, true},
    {"create_animstate", "client.dll", "55 8B EC 56 8B F1 B9 ? ? ? ? C7 46", {0, 0, 0, 0}, 0, true, 0},
    {"disable_post_process", "client.dll", "80 3D ? ? ? ? ? 53 56 57 0F 85", {2, 0, 0, 0}, 1, true, 0},
    {"do_extra_bone_processing", "client.dll", "55 8B EC 83 E4 F8 81 EC ? ? ? ? 53 56 8B F1 57 89 74 24 1C", {0, 0, 0, 0}, 0, true, 0},
    {"draw_models", "client.dll", "55 8B EC 83 E4 F8 51 8B 45 18", {0, 0, 0, 0}, 0, true, 0},
    {"estimate_abs_velocity", "client.dll", "55 8B EC 83 E4 ? 83 EC ? 56 8B F1 85 F6 74 ? 8B 06 8B 80 ? ? ? ? FF D0 84 C0 74 ? 8A 86", {0, 0, 0, 0}, 0, true, 0},
    {"eye_angles", "client.dll", "56 8B F1 85 F6 74 ? 8B 06 8B 80 ? ? ? ? FF D0 84 C0 74 ? 8A 86 ? ? ? ? 84 C0 74 ? 83 3D", {0, 0, 0, 0}, 0, true, 0},
    {"get_bone_merge", "client.dll", "89 86 ? ? ? ? E8 ? ? ? ? FF 75 08", {2, 0, 0, 0}, 1, false, 0},
    {"get_exposure_range", "client.dll", "55 8B EC 51 80 3D ? ? ? ? ? 0F 57", {0, 0, 0, 0}, 0, true, 0},
    {"get_name", "client.dll", "56 8B 35 ? ? ? ? 85 F6 74 21 8B 41", {0, 0, 0, 0}, 0, true, 0},
    {"get_pose_parameter", "client.dll", "55 8B EC 8B 45 08 57 8B F9 8B 4F 04 85 C9 75 15 8B", {0, 0, 0, 0}, 0, true, 0},
    {"global_vars", "client.dll", "A1 ? ? ? ? F3 0F 10 40 ? 0F 5A C0 F2 0F 11 04", {1, 0, 0, 0}, 1, true, 0},
    {"glow_object", "client.dll", "0F 11 05 ? ? ? ? 83 C8 01 C7 05 ? ? ? ? ? ? ? ?", {3, 0, 0, 0}, 1, true, 0},
    {"index_from_anim_tag_name", "client.dll", "56 57 8B F9 BE ? ? ? ? 0F 1F 80", {0, 0, 0, 0}, 0, true, 0},
    {"interpolate", "client.dll", "55 8B EC 83 E4 ? 83 EC ? 53 56 8B F1 57 83 BE ? ? ? ? ? 75 ? 8B 46 ? 8D 4E ? FF 50 ? 85 C0 74 ? 8B CE E8 ? ? ? ? 8B 9E", {0, 0, 0, 0}, 0, true, 0},
    {"interpolate_player", "client.dll", "55 8B EC 83 EC ? 56 8B F1 83 BE ? ? ? ? ? 0F 85", {0, 0, 0, 0}, 0, true, 0},
    {"interpolate_server_entities", "client.dll", "55 8B EC 83 EC 1C 8B 0D ? ? ? ? 53 56", {0, 0, 0, 0}, 0, true, 0},
    {"invalidate_bone_cache", "client.dll", "80 3D ? ? ? ? ? 74 16 A1 ? ? ? ? 48 C7 81", {0, 0, 0, 0}, 0, true, 0},
    {"invalidate_physics_recursive", "client.dll", "55 8B EC 83 E4 F8 83 EC 0C 53 8B 5D 08 8B C3 56 83 E0 04", {0, 0, 0, 0}, 0, true, 0},
    {"is_breakable_entity", "client.dll", "55 8B EC 51 56 8B F1 85 F6 74 68", {0, 0, 0, 0}, 0, true, 0},
    {"item_system", "client.dll", "A1 ? ? ? ? 85 C0 75 53", {0, 0, 0, 0}, 0, true, 0},
    {"line_goes_through_smoke", "client.dll", "55 8B EC 83 EC 08 8B 15 ? ? ? ? 0F 57 C0", {0, 0, 0, 0}, 0, true, 0},
    {"load_from_buffer", "client.dll", "55 8B EC 83 E4 F8 83 EC 34 53 8B 5D 0C 89", {0, 0, 0, 0}, 0, true, 0},
    {"local", "client.dll", "8B 0D ? ? ? ? 83 FF FF 74 07", {2, 0, 0, 0}, 1, true, 0},
    {"lookup_bone", "client.dll", "55 8B EC 53 56 8B F1 57 83 BE ? ? ? ? ? 75", {0, 0, 0, 0}, 0, true, 0},
    {"model_renderable_animating", "client.dll", "E8 ? ? ? ? 85 C0 75 04 33 C0 5E C3 83", {1, 0, 0, 0}, 0, true, 0, true},
    {"move_helper", "client.dll", "8B 0D ? ? ? ? 8B 46 08 68", {2, 0, 0, 0}, 1, true, 0},
    {"notify_on_layer_change_sequence", "client.dll", "55 8B EC 8B 45 ? 85 C0 74 ? 80 B9 ? ? ? ? ? 74 ? 8B 89", {0, 0, 0, 0}, 0, true, 0},
    {"notify_on_layer_change_weight", "client.dll", "55 8B EC 8B 45 ? 85 C0 74 ? 80 B9 ? ? ? ? ? 74 ? 56 8B B1 ? ? ? ? 85 F6 74 ? 8D 4D ? 51 50 8B CE E8 ? ? ? ? 84 C0 74 ? 83 7D ? ? 75 ? F3 0F 10 45 ? F3 0F 11 86 ? ? ? ? 5E 5D C2 ? ? CC CC CC CC CC CC CC CC CC CC 55 8B EC 8B 45", {0, 0, 0, 0}, 0, true, 0},
    {"on_latch_interpolated_variables", "client.dll", "55 8B EC 83 EC ? 53 56 8B F1 57 80 BE ? ? ? ? ? 75 ? 8B 06", {0, 0, 0, 0}, 0, true, 0},
    {"perform_screen_overlay", "client.dll", "55 8B EC 51 A1 ? ? ? ? 53 56 8B D9", {0, 0, 0, 0}, 0, true, 0},
    {"post_data_update", "client.dll", "55 8B EC 83 E4 ? 83 EC ? 53 56 57 83 CB", {0, 0, 0, 0}, 0, true, 0},
    {"post_process_data", "client.dll", "0F 11 05 ? ? ? ? 0F 10 87", {3, 0, 0, 0}, 1, true, 0},
    {"prediction_player", "client.dll", "89 35 ? ? ? ? F3 0F 10 48", {2, 0, 0, 0}, 1, true, 0},
    {"prediction_random_seed", "client.dll", "A3 ? ? ? ? 66 0F 6E 86", {1, 0, 0, 0}, 1, true, 0},
    {"process_interpolated_list", "client.dll", "53 0F B7 1D ? ? ? ? 56", {0, 0, 0, 0}, 0, true, 0},
    {"put_attachments", "client.dll", "55 8B EC 8B 45 ? 8B D1 83 F8 ? 0F 8C", {0, 0, 0, 0}, 0, true, 0},
    {"remove_smoke", "client.dll", "55 8B EC 83 EC 08 8B 15 ? ? ? ? 0F 57 C0", {8, 0, 0, 0}, 1, true, 0},
    {"render_glow_boxes", "client.dll", "53 8B DC 83 EC ? 83 E4 ? 83 C4 ? 55 8B 6B ? 89 6C ? ? 8B EC 83 EC ? 56 57 8B F9 89 7D ? 8B 4F", {0, 0, 0, 0}, 0, true, 0},
    {"reset_animstate", "client.dll", "56 6A 01 68 ? ? ? ? 8B F1", {0, 0, 0, 0}, 0, true, 0},
    {"reset_latched", "client.dll", "56 8B F1 57 8B BE ? ? ? ? 85 FF 74 ? 8B CF E8 ? ? ? ? 68", {0, 0, 0, 0}, 0, true, 0},
    {"return_addr_accumulate_layers", "client.dll", "84 C0 75 0D F6 87", {0, 0, 0, 0}, 0, true, 0},
    {"return_addr_apply_shake", "client.dll", "84 C0 75 24 A1 ? ? ? ? B9 ? ? ? ? FF 50 1C A1 ? ? ? ? 51 C7 04 24 ? ? 80 3F B9 ? ? ? ? 53 57 FF 50 20", {0, 0, 0, 0}, 0, true, 0},
    {"return_addr_drift_pitch", "client.dll", "84 C0 75 0B 8B 0D ? ? ? ? 8B 01 FF 50 4C", {0, 0, 0, 0}, 0, true, 0},
    {"return_addr_extrapolation", "client.dll", "FF D0 A1 ? ? ? ? B9 ? ? ? ? D9 1D ? ? ? ? FF 50 34 85 C0 74 22 8B 0D ? ? ? ?", {0, 0, 0, 0}, 0, true, 41},
    {"return_addr_maintain_seq_transitions", "client.dll", "84 C0 74 ? 8B 87 ? ? ? ? 89 87", {0, 0, 0, 0}, 0, true, 0},
    {"return_addr_reevaluate_anim_lod", "client.dll", "84 C0 0F 85 ? ? ? ? A1 ? ? ? ? 8B B7", {0, 0, 0, 0}, 0, true, 0},
    {"return_addr_setup_bones", "client.dll", "84 C0 0F 84 ? ? ? ? 8B 44 24 24", {0, 0, 0, 0}, 0, true, 0},
    {"return_addr_setup_velocity", "client.dll", "84 C0 75 38 8B 0D ? ? ? ? 8B 01 8B 80", {0, 0, 0, 0}, 0, true, 0},
    {"set_collision_bounds", "client.dll", "E8 ? ? ? ? 0F BF 87", {1, 0, 0, 0}, 0, true, 0, true},
    {"setup_aim_matrix", "client.dll", "55 8B EC 81 EC ? ? ? ? 53 56 57 8B 3D", {0, 0, 0, 0}, 0, true, 0},
    {"setup_bones", "client.dll", "55 8B EC 83 E4 F0 B8 D8", {0, 0, 0, 0}, 0, true, 0},
    {"setup_clr_modulation", "client.dll", "55 8B EC 83 7D 08 ? 7E", {0, 0, 0, 0}, 0, true, 0},
    {"setup_movement", "client.dll", "55 8B EC 83 E4 ? 81 EC ? ? ? ? 56 57 8B 3D ? ? ? ? 8B F1", {0, 0, 0, 0}, 0, true, 0},
    {"should_skip_anim_frame", "client.dll", "57 8B F9 8B 07 8B 80 ? ? ? ? FF D0 84 C0 75 02", {0, 0, 0, 0}, 0, true, 0},
    {"standard_blending_rules", "client.dll", "55 8B EC 83 E4 F0 B8 ? ? ? ? E8 ? ? ? ? 56 8B 75 08 57 8B F9 85 F6", {0, 0, 0, 0}, 0, true, 0},
    {"studio_hdr_ptr", "client.dll", "8B B7 ? ? ? ? 89 74 24 20", {2, 0, 0, 0}, 1, false, 0},
    {"trace_filter", "client.dll", "55 8B EC 83 E4 F0 83 EC 7C 56 52", {61, 0, 0, 0}, 1, true, 0},
    {"trace_filter_skip_entities", "client.dll", "C7 45 ? ? ? ? ? 89 45 E4 8B 01", {3, 0, 0, 0}, 1, true, 0},
    {"update_animstate", "client.dll", "55 8B EC 83 E4 F8 83 EC 18 56 57 8B F9 F3 0F 11 54 24", {0, 0, 0, 0}, 0, true, 0},
    {"update_client_side_animation", "client.dll", "55 8B EC 51 56 8B F1 80 BE ?? ?? 00 00 00 74 36 8B 06 FF 90 ?? ?? 00 00", {0, 0, 0, 0}, 0, true, 0},
    {"update_merge_cache", "client.dll", "E8 ? ? ? ? 83 7E 10 ? 74 64", {1, 0, 0, 0}, 0, true, 0, true},
    {"update_visibility", "client.dll", "55 8B EC 83 E4 F8 83 EC 24 53 56 57 8B F9 8D B7", {0, 0, 0, 0}, 0, true, 0},
    {"weapon_shootpos", "client.dll", "55 8B EC 56 8B 75 ? 57 8B F9 56 8B 07 FF 90", {0, 0, 0, 0}, 0, true, 0},
    {"weapon_system", "client.dll", "8B 35 ? ? ? ? FF 10 0F B7 C0", {2, 0, 0, 0}, 1, true, 0},
    {"write_user_cmd", "client.dll", "55 8B EC 83 E4 F8 51 53 56 8B D9", {0, 0, 0, 0}, 0, true, 0},

    // engine.dll (12)
    {"client_state", "engine.dll", "A1 ? ? ? ? 8B 80 ? ? ? ? C3", {1, 0, 0, 0}, 1, true, 0},
    {"construct_voice_data_message", "engine.dll", "56 57 8B F9 8D 4F 08 C7 07 ? ? ? ? E8 ? ? ? ? C7", {0, 0, 0, 0}, 0, true, 0},
    {"engine_paint", "engine.dll", "55 8B EC 83 EC 40 53 8B D9 8B 0D ? ? ? ? 89 5D F8", {0, 0, 0, 0}, 0, true, 0},
    {"file_system_ptr", "engine.dll", "8B 0D ? ? ? ? 83 C1 04 8B 01 FF 37 FF 50 1C 89 47 10", {2, 0, 0, 0}, 1, true, 0},
    {"force_update", "engine.dll", "A1 ? ? ? ? B9 ? ? ? ? 56 FF 50 14 8B 34 85", {0, 0, 0, 0}, 0, true, 0},
    {"load_named_sky", "engine.dll", "55 8B EC 81 EC ? ? ? ? 56 57 8B F9 C7 45", {0, 0, 0, 0}, 0, true, 0},
    {"process_packet", "engine.dll", "55 8B EC 83 E4 C0 81 EC ? ? ? ? 53 56 57 8B 7D 08 8B D9", {0, 0, 0, 0}, 0, true, 0},
    {"read_packets", "engine.dll", "53 8A D9 8B 0D ? ? ? ? 56 57 8B B9", {0, 0, 0, 0}, 0, true, 0},
    {"read_packets_return_addr", "engine.dll", "85 C0 0F 95 C0 84 C0 75 0C", {0, 0, 0, 0}, 0, true, 0},
    {"send_move_addr", "engine.dll", "B8 ? ? ? ? 3B F0 0F 4F F0 89 5D FC", {0, 0, 0, 0}, 0, true, 1},
    {"start_sound_immediate", "engine.dll", "E8 ? ? ? ? 3B F8 0F 4F C7", {1, 0, 0, 0}, 0, true, 0, true},
    {"temp_entities", "engine.dll", "55 8B EC 83 E4 F8 83 EC 4C A1 ? ? ? ? 80", {0, 0, 0, 0}, 0, true, 0},

    // materialsystem.dll (1)
    {"get_color_modulation", "materialsystem.dll", "55 8B EC 83 EC ? 56 8B F1 8A 46", {0, 0, 0, 0}, 0, true, 0},

    // server.dll (2)
    {"draw_hitbox", "server.dll", "55 8B EC 81 EC ? ? ? ? 53 56 8B 35 ? ? ? ? 8B D9 57 8B CE", {0, 0, 0, 0}, 0, true, 0},
    {"server_edict", "server.dll", "8B 15 ? ? ? ? 33 C9 83 7A 18 01", {0, 0, 0, 0}, 0, true, 0},

    // gameoverlayrenderer.dll (2)
    {"direct_present", "gameoverlayrenderer.dll", "FF 15 ? ? ? ? 8B F0 85 FF", {2, 0, 0, 0}, 1, true, 0},
    {"direct_reset", "gameoverlayrenderer.dll", "C7 45 ? ? ? ? ? FF 15 ? ? ? ? 8B D8", {9, 0, 0, 0}, 1, true, 0},

    // tier0.dll (1)
    {"thread_id_allocated", "tier0.dll", "C6 86 ? ? ? ? ? 83 05 ? ? ? ? ? 5E 75 04 33 C0 87 07", {2, 0, 0, 0}, 1, true, 0},

    // filesystem_stdio.dll (1)
    {"get_unverified_file_hashes", "filesystem_stdio.dll", "55 8B EC 81 C1 ? ? ? ? 5D E9 ? ? ? ? CC 55 8B EC 8B 45 ? 89 41", {0, 0, 0, 0}, 0, true, 0},
};

// Signatures that come from public CS:GO cheat sources rather than from this
// repository's `main` branch. Each one is used by exactly one feature, which stays
// switched off (and says so in the log) when its signature does not resolve.
inline constexpr Definition kReferenceCatalog[] = {
    // Misc "Auto-accept": the client's SetLocalPlayerReady (bool __stdcall(const char*)).
    // Same signature every public client has used since 2017; calling it with an empty
    // string is what the accept button does.
    {"set_local_player_ready", "client.dll", "55 8B EC 83 E4 F8 8B 4D 08 BA ? ? ? ? E8 ? ? ? ? 85 C0 75 12", {0, 0, 0, 0}, 0, true, 0},
    // Scoreboard "Reveal ranks": the client's ServerRankRevealAll handler, called as
    // char __cdecl(int*). Community signature for the 2019-2023 client.
    {"server_rank_reveal_all", "client.dll", "55 8B EC 8B 0D ? ? ? ? 85 C9 75 28 A1 ? ? ? ? 68 ? ? ? ? 8B 08 8B 01 FF 50 04 85 C0 74 0B 8B C8 E8 ? ? ? ? 8B C8 EB 02 33 C9 89 0D ? ? ? ? 8B 45 08", {0, 0, 0, 0}, 0, true, 0},
};
} // namespace Patterns
