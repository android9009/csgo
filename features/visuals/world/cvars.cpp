#include "world.hpp"
#include "crosshair.hpp"
#include "trace.hpp"

#include "../../../sdk/config_system/config_system.hpp"
#include "../settings.hpp"
#include "../dependencies/source.hpp"

#include "../../../runtime/debug_console.hpp"
#include "../../../runtime/hooks.hpp"
#include "../../../sdk/game/globals.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace world_visuals {
void run_thirdperson();
}

namespace {
struct convar_state {
    const char* name;
    c_convar* object = nullptr;
    float original = 0.f;
    bool original_valid = false;
    bool overridden = false;
    float last_applied = 0.f;
};

std::array<convar_state, 21> convars{{
    { "viewmodel_fov" },
    { "viewmodel_offset_x" },
    { "viewmodel_offset_y" },
    { "viewmodel_offset_z" },
    { "cl_wpn_sway_scale" },
    { "cl_wpn_sway_interp" },
    { "cl_bobamt_lat" },
    { "cl_bobamt_vert" },
    { "cl_bob_lower_amt" },
    { "cl_viewmodel_shift_left_amt" },
    { "cl_viewmodel_shift_right_amt" },
    { "fog_override" },
    { "fog_maxdensity" },
    { "mat_postprocess_enable" },
    { "r_shadows" },
    { "r_aspectratio" },
    { "r_drawviewmodel" },
    { "spec_allow_roaming" },
    { "cl_crosshairalpha" },
    { "cl_disable_ragdolls" },
    { "cl_ragdoll_physics_enable" }
}};
convar_state tonemap_scale{ "mat_force_tonemap_scale" };

using override_view_t = void(__thiscall*)(void*, c_view_setup*);
override_view_t original_override_view = nullptr;
bool override_installed = false;

// World > Removals > Smoke: main's remove_smoke - the smoke counter the client
// checks in CSmokeScreenEffect::Render (offsets::remove_smoke).
void clear_smoke_counter() {
    if (int** counter = offsets::remove_smoke.cast<int**>(); counter && *counter)
        **counter = 0;
}

float read_convar_float(c_convar* object, bool& valid) {
    valid = false;
    if (!object || !object->parent)
        return 0.f;
    const float result = object->get_float();
    valid = std::isfinite(result);
    return valid ? result : 0.f;
}

void set_convar_float(c_convar* object, float value) {
    if (object && std::isfinite(value))
        object->set_value(value);
}

c_convar* find_convar(const char* name) {
    return name ? HACKS->cvar->find_convar(name) : nullptr;
}

void cache_convars() {
    for (auto& item : convars) {
        item.object = find_convar(item.name);
        item.original = read_convar_float(item.object, item.original_valid);
    }
    tonemap_scale.object = find_convar(tonemap_scale.name);
    tonemap_scale.original = read_convar_float(tonemap_scale.object, tonemap_scale.original_valid);
}

convar_state* get_convar(const char* name) {
    if (!name)
        return nullptr;
    // OverrideView resolves the same ~35 literals every frame. Call sites pass
    // string literals, so a pointer-keyed memo replaces the linear strcmp scan;
    // entries are only cached once the convar actually resolved.
    struct memo_entry {
        const char* name;
        convar_state* item;
    };
    static memo_entry memo[64]{};
    static int memo_count = 0;
    for (int i = 0; i < memo_count; ++i) {
        if (memo[i].name == name)
            return memo[i].item;
    }
    convar_state* resolved = nullptr;
    for (auto& item : convars) {
        if (std::strcmp(item.name, name) == 0) {
            resolved = &item;
            break;
        }
    }
    if (!resolved && std::strcmp(tonemap_scale.name, name) == 0)
        resolved = &tonemap_scale;
    if (resolved && memo_count < static_cast<int>(std::size(memo))) {
        memo[memo_count].name = name;
        memo[memo_count].item = resolved;
        ++memo_count;
    }
    return resolved;
}

void set_override(const char* name, bool enabled, float value) {
    convar_state* item = get_convar(name);
    if (!item || !item->object)
        return;
    if (enabled) {
        if (!item->original_valid) {
            item->original = read_convar_float(item->object, item->original_valid);
        }
        if (item->original_valid &&
            (!item->overridden || std::fabs(item->last_applied - value) > 0.0001f)) {
            set_convar_float(item->object, value);
            item->last_applied = value;
            item->overridden = true;
        }
    } else if (item->overridden) {
        set_convar_float(item->object, item->original);
        item->overridden = false;
    }
}

// set_convar_float goes through ConVar::SetValue, which clamps to the convar's
// registered min/max - that is why viewmodel_fov could never leave 54..68. The
// engine stores the value XOR-encoded with the convar pointer, so writing that
// cell directly bypasses the clamp the same way the NL Lua's cvar:float does.
bool set_convar_float_raw(c_convar* object, float value) {
    if (!object || !object->parent || !std::isfinite(value))
        return false;
    if (!std::isfinite(object->get_float()))
        return false;
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    *reinterpret_cast<std::uint32_t*>(&object->parent->float_value) =
        bits ^ static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(object));
    return true;
}

