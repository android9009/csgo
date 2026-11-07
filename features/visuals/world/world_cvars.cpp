#include "world.hpp"

#include "../../../sdk/config_system/config_system.hpp"
#include "../settings.hpp"
#include "../dependencies/source.hpp"

#include "../../../runtime/debug_console.hpp"
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {
struct vector3 {
    float x, y, z;
};

// CViewSetup layout used by the 32-bit CS:GO client.
struct view_setup {
    std::uint8_t pad[176];
    float fov;
    float viewmodel_fov;
    vector3 origin;
    vector3 angles;
};

// IInput layout as Fatality's sdk/interfaces/IInput.h has it: the pad is 0xA9
// bytes, so m_fCameraInThirdPerson sits at 0xA9 and m_vecCameraOffset at 0xAC.
constexpr std::size_t input_camera_in_third_person = 0xA9;
constexpr std::size_t convar_parent_offset = 0x1C;
constexpr std::size_t convar_float_offset = 0x2C;
constexpr std::size_t convar_find_var_vfunc = 16;
constexpr std::size_t convar_set_float_vfunc = 15;
constexpr std::size_t override_view_vfunc = 18;
constexpr std::size_t panel_get_name_vfunc = 36;
constexpr std::size_t panel_paint_traverse_vfunc = 41;

struct convar_state {
    const char* name;
    void* object = nullptr;
    float original = 0.f;
    bool original_valid = false;
    bool overridden = false;
    float last_applied = 0.f;
};

std::array<convar_state, 24> convars{{
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
    { "cl_ragdoll_physics_enable" },
    // Fatality's thirdperson drives the engine's own camera through these three:
    // the view distance, wall collision and the snap used by CInput::CAM_Think.
    { "cam_idealdist" },
    { "cam_collision" },
    { "cam_snapto" }
}};
convar_state tonemap_scale{ "mat_force_tonemap_scale" };

void* client_mode = nullptr;
void* input_object = nullptr;
void* cvar_interface = nullptr;
void* engine_client = nullptr;
void* entity_list = nullptr;
void* panel_interface = nullptr;
HMODULE client_module = nullptr;
int** smoke_count_global = nullptr;
bool smoke_pattern_resolved = false;
using override_view_t = void(__thiscall*)(void*, view_setup*);
override_view_t original_override_view = nullptr;
bool override_installed = false;

bool readable_range(const void* address, std::size_t size) {
    if (!address || !size)
        return false;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
        return false;
    }
    const auto begin = reinterpret_cast<std::uintptr_t>(address);
    const auto region_begin = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
    const auto region_end = region_begin + info.RegionSize;
    return begin >= region_begin && begin <= region_end && size <= region_end - begin;
}

void resolve_smoke_count() {
    if (!client_module || smoke_pattern_resolved)
        return;
    smoke_pattern_resolved = true;
    const auto* base = reinterpret_cast<const std::uint8_t*>(client_module);
    if (!readable_range(base, sizeof(IMAGE_DOS_HEADER)))
        return;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x100000)
        return;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (!readable_range(nt, sizeof(*nt)) || nt->Signature != IMAGE_NT_SIGNATURE)
        return;
    // The client smoke counter is the operand used by CSmokeScreenEffect::Render.
    // Search only executable code and require a unique version-specific signature.
    constexpr std::array<int, 15> pattern{
        0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0x8B, 0x15, -1, -1, -1, -1, 0x0F, 0x57, 0xC0
    };
    const std::uint8_t* match = nullptr;
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned int section_index = 0; section_index < nt->FileHeader.NumberOfSections;
         ++section_index, ++section) {
        if (std::memcmp(section->Name, ".text", 5) != 0)
            continue;
        const auto size = static_cast<std::size_t>(section->Misc.VirtualSize);
        const auto* text = base + section->VirtualAddress;
        if (size < pattern.size() || !readable_range(text, size))
            continue;
        for (std::size_t offset = 0; offset + pattern.size() <= size; ++offset) {
            bool matches = true;
            for (std::size_t index = 0; index < pattern.size(); ++index) {
                if (pattern[index] >= 0 && text[offset + index] != static_cast<std::uint8_t>(pattern[index])) {
                    matches = false;
                    break;
                }
            }
            if (!matches)
                continue;
            if (match)
                return; // ambiguous build signature: leave smoke untouched
            match = text + offset;
        }
    }
    if (!match || !readable_range(match + 8, sizeof(std::uintptr_t)))
        return;
    std::uintptr_t address = 0;
    std::memcpy(&address, match + 8, sizeof(address));
    auto** global = reinterpret_cast<int**>(address);
    if (!readable_range(global, sizeof(int*)))
        return;
    smoke_count_global = global;
}

