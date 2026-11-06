#include "world.hpp"

#include "../../sdk/config_system/config_system.hpp"

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

constexpr std::size_t input_camera_in_third_person = 0xA9;
constexpr std::size_t input_camera_offset = 0xAC;
constexpr std::size_t convar_parent_offset = 0x1C;
constexpr std::size_t convar_float_offset = 0x2C;
constexpr std::size_t convar_find_var_vfunc = 16;
constexpr std::size_t convar_set_float_vfunc = 15;
constexpr std::size_t override_view_vfunc = 18;

struct convar_state {
    const char* name;
    void* object = nullptr;
    float original = 0.f;
    bool original_valid = false;
    bool overridden = false;
    float last_applied = 0.f;
};

std::array<convar_state, 13> convars{{
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
    { "fog_maxdensity" }
}};
convar_state tonemap_scale{ "mat_force_tonemap_scale" };

void* client_mode = nullptr;
void* input_object = nullptr;
void* cvar_interface = nullptr;
void* engine_client = nullptr;
void* entity_list = nullptr;
using override_view_t = void(__thiscall*)(void*, view_setup*);
override_view_t original_override_view = nullptr;
bool override_installed = false;
bool camera_owned = false;
bool previous_camera_state = false;
vector3 previous_camera_offset{};

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
    for (auto& item : convars)
        if (std::strcmp(item.name, name) == 0)
            return &item;
    return std::strcmp(tonemap_scale.name, name) == 0 ? &tonemap_scale : nullptr;
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

void update_camera() {
    if (!readable_range(input_object, input_camera_offset + sizeof(vector3)))
        return;

    const auto& cfg = g_cfg->world_esp;
    const int local_index = local_player_index();
    using entity_at_t = void*(__thiscall*)(void*, int);
    const auto entity_at = reinterpret_cast<entity_at_t>(vfunc(entity_list, 3));
    const bool has_local_player = local_index > 0 && entity_at && entity_at(entity_list, local_index);
    const bool active = cfg.m_enable_thirdperson && engine_in_game() && has_local_player;
    auto* bytes = static_cast<std::uint8_t*>(input_object);
    auto* camera_flag = reinterpret_cast<bool*>(bytes + input_camera_in_third_person);
    auto* camera_offset = reinterpret_cast<vector3*>(bytes + input_camera_offset);

    if (active) {
        if (!camera_owned) {
            previous_camera_state = *camera_flag;
            previous_camera_offset = *camera_offset;
            camera_owned = true;
        }
        using get_angles_t = void(__thiscall*)(void*, vector3&);
        const auto get_angles = reinterpret_cast<get_angles_t>(vfunc(engine_client, 18));
        vector3 angles{};
        if (get_angles)
            get_angles(engine_client, angles);
        angles.z = static_cast<float>(std::clamp(cfg.m_distance, 50, 300));
        *camera_offset = angles;
        *camera_flag = true;
    } else if (camera_owned) {
        *camera_flag = previous_camera_state;
        *camera_offset = previous_camera_offset;
        camera_owned = false;
    }
}

void update_world_cvars() {
    const auto& world = g_cfg->world;
    const bool fog = world.m_render_fog;
    set_override("fog_override", fog, fog ? 1.f : 0.f);
    set_override("fog_maxdensity", fog, 0.f);

    const int exposure = std::clamp(world.m_exposure, 0, 100);
    const float brightness = std::clamp(world.m_brightness, 0.f, 1.f);
    const bool tone_changed = exposure != 20 || std::fabs(brightness - 0.5f) > 0.001f;
    const float tone_scale = std::clamp((static_cast<float>(exposure) / 20.f) *
        (brightness * 2.f), 0.f, 5.f);
    set_override("mat_force_tonemap_scale", tone_changed, tone_scale);

    const int vm_fov = std::clamp(world.m_viewmodel_fov, 0, 30);
    set_override("viewmodel_fov", vm_fov != 0,
        convars[0].original + static_cast<float>(vm_fov));
    set_override("viewmodel_offset_x", std::fabs(world.m_viewmodel_offset_x) > 0.001f,
        convars[1].original + std::clamp(world.m_viewmodel_offset_x, -2.5f, 2.5f));
    set_override("viewmodel_offset_y", std::fabs(world.m_viewmodel_offset_y) > 0.001f,
        convars[2].original + std::clamp(world.m_viewmodel_offset_y, -2.f, 2.f));
    set_override("viewmodel_offset_z", std::fabs(world.m_viewmodel_offset_z) > 0.001f,
        convars[3].original + std::clamp(world.m_viewmodel_offset_z, -2.f, 2.f));

    const bool remove_move = world.m_remove_viewmodel_move;
    set_override("cl_wpn_sway_scale", remove_move, 0.f);
    set_override("cl_wpn_sway_interp", remove_move, 0.f);
    set_override("cl_bobamt_lat", remove_move, 0.f);
    set_override("cl_bobamt_vert", remove_move, 0.f);
    set_override("cl_bob_lower_amt", remove_move, 0.f);
    const bool no_landing_bob = world.m_remove_landing_bob;
    set_override("cl_viewmodel_shift_left_amt", no_landing_bob, 0.f);
    set_override("cl_viewmodel_shift_right_amt", no_landing_bob, 0.f);
}

void __fastcall hooked_override_view(void* self, void*, view_setup* setup) {
    update_camera();
    update_world_cvars();
    if (original_override_view)
        original_override_view(self, setup);
    if (!setup)
        return;

    const auto& cfg = g_cfg->world_esp;
    if (cfg.m_enable_override_fov)
        setup->fov = static_cast<float>(std::clamp(cfg.m_override_fov, 60, 130));
}
} // namespace

namespace world_visuals {
bool initialize(void* mode, void* input, void* cvars, void* engine, void* entities) {
    client_mode = mode;
    input_object = input;
    cvar_interface = cvars;
    engine_client = engine;
    entity_list = entities;
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