void set_override_raw(const char* name, bool enabled, float value) {
    convar_state* item = get_convar(name);
    if (!item || !item->object)
        return;
    if (enabled) {
        if (!item->original_valid) {
            item->original = read_convar_float(item->object, item->original_valid);
        }
        if (item->original_valid &&
            (!item->overridden || std::fabs(item->last_applied - value) > 0.0001f)) {
            // If the encoded cell could not be decoded the build differs; fall back
            // to the clamped SetValue so the override at least keeps working.
            if (!set_convar_float_raw(item->object, value))
                set_convar_float(item->object, value);
            item->last_applied = value;
            item->overridden = true;
        }
    } else if (item->overridden) {
        if (!set_convar_float_raw(item->object, item->original))
            set_convar_float(item->object, item->original);
        item->overridden = false;
    }
}

void update_cvars() {
    const auto& world = g_cfg->world;
    const auto& world_esp = g_cfg->world_esp;
    const auto& page = player_visuals::world_settings();
    g_cfg->world.m_remove_post_processing = world.m_remove_post_processing || page.removals[9];
    const bool fog = world.m_render_fog;
    set_override("fog_override", fog, fog ? 1.f : 0.f);
    set_override("fog_maxdensity", fog, 0.f);
    set_override("mat_postprocess_enable", g_cfg->world.m_remove_post_processing, 0.f);
    set_override("r_shadows", world.m_remove_shadows, 0.f);
    set_override("spec_allow_roaming", page.unlock_spectating_mode > 0 || world.m_unlock_spectating, 1.f);
    float aspect_ratio = std::clamp(world_esp.m_aspect_ratio, 0.f, 2.5f);
    if (page.aspect_scaling == 1 && aspect_ratio > 0.01f)
        aspect_ratio = 1.f / aspect_ratio;
    set_override("r_aspectratio", page.aspect_ratio_override, aspect_ratio);
    g_cfg->world.m_custom_crosshair = page.force_sniper_crosshair || page.force_crosshair_scoped ||
        page.force_crosshair_c4;
    // Scope overlay: 0 Default (game HudZoom), 1 Lines and 2 Custom both replace
    // the engine overlay, so HudZoom is suppressed for every non-default mode.
    g_cfg->world.m_remove_scope_overlay = page.scope_overlay_mode != 0;
    g_cfg->world.m_remove_scoreboard = page.scoreboard_opacity <= 0.f;
    g_cfg->world_esp.m_skip_second_zoom = page.instant_scope;
    g_cfg->visuals.m_effects.m_hitmarker = page.hitmarker_2d || page.hitmarker_3d || page.damage_marker;
    g_cfg->visuals.m_effects.m_bullet_tracers = page.tracer_enemies || page.tracer_teammates || page.tracer_local_player;
    g_cfg->visuals.m_effects.m_tracer_color = page.tracer_enemy_color;
    g_cfg->visuals.m_effects.m_bullet_impacts = page.bullet_impacts || page.impact_sparks;

    visuals_source::local_view_state view_state{};
    const bool view_state_valid = visuals_source::get_local_view_state(view_state);
    const bool scoped = view_state_valid && view_state.scoped;
    set_override("r_drawviewmodel", world.m_show_viewmodel_in_scope && scoped, 1.f);
    // World > Crosshairs: the engine crosshair is drawn from the weapon state,
    // so "hide on knife/grenade" is an alpha override rather than a paint skip.
    const bool hide_crosshair = view_state_valid &&
        ((view_state.holding_knife && page.hide_crosshair_knife) ||
         (view_state.holding_grenade && page.hide_crosshair_grenade));
    set_override("cl_crosshairalpha", hide_crosshair, 0.f);
    // World > Ragdolls: 0 Default, 1 Remove, 2 Client Side.
    const int ragdoll_mode = std::clamp(page.ragdoll_mode, 0, 2);
    set_override("cl_disable_ragdolls", ragdoll_mode == 1, 1.f);
    set_override("cl_ragdoll_physics_enable", ragdoll_mode == 2, 1.f);
    g_cfg->world.m_remove_flash = world.m_remove_flash || page.removals[5];
    g_cfg->world.m_remove_smoke = world.m_remove_smoke || page.removals[6];
    g_cfg->world.m_remove_post_processing = world.m_remove_post_processing || page.removals[9];
    AIRFLOW_DEBUG_STAGE("cvars.flash");
    visuals_source::set_flash_removed(g_cfg->world.m_remove_flash);
    if (g_cfg->world.m_remove_smoke)
        clear_smoke_counter();

    AIRFLOW_DEBUG_STAGE("cvars.post_processing");
    const int exposure = std::clamp(world.m_exposure, 0, 100);
    const float brightness = std::clamp(world.m_brightness, 0.f, 1.f);
    const bool tone_changed = exposure != 20 || std::fabs(brightness - 0.5f) > 0.001f;
    const float tone_scale = std::clamp((static_cast<float>(exposure) / 20.f) *
        (brightness * 2.f), 0.f, 5.f);
    set_override("mat_force_tonemap_scale", tone_changed, tone_scale);

    AIRFLOW_DEBUG_STAGE("cvars.viewmodel_fov");
    // FOV goes through the raw cell: the convar's registered range is 54..68 and
    // SetValue clamped every value we pushed into it. Position/angles are applied
    // in the CalcViewModelView hook (see visuals_source), never through cvars.
    const int vm_fov = std::clamp(world.m_viewmodel_fov, 0, 30);
    const bool override_vm_fov = page.viewmodel_fov_override ||
        (page.scope_force_viewmodel_fov && scoped) || vm_fov != 0;
    const float vm_fov_value = page.viewmodel_fov_override || (page.scope_force_viewmodel_fov && scoped)
        ? static_cast<float>(std::clamp(page.viewmodel_fov_value, 40, 150))
        : convars[0].original + static_cast<float>(vm_fov);
    set_override_raw("viewmodel_fov", override_vm_fov, vm_fov_value);

    AIRFLOW_DEBUG_STAGE("cvars.modifiers");
    const bool override_modifiers = page.viewmodel_modifiers_override;
    set_override("cl_wpn_sway_scale", override_modifiers || world.m_remove_viewmodel_move,
        override_modifiers ? page.modifier_velocity_sway : 0.f);
    set_override("cl_wpn_sway_interp", override_modifiers || world.m_remove_viewmodel_move,
        override_modifiers ? page.modifier_rotation_sway : 0.f);
    set_override("cl_bobamt_lat", override_modifiers || world.m_remove_viewmodel_move,
        override_modifiers ? page.modifier_bobbing : 0.f);
    // Disable Landing Bob: the landing dip is the vertical bob + lower amount, not
    // the shift amts (those only cancel strafe/turn shift), so zero both here.
    const bool no_landing_bob = world.m_remove_landing_bob;
    set_override("cl_bobamt_vert", override_modifiers || world.m_remove_viewmodel_move || no_landing_bob,
        no_landing_bob ? 0.f : (override_modifiers ? page.modifier_bobbing : 0.f));
    set_override("cl_bob_lower_amt", override_modifiers || world.m_remove_viewmodel_move || no_landing_bob,
        no_landing_bob ? 0.f : (override_modifiers ? page.modifier_lower_weapon : 0.f));
    set_override("cl_viewmodel_shift_left_amt", override_modifiers || no_landing_bob,
        override_modifiers ? page.modifier_in_air_offset : 0.f);
    set_override("cl_viewmodel_shift_right_amt", override_modifiers || no_landing_bob,
        override_modifiers ? page.modifier_ladder_bob : 0.f);
}