void clear_smoke_counter() {
    if (!smoke_count_global || !readable_range(smoke_count_global, sizeof(int*)))
        return;
    int* counter = nullptr;
    std::memcpy(&counter, smoke_count_global, sizeof(counter));
    if (counter && readable_range(counter, sizeof(int)) &&
        visuals_source::writable_range(counter, sizeof(int))) {
        const int zero = 0;
        std::memcpy(counter, &zero, sizeof(zero));
    }
}

void* vfunc(void* object, std::size_t index) {
    if (!readable_range(object, sizeof(void*)))
        return nullptr;
    void** table = *reinterpret_cast<void***>(object);
    if (!readable_range(table, (index + 1) * sizeof(void*)))
        return nullptr;
    return table[index];
}

bool write_vfunc(void* object, std::size_t index, void* replacement, void** original) {
    void* target = vfunc(object, index);
    if (!target || !replacement || !original)
        return false;
    void** table = *reinterpret_cast<void***>(object);
    DWORD old_protect = 0;
    if (!VirtualProtect(&table[index], sizeof(void*), PAGE_READWRITE, &old_protect))
        return false;
    *original = table[index];
    table[index] = replacement;
    DWORD ignored = 0;
    VirtualProtect(&table[index], sizeof(void*), old_protect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), &table[index], sizeof(void*));
    return true;
}

float read_convar_float(void* object, bool& valid) {
    valid = false;
    if (!readable_range(object, convar_parent_offset + sizeof(void*)))
        return 0.f;
    void* parent = nullptr;
    std::memcpy(&parent, static_cast<const std::uint8_t*>(object) + convar_parent_offset,
        sizeof(parent));
    if (!readable_range(parent, convar_float_offset + sizeof(std::uint32_t)))
        return 0.f;
    std::uint32_t encoded = 0;
    std::memcpy(&encoded, static_cast<const std::uint8_t*>(parent) + convar_float_offset,
        sizeof(encoded));
    const auto self = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(object));
    const std::uint32_t decoded = encoded ^ self;
    float result = 0.f;
    std::memcpy(&result, &decoded, sizeof(result));
    valid = std::isfinite(result);
    return valid ? result : 0.f;
}

void set_convar_float(void* object, float value) {
    if (!object || !std::isfinite(value))
        return;
    const auto function = reinterpret_cast<void(__thiscall*)(void*, float)>(
        vfunc(object, convar_set_float_vfunc));
    if (function)
        function(object, value);
}