void __fastcall hooked_override_view(void* self, void*, c_view_setup* setup) {
    AIRFLOW_DEBUG_STAGE("world.override_view.camera");
    // main calls THIRDPSERON->run_alive() here, before the original.
    world_visuals::run_thirdperson();
    AIRFLOW_DEBUG_STAGE("world.override_view.cvars");
    update_cvars();
    AIRFLOW_DEBUG_STAGE("world.override_view.original");
    if (original_override_view)
        original_override_view(self, setup);
    AIRFLOW_DEBUG_STAGE("world.override_view.setup");
    if (!setup)
        return;

    // World > Viewmodel > Position / Angles / Visual recoil are applied in the
    // CalcViewModelView hook (visuals_source::install_viewmodel_hook), which is
    // the only place the game reads them from - writing the entity here was what
    // the game overwrote and what crashed.

    const auto& cfg = g_cfg->world_esp;
    if (cfg.m_enable_override_fov)
        setup->fov = static_cast<float>(std::clamp(cfg.m_override_fov, 60, 130));

    visuals_source::local_view_state view_state{};
    if (visuals_source::get_local_view_state(view_state)) {
        const auto& page = player_visuals::world_settings();
        // World > Visual Recoil follows its label: Enabled keeps the kick the
        // engine adds, Disabled is the one that subtracts it (the combo used to
        // be wired the other way round).
        if (view_state.alive && !g_cfg->world.m_visual_recoil) {
            setup->angles.x -= view_state.aim_punch.x * 0.9f + view_state.view_punch.x;
            setup->angles.y -= view_state.aim_punch.y * 0.9f + view_state.view_punch.y;
        }
        if (view_state.scoped && page.scope_fov_override) {
            // The second scope is not a lower FOV window: m_iFOV drops to <= 20
            // only on the second zoom stage (AWP 40/10, SSG 40/15), so read the
            // netvar instead of guessing from the setup->fov the game handed us.
            const bool second_scope = view_state.player_fov > 0
                ? view_state.player_fov <= 20
                : view_state.zoom_level >= 2;
            // The sliders are "how much of the scope" in percent, so 0 % leaves the
            // 90-degree view alone and 100 % gives the weapon's own scope FOV.
            const int amount = std::clamp(second_scope ? page.scope_fov_x2 : page.scope_fov_x1, 0, 100);
            const float base_fov = second_scope ? 15.f : 40.f;
            const float factor = static_cast<float>(amount) / 100.f;
            setup->fov = 90.f + (base_fov - 90.f) * factor;
        } else if (view_state.scoped && cfg.m_zoom_fov > 0)
            setup->fov = static_cast<float>(std::clamp(cfg.m_zoom_fov, 20, 90));
        else if (view_state.scoped && cfg.m_skip_second_zoom && setup->fov < 20.f)
            setup->fov = 40.f;
    }
}

namespace world_visuals {
void run_thirdperson();
}

// --- engine trace (HACKS->engine_trace, main's ray_t / c_game_trace) ---------
// main's thirdperson shoots a 32x32 box behind the camera and scales the distance
// with the fraction, so the camera never ends up inside a wall.
// The camera/grenade world trace uses main's c_trace_filter_world_and_props_only
// (no entity is hit); the grenade entity trace uses c_trace_filter_skip_entity
// (every entity except the thrower). entity == nullptr marks a world hit.
void fill_swept_trace(const c_game_trace& result, ::world_visuals::swept_trace& out) {
    out.fraction = std::clamp(result.fraction, 0.f, 1.f);
    out.hit = result.fraction < 1.f;
    out.start_solid = result.start_solid;
    out.all_solid = result.all_solid;
    out.contents = static_cast<unsigned int>(result.contents);
    out.entity = result.fraction < 1.f || result.start_solid ? result.entity : nullptr;
    out.end = { result.end.x, result.end.y, result.end.z };
    out.normal = { result.plane.normal.x, result.plane.normal.y, result.plane.normal.z };
}

bool trace_hull(const vec3_t& from, const vec3_t& to, const vec3_t& mins, const vec3_t& maxs,
                unsigned int mask, i_trace_filter& filter, c_game_trace& result) {
    if (!HACKS->engine_trace)
        return false;
    result.clear();
    const ray_t ray(from, to, mins, maxs);
    HACKS->engine_trace->trace_ray(ray, mask, &filter, &result);
    return std::isfinite(result.fraction);
}