void* find_convar(const char* name) {
    if (!cvar_interface || !name)
        return nullptr;
    using find_var_t = void*(__thiscall*)(void*, const char*);
    const auto find = reinterpret_cast<find_var_t>(vfunc(cvar_interface, convar_find_var_vfunc));
    return find ? find(cvar_interface, name) : nullptr;
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
bool set_convar_float_raw(void* object, float value) {
    if (!object || !std::isfinite(value))
        return false;
    void* parent = nullptr;
    std::memcpy(&parent, static_cast<const std::uint8_t*>(object) + convar_parent_offset,
        sizeof(parent));
    if (!readable_range(parent, convar_float_offset + sizeof(std::uint32_t)))
        return false;
    // client.dll keeps plenty of read-only tables next to its .data; a wrong
    // parent pointer used to turn this into a write into .rdata (the crash we
    // kept hitting), so the cell has to be writable before it is touched.
    if (!visuals_source::writable_range(
            static_cast<const std::uint8_t*>(parent) + convar_float_offset,
            sizeof(std::uint32_t))) {
        return false;
    }
    const auto self = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(object));
    std::uint32_t encoded = 0;
    std::memcpy(&encoded, static_cast<const std::uint8_t*>(parent) + convar_float_offset,
        sizeof(encoded));
    std::uint32_t decoded = encoded ^ self;
    float current = 0.f;
    std::memcpy(&current, &decoded, sizeof(current));
    if (!std::isfinite(current))
        return false;
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    encoded = bits ^ self;
    std::memcpy(static_cast<std::uint8_t*>(parent) + convar_float_offset, &encoded,
        sizeof(encoded));
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

bool engine_in_game() {
    using bool_fn = bool(__thiscall*)(void*);
    const auto function = reinterpret_cast<bool_fn>(vfunc(engine_client, 26));
    return function && function(engine_client);
}

int local_player_index() {
    using int_fn = int(__thiscall*)(void*);
    const auto function = reinterpret_cast<int_fn>(vfunc(engine_client, 12));
    return function ? function(engine_client) : 0;
}

void update_world_cvars() {
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
    g_cfg->world.m_draw_radar = std::any_of(page.radar_options.begin(), page.radar_options.end(),
        [](bool value) { return value; });
    visuals_source::update_radar_reveal();
    g_cfg->world.m_custom_crosshair = page.force_sniper_crosshair || page.force_crosshair_scoped ||
        page.force_crosshair_c4 || page.penetration_crosshair;
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
    AIRFLOW_DEBUG_STAGE("world_cvars.flash");
    visuals_source::set_flash_removed(g_cfg->world.m_remove_flash);
    if (g_cfg->world.m_remove_smoke)
        clear_smoke_counter();

    AIRFLOW_DEBUG_STAGE("world_cvars.post_processing");
    const int exposure = std::clamp(world.m_exposure, 0, 100);
    const float brightness = std::clamp(world.m_brightness, 0.f, 1.f);
    const bool tone_changed = exposure != 20 || std::fabs(brightness - 0.5f) > 0.001f;
    const float tone_scale = std::clamp((static_cast<float>(exposure) / 20.f) *
        (brightness * 2.f), 0.f, 5.f);
    set_override("mat_force_tonemap_scale", tone_changed, tone_scale);

    AIRFLOW_DEBUG_STAGE("world_cvars.viewmodel_fov");
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

    AIRFLOW_DEBUG_STAGE("world_cvars.modifiers");
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

void __fastcall hooked_override_view(void* self, void*, view_setup* setup) {
    AIRFLOW_DEBUG_STAGE("world.override_view.camera");
    AIRFLOW_DEBUG_STAGE("world.override_view.cvars");
    update_world_cvars();
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
    if (cfg.m_enable_override_fov && visuals_source::writable_range(&setup->fov, sizeof(float)))
        setup->fov = static_cast<float>(std::clamp(cfg.m_override_fov, 60, 130));

    visuals_source::local_view_state view_state{};
    if (visuals_source::get_local_view_state(view_state)) {
        const auto& page = player_visuals::world_settings();
        // World > Visual Recoil follows its label: Enabled keeps the kick the
        // engine adds, Disabled is the one that subtracts it (the combo used to
        // be wired the other way round).
        if (view_state.alive && !g_cfg->world.m_visual_recoil &&
            visuals_source::writable_range(&setup->angles, sizeof(vector3))) {
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
            const int scope_fov = second_scope ? page.scope_fov_x2 : page.scope_fov_x1;
            // The menu exposes both scopes as a 0-100% scale, so the applied value follows it. 0 is
            // kept just above zero: a projection matrix built from a 0-degree FOV degenerates.
            setup->fov = static_cast<float>(std::clamp(scope_fov, 1, 100));
        } else if (view_state.scoped && cfg.m_zoom_fov > 0)
            setup->fov = static_cast<float>(std::clamp(cfg.m_zoom_fov, 20, 90));
        else if (view_state.scoped && cfg.m_skip_second_zoom && setup->fov < 20.f)
            setup->fov = 40.f;
    }
}

using panel_name_t = const char*(__thiscall*)(void*, unsigned int);
using paint_traverse_t = void(__thiscall*)(void*, unsigned int, bool, bool);

} // namespace

namespace world_visuals {
// Fatality's thirdperson (features/thirdperson.cpp::run): flip
// IInput::m_fCameraInThirdPerson and let the engine move the camera itself via
// cam_idealdist / cam_collision / cam_snapto. The old code wrote
// m_vecCameraOffset instead - that is the spectator/replay camera and never
// moved the first-person view, which is why the feature did nothing.
void run_thirdperson() {
    if (!input_object || !readable_range(input_object, input_camera_in_third_person + 1))
        return;
    auto* camera_flag = reinterpret_cast<bool*>(static_cast<std::uint8_t*>(input_object) +
        input_camera_in_third_person);
    if (!visuals_source::writable_range(camera_flag, 1))
        return;

    const auto& cfg = g_cfg->world_esp;
    const auto& page = player_visuals::world_settings();
    const bool in_game = engine_in_game() && local_player_index() > 0;
    const bool alive = visuals_source::local_player_alive();
    const bool active = cfg.m_enable_thirdperson && in_game && (alive || cfg.m_thirdperson_while_dead);

    if (active) {
        *camera_flag = true;
        // Distance is a float convar in the engine, so the slider value goes in raw.
        set_override_raw("cam_idealdist", true,
            std::clamp(static_cast<float>(cfg.m_distance), 50.f, 300.f));
        set_override_raw("cam_collision", true, page.thirdperson_close_to_wall ? 1.f : 0.f);
        set_override_raw("cam_snapto", true, 1.f);
    } else if (*camera_flag) {
        *camera_flag = false;
    }
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

bool initialize(void* mode, void* input, void* cvars, void* engine, void* entities,
                void* panels, void* client) {
    client_mode = mode;
    input_object = input;
    cvar_interface = cvars;
    engine_client = engine;
    entity_list = entities;
    panel_interface = panels;
    client_module = static_cast<HMODULE>(client);
    resolve_smoke_count();
    visuals_source::install_viewmodel_hook();
    if (!override_installed && client_mode && vfunc(client_mode, override_view_vfunc)) {
        override_installed = write_vfunc(client_mode, override_view_vfunc,
            reinterpret_cast<void*>(&hooked_override_view),
            reinterpret_cast<void**>(&original_override_view));
    }
    if (!convars[0].object && cvar_interface)
        cache_convars();
    return override_installed;
}
} // namespace world_visuals