bool trace_line(const vec3_t& from, const vec3_t& to, const vec3_t& mins, const vec3_t& maxs,
                c_game_trace& result) {
    c_trace_filter_world_and_props_only filter;
    return trace_hull(from, to, mins, maxs, MASK_NPCWORLDSTATIC, filter, result);
}

float trace_fraction(const vec3_t& from, const vec3_t& to, const vec3_t& mins, const vec3_t& maxs) {
    c_game_trace result;
    if (!trace_line(from, to, mins, maxs, result))
        return 1.f;
    return std::clamp(result.fraction, 0.f, 1.f);
}

// SURF_NOPENTRATE: surfaces a bullet can never pass.
constexpr unsigned short surf_nopentrate = 0x2000;

// Game crosshair settings, so the force options can show the crosshair the player
// actually uses instead of a shape of our own.
float convar_value(const char* name, float fallback) {
    c_convar* object = find_convar(name);
    if (!object)
        return fallback;
    bool valid = false;
    const float value = read_convar_float(object, valid);
    return valid ? value : fallback;
}

vec3_t view_forward(const vec3_t& angles) {
    constexpr float pi = 3.14159265358979323846f;
    const float pitch = angles.x * (pi / 180.f);
    const float yaw = angles.y * (pi / 180.f);
    const float cp = std::cos(pitch);
    return { cp * std::cos(yaw), cp * std::sin(yaw), -std::sin(pitch) };
}

vec3_t engine_view_angles() {
    vec3_t angles{};
    HACKS->engine->get_view_angles(angles);
    return angles;
}

vec3_t to_vec3(const visuals_source::vector3& v) {
    return { v.x, v.y, v.z };
}

} // namespace

namespace world_visuals {
// main's c_thirdperson::run_alive, one to one: flip camera_in_third_person,
// trace a 32x32 box backwards from the eye and store the view angles with the
// (wall-clamped) distance in z as camera_offset. main runs this from its
// OverrideView hook before the original, which is where this is called from.
void run_thirdperson() {
    c_input* input = HACKS->input;
    if (!input)
        return;

    const auto& cfg = g_cfg->world_esp;
    const auto& page = player_visuals::world_settings();
    visuals_source::local_view_state state{};
    const bool have_state = visuals_source::get_local_view_state(state);
    const bool alive = have_state && state.alive;
    const bool active = cfg.m_enable_thirdperson && HACKS->engine->is_in_game() &&
        HACKS->engine->get_local_player() > 0 && alive;

    static bool enabled = false;
    if (active) {
        input->camera_in_third_person = true;
        enabled = true;
    } else if (input->camera_in_third_person && enabled) {
        input->camera_in_third_person = false;
        input->camera_offset.z = 0.f;
        enabled = false;
        return;
    }
    if (!input->camera_in_third_person)
        return;

    vec3_t offset = engine_view_angles();
    const vec3_t forward = view_forward(offset);
    offset.z = static_cast<float>(std::clamp(cfg.m_distance, 50, 300));

    const vec3_t eye = to_vec3(state.eye_origin);
    if (page.thirdperson_close_to_wall) {
        const vec3_t target{ eye.x - forward.x * offset.z, eye.y - forward.y * offset.z,
            eye.z - forward.z * offset.z };
        offset.z *= trace_fraction(eye, target, { -16.f, -16.f, -16.f }, { 16.f, 16.f, 16.f });
    }
    input->camera_offset = offset;
}

// Fatality's util_trace_hull: the camera trace above, but with the caller's hull and
// contents mask so the grenade prediction can sweep a 2x2x2 box through the world.
bool trace_swept(const visuals_source::vector3& from, const visuals_source::vector3& to,
                 const visuals_source::vector3& mins, const visuals_source::vector3& maxs,
                 unsigned int mask, swept_trace& out) {
    c_trace_filter_world_and_props_only filter;
    c_game_trace result;
    if (!trace_hull(to_vec3(from), to_vec3(to), to_vec3(mins), to_vec3(maxs), mask, filter, result))
        return false;
    fill_swept_trace(result, out);
    return true;
}

bool trace_swept_entities(const visuals_source::vector3& from, const visuals_source::vector3& to,
                          const visuals_source::vector3& mins, const visuals_source::vector3& maxs,
                          unsigned int mask, void* skip_entity, swept_trace& out) {
    c_trace_filter_skip_entity filter(static_cast<c_base_entity*>(skip_entity));
    c_game_trace result;
    if (!trace_hull(to_vec3(from), to_vec3(to), to_vec3(mins), to_vec3(maxs), mask, filter, result))
        return false;
    fill_swept_trace(result, out);
    return true;
}

float world_convar(const char* name, float fallback) {
    return convar_value(name, fallback);
}

// Fatality's autowall crosshair decides its colour from penetration::can_wallbang();
// a full penetration simulation needs the weapon data table, so this is the cheap
// version of the same question: trace to the wall, then measure how thick it is.
bool wallbang_possible() {
    visuals_source::local_view_state state{};
    if (!visuals_source::get_local_view_state(state) || !state.alive)
        return false;
    const vec3_t eye = to_vec3(state.eye_origin);
    const vec3_t forward = view_forward(engine_view_angles());
    constexpr float range = 8192.f;
    // windef.h still defines "far" as an empty macro, hence "destination".
    const vec3_t destination{ eye.x + forward.x * range, eye.y + forward.y * range, eye.z + forward.z * range };
    const vec3_t point{ 0.f, 0.f, 0.f };
    c_game_trace hit;
    if (!trace_line(eye, destination, point, point, hit))
        return false;
    if (hit.fraction >= 1.f)
        return true; // nothing in the way
    if ((hit.surface.flags & surf_nopentrate) != 0)
        return false;
    // Distance between the entry and the exit of the wall: the thickness.
    const vec3_t entry{ eye.x + (destination.x - eye.x) * hit.fraction,
        eye.y + (destination.y - eye.y) * hit.fraction, eye.z + (destination.z - eye.z) * hit.fraction };
    const vec3_t skip{ entry.x + forward.x, entry.y + forward.y, entry.z + forward.z };
    c_game_trace exit;
    if (!trace_line(skip, destination, point, point, exit))
        return false;
    const float thickness = exit.fraction * (1.f - hit.fraction) * range - 1.f;
    return thickness >= 0.f && thickness <= 45.f;
}

crosshair_style player_crosshair() {
    crosshair_style style{};
    style.size = std::clamp(convar_value("cl_crosshairsize", 5.f), 0.f, 20.f);
    style.thickness = std::clamp(convar_value("cl_crosshairthickness", 0.5f), 0.f, 10.f);
    style.gap = std::clamp(convar_value("cl_crosshairgap", 1.f), -20.f, 20.f);
    style.dot = convar_value("cl_crosshairdot", 0.f) >= 0.5f;
    style.valid = true;
    switch (static_cast<int>(convar_value("cl_crosshaircolor", 0.f))) {
    case 0: style.r = 0.f; style.g = 1.f; style.b = 0.f; break;
    case 1: style.r = 1.f; style.g = 1.f; style.b = 0.f; break;
    case 2: style.r = 0.f; style.g = 0.f; style.b = 1.f; break;
    case 3: style.r = 0.f; style.g = 1.f; style.b = 1.f; break;
    case 4: style.r = 1.f; style.g = 0.f; style.b = 1.f; break;
    default:
        style.r = std::clamp(convar_value("cl_crosshaircolor_r", 50.f), 0.f, 255.f) / 255.f;
        style.g = std::clamp(convar_value("cl_crosshaircolor_g", 250.f), 0.f, 255.f) / 255.f;
        style.b = std::clamp(convar_value("cl_crosshaircolor_b", 50.f), 0.f, 255.f) / 255.f;
        break;
    }
    return style;
}

// Panel filtering lives here so both renderers agree; the PaintTraverse hook
// itself is installed once in the D3D9 runtime (the old code installed a second
// hook from this file, which made the chain order dependent).
bool should_skip_panel(const char* name) {
    if (!name)
        return false;
    const auto& world = g_cfg->world;
    const auto& removals = player_visuals::world_settings().removals;
    // Exact names only. The old `strstr(name, "Panorama")` test matched whole
    // Panorama HUD panels, which also took the in-game team select down with it.
    const bool panorama_dots = std::strstr(name, "Panorama") != nullptr &&
        std::strstr(name, "Dots") != nullptr;
    if ((removals[0] && std::strcmp(name, "TeamIntro") == 0) ||
        (removals[1] && panorama_dots) ||
        (removals[2] && std::strstr(name, "BottomGradient") != nullptr) ||
        (removals[3] && std::strstr(name, "DamageIndicator") != nullptr) ||
        (removals[4] && (std::strstr(name, "ServerAdvertisement") != nullptr ||
            std::strstr(name, "ServerAd") != nullptr)))
        return true;
    if (world.m_remove_scope_overlay && std::strcmp(name, "HudZoom") == 0)
        return true;
    if (world.m_remove_death_notices && std::strcmp(name, "HudDeathNotice") == 0)
        return true;
    if (world.m_remove_scoreboard &&
        (std::strstr(name, "Scoreboard") != nullptr || std::strstr(name, "scoreboard") != nullptr))
        return true;
    return false;
}

bool initialize() {
    visuals_source::install_viewmodel_hook();
    if (!override_installed) {
        // ClientMode::OverrideView, main's OVERRIDE_VIEW_IDX.
        override_installed = hooks::detour_virtual(HACKS->client_mode, 18,
            reinterpret_cast<void*>(&hooked_override_view),
            reinterpret_cast<void**>(&original_override_view));
    }
    if (!convars[0].object)
        cache_convars();
    return override_installed;
}
} // namespace world_visuals
