#include "source.hpp"
#include "settings.hpp"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <utility>

namespace {
static_assert(sizeof(void*) == 4, "Source 1 player visuals require the x86 ABI");

using visuals_source::line3;
using visuals_source::player_data;
using visuals_source::world_data;
using visuals_source::renderable_entity_data;
using visuals_source::vector3;

struct matrix3x4_t {
    float m[3][4];
};

struct quaternion_t {
    float x, y, z, w;
};

struct recv_prop_t;
struct recv_table_t {
    recv_prop_t* props;
    int prop_count;
    void* decoder;
    char* name;
    bool initialized;
    bool in_main_list;
};

struct recv_prop_t {
    char* name;
    int type;
    int flags;
    int buffer_size;
    int inside_array;
    const void* extra_data;
    recv_prop_t* array_prop;
    void* array_length_proxy;
    void* proxy_fn;
    void* data_table_proxy_fn;
    recv_table_t* data_table;
    int offset;
    int element_stride;
    int elements;
    const char* parent_array_prop_name;
};

struct client_class_t {
    void* create_fn;
    void* create_event_fn;
    char* name;
    recv_table_t* table;
    client_class_t* next;
    int class_id;
};

struct global_vars_t {
    float real_time;
    int frame_count;
    float absolute_frame_time;
    float absolute_frame_start_stddev;
    float current_time;
    float frame_time;
    int max_clients;
    int tick_count;
    float interval_per_tick;
};

// CCSWeaponData prefix from the Source 1 CS:GO layout. Only these two fields are
// consumed; keeping the structure short avoids depending on the rest of the schema.
struct weapon_info_t {
    unsigned char pad_0000[20];
    int max_clip1;                 // 0x14
    unsigned char pad_0018[192];
    float cycle_time;              // 0xD8
};
static_assert(offsetof(weapon_info_t, max_clip1) == 0x14);
static_assert(offsetof(weapon_info_t, cycle_time) == 0xD8);

struct studio_hdr_t;

struct studiohdr_t {
    int id;
    int version;
    int checksum;
    char name[64];
    int length;
    vector3 eye_position;
    vector3 illumination_position;
    vector3 hull_min;
    vector3 hull_max;
    vector3 bb_min;
    vector3 bb_max;
    int flags;
    int bone_count;
    int bone_index;
    int bone_controller_count;
    int bone_controller_index;
    int hitbox_set_count;
    int hitbox_set_index;
};

struct mstudiobone_t {
    int name_index;
    int parent;
    int bone_controller[6];
    vector3 position;
    quaternion_t rotation_quaternion;
    vector3 rotation;
    vector3 position_scale;
    vector3 rotation_scale;
    matrix3x4_t pose_to_bone;
    quaternion_t alignment;
    int flags;
    int procedure_type;
    int procedure_index;
    int physics_bone;
    int surface_prop_index;
    int contents;
    int surface_prop_lookup;
    int unused[7];
};
static_assert(sizeof(mstudiobone_t) == 216);

struct mstudiobbox_t {
    int bone;
    int group;
    vector3 mins;
    vector3 maxs;
    int name_index;
    vector3 rotation;
    float radius;
    unsigned char padding[16];
};
static_assert(sizeof(mstudiobbox_t) == 68);

struct mstudiohitboxset_t {
    int name_index;
    int hitbox_count;
    int hitbox_index;

    const mstudiobbox_t* hitbox(int index) const {
        return reinterpret_cast<const mstudiobbox_t*>(
            reinterpret_cast<const unsigned char*>(this) + hitbox_index) + index;
    }
};

struct studio_hdr_t {
    studiohdr_t* studio;
};

struct netvars_t {
    int team = -1;
    int health = -1;
    int life_state = -1;
    int origin = -1;
    int view_offset = -1;
    int aim_punch = -1;
    int mins = -1;
    int maxs = -1;
    int simulation_time = -1;
    int velocity_modifier = -1;
    int active_weapon = -1;
    int my_weapons = -1;
    int armor = -1;
    int helmet = -1;
    int defuser = -1;
    int defusing = -1;
    int scoped = -1;
    int immunity = -1;
    int flash_duration = -1;
    int money = -1;
    int carried_hostage = -1;
    int hitbox_set = -1;
    int force_bone = -1;
    int move_parent = -1;
    int weapon_id = -1;
    int clip = -1;
    int reserve_ammo = -1;
    int next_primary_attack = -1;
    int postpone_fire_ready = -1;
    int reloading = -1;
    int pin_pulled = -1;
    int owner = -1;
    int smoke_effect = -1;
    int smoke_tick = -1;
    int explode_tick = -1;
    int fire_count = -1;
    int fire_burning = -1;
    int fire_x = -1;
    int fire_y = -1;
    int fire_z = -1;
};

void* client = nullptr;
void* engine = nullptr;
void* entity_list = nullptr;
void* weapon_system = nullptr;
global_vars_t* global_vars = nullptr;
HMODULE client_module = nullptr;
HMODULE engine_module = nullptr;
netvars_t nv;
std::size_t studio_hdr_offset = 0;
std::atomic<bool> core_ready = false;
std::atomic<ULONGLONG> next_initialize_attempt = 0;
std::mutex initialize_mutex;

// DrawModelExecute is the only place where the engine hands us the exact
// bones used for the visible model. Keep just that render-frame snapshot; it
// is not a dormant/history cache and is never intentionally carried to a later
// engine frame.
struct rendered_player_snapshot {
    vector3 origin;
    alignas(16) matrix3x4_t bones[256] = {};
    ULONGLONG time = 0;
    int frame = -1;
    bool valid = false;
    bool bones_valid = false;
};
rendered_player_snapshot rendered_players[65];
std::mutex rendered_players_mutex;

int snapshot_frame() {
    const int frame = global_vars ? global_vars->frame_count : 0;
    // A time token is only a fallback for a missing global-vars pointer. It
    // still prevents a single DME record from becoming an unbounded cache.
    return frame > 0 ? frame : static_cast<int>(GetTickCount64() / 16);
}

bool readable_range(const void* address, std::size_t size) {
    if (!address || !size)
        return false;
    auto current = reinterpret_cast<std::uintptr_t>(address);
    const auto end = current + size;
    if (end < current)
        return false;
    while (current < end) {
        MEMORY_BASIC_INFORMATION information{};
        if (!VirtualQuery(reinterpret_cast<const void*>(current), &information,
                          sizeof(information)) || information.State != MEM_COMMIT ||
            (information.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
            return false;
        }
        const auto begin = reinterpret_cast<std::uintptr_t>(information.BaseAddress);
        const auto region_end = begin + information.RegionSize;
        if (region_end <= begin || current < begin || current >= region_end)
            return false;
        current = std::min<std::uintptr_t>(end, region_end);
    }
    return true;
}

template <typename Fn>
Fn virtual_function(void* instance, std::size_t index) {
    if (!readable_range(instance, sizeof(void*)))
        return nullptr;
    void** table = *reinterpret_cast<void***>(instance);
    if (!readable_range(table, (index + 1) * sizeof(void*)))
        return nullptr;
    return reinterpret_cast<Fn>(table[index]);
}

void* create_interface(HMODULE module, const char* name) {
    if (!module)
        return nullptr;
    using create_interface_t = void* (__cdecl*)(const char*, int*);
    const auto factory = reinterpret_cast<create_interface_t>(
        GetProcAddress(module, "CreateInterface"));
    return factory ? factory(name, nullptr) : nullptr;
}

bool module_text(HMODULE module, const std::uint8_t*& text, std::size_t& size) {
    if (!module)
        return false;
    const auto* base = reinterpret_cast<const std::uint8_t*>(module);
    if (!readable_range(base, sizeof(IMAGE_DOS_HEADER)))
        return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE ||
        !readable_range(base + dos->e_lfanew, sizeof(IMAGE_NT_HEADERS))) {
        return false;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return false;
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (std::memcmp(section->Name, ".text", 5) != 0)
            continue;
        const auto virtual_size = static_cast<std::size_t>(section->Misc.VirtualSize);
        const auto raw_size = static_cast<std::size_t>(section->SizeOfRawData);
        size = virtual_size && raw_size ? std::min(virtual_size, raw_size)
                                        : std::max(virtual_size, raw_size);
        text = base + section->VirtualAddress;
        return text && size;
    }
    return false;
}

bool function_in_module(HMODULE module, const void* function) {
    const std::uint8_t* text = nullptr;
    std::size_t size = 0;
    if (!module_text(module, text, size) || !function)
        return false;
    const auto address = reinterpret_cast<std::uintptr_t>(function);
    const auto begin = reinterpret_cast<std::uintptr_t>(text);
    return address >= begin && address - begin < size && readable_range(function, 1);
}

template <typename Fn>
Fn checked_virtual(void* instance, std::size_t index, HMODULE module) {
    const auto function = virtual_function<Fn>(instance, index);
    return function_in_module(module, reinterpret_cast<const void*>(function)) ? function : nullptr;
}

template <std::size_t N>
const std::uint8_t* find_pattern(HMODULE module, const int (&pattern)[N]) {
    const std::uint8_t* text = nullptr;
    std::size_t size = 0;
    if (!module_text(module, text, size) || N > size)
        return nullptr;
    for (std::size_t offset = 0; offset + N <= size; ++offset) {
        bool matches = true;
        for (std::size_t i = 0; i < N; ++i) {
            if (pattern[i] >= 0 && text[offset + i] != static_cast<std::uint8_t>(pattern[i])) {
                matches = false;
                break;
            }
        }
        if (matches && readable_range(text + offset, N))
            return text + offset;
    }
    return nullptr;
}

template <typename T>
T* resolve_absolute_pointer(const std::uint8_t* operand) {
    if (!operand)
        return nullptr;
    std::uintptr_t address = 0;
    std::memcpy(&address, operand, sizeof(address));
    if (!readable_range(reinterpret_cast<const void*>(address), sizeof(void*)))
        return nullptr;
    T* result = nullptr;
    std::memcpy(&result, reinterpret_cast<const void*>(address), sizeof(result));
    return result;
}

int find_offset(const recv_table_t* table, const char* name, int depth = 0) {
    if (!table || depth > 24 || !readable_range(table, sizeof(recv_table_t)) ||
        table->prop_count < 0 || table->prop_count > 4096 ||
        !readable_range(table->props, static_cast<std::size_t>(table->prop_count) * sizeof(recv_prop_t))) {
        return -1;
    }
    for (int i = 0; i < table->prop_count; ++i) {
        const auto& prop = table->props[i];
        if (readable_range(prop.name, 1) && std::strcmp(prop.name, name) == 0)
            return prop.offset;
    }
    for (int i = 0; i < table->prop_count; ++i) {
        const auto& prop = table->props[i];
        if (!prop.data_table)
            continue;
        const int child = find_offset(prop.data_table, name, depth + 1);
        if (child >= 0)
            return prop.offset + child;
    }
    return -1;
}

int table_offset(client_class_t* classes, std::initializer_list<const char*> tables,
                 const char* property) {
    for (const char* table_name : tables) {
        for (auto* current = classes; current; current = current->next) {
            if (!readable_range(current, sizeof(client_class_t)))
                break;
            if (!readable_range(current->table, sizeof(recv_table_t)) ||
                !readable_range(current->table->name, 1) ||
                std::strcmp(current->table->name, table_name) != 0) {
                continue;
            }
            const int result = find_offset(current->table, property);
            if (result >= 0 && result < 0x10000)
                return result;
        }
    }
    return -1;
}

using get_all_classes_t = client_class_t* (__thiscall*)(void*);

void resolve_netvars() {
    const auto get_classes = virtual_function<get_all_classes_t>(client, 8);
    auto* classes = get_classes ? get_classes(client) : nullptr;
    if (!classes)
        return;
    nv.team = table_offset(classes, { "DT_CSPlayer", "DT_BaseEntity" }, "m_iTeamNum");
    nv.health = table_offset(classes, { "DT_CSPlayer", "DT_BasePlayer" }, "m_iHealth");
    nv.life_state = table_offset(classes, { "DT_CSPlayer", "DT_BasePlayer" }, "m_lifeState");
    nv.origin = table_offset(classes, { "DT_CSPlayer", "DT_BaseEntity" }, "m_vecOrigin");
    nv.view_offset = table_offset(classes, { "DT_CSPlayer", "DT_BasePlayer" }, "m_vecViewOffset[0]");
    if (nv.view_offset < 0)
        nv.view_offset = table_offset(classes, { "DT_CSPlayer", "DT_BasePlayer" }, "m_vecViewOffset");
    nv.aim_punch = table_offset(classes, { "DT_CSPlayer", "DT_BasePlayer" }, "m_aimPunchAngle");
    nv.mins = table_offset(classes, { "DT_CSPlayer", "DT_BaseEntity" }, "m_vecMins");
    nv.maxs = table_offset(classes, { "DT_CSPlayer", "DT_BaseEntity" }, "m_vecMaxs");
    nv.simulation_time = table_offset(classes, { "DT_CSPlayer", "DT_BaseEntity" }, "m_flSimulationTime");
    nv.velocity_modifier = table_offset(classes, { "DT_CSPlayer" }, "m_flVelocityModifier");
    nv.active_weapon = table_offset(classes, { "DT_CSPlayer", "DT_BaseCombatCharacter", "DT_BasePlayer" }, "m_hActiveWeapon");
    nv.my_weapons = table_offset(classes, { "DT_CSPlayer", "DT_BaseCombatCharacter" }, "m_hMyWeapons");
    if (nv.my_weapons < 0)
        nv.my_weapons = table_offset(classes, { "DT_CSPlayer", "DT_BaseCombatCharacter" }, "m_hMyWeapons[0]");
    nv.armor = table_offset(classes, { "DT_CSPlayer" }, "m_ArmorValue");
    nv.helmet = table_offset(classes, { "DT_CSPlayer" }, "m_bHasHelmet");
    nv.defuser = table_offset(classes, { "DT_CSPlayer" }, "m_bHasDefuser");
    nv.defusing = table_offset(classes, { "DT_CSPlayer" }, "m_bIsDefusing");
    nv.scoped = table_offset(classes, { "DT_CSPlayer" }, "m_bIsScoped");
    nv.immunity = table_offset(classes, { "DT_CSPlayer" }, "m_bGunGameImmunity");
    nv.flash_duration = table_offset(classes, { "DT_CSPlayer" }, "m_flFlashDuration");
    nv.money = table_offset(classes, { "DT_CSPlayer" }, "m_iAccount");
    nv.carried_hostage = table_offset(classes, { "DT_CSPlayer" }, "m_hCarriedHostage");
    if (nv.carried_hostage < 0)
        nv.carried_hostage = table_offset(classes, { "DT_CSPlayer" }, "m_hCarriedHostageProp");
    nv.hitbox_set = table_offset(classes, { "DT_CSPlayer", "DT_BaseAnimating" }, "m_nHitboxSet");
    nv.force_bone = table_offset(classes, { "DT_CSPlayer", "DT_BaseAnimating" }, "m_nForceBone");
    nv.move_parent = table_offset(classes, { "DT_BaseEntity" }, "m_hMoveParent");
    if (nv.move_parent < 0)
        nv.move_parent = table_offset(classes, { "DT_BaseEntity" }, "moveparent");
    nv.weapon_id = table_offset(classes, { "DT_BaseAttributableItem" }, "m_iItemDefinitionIndex");
    nv.clip = table_offset(classes, { "DT_BaseCombatWeapon" }, "m_iClip1");
    nv.reserve_ammo = table_offset(classes, { "DT_BaseCombatWeapon" }, "m_iPrimaryReserveAmmoCount");
    nv.next_primary_attack = table_offset(classes, { "DT_BaseCombatWeapon" }, "m_flNextPrimaryAttack");
    nv.postpone_fire_ready = table_offset(classes, { "DT_WeaponCSBase" }, "m_flPostponeFireReadyTime");
    nv.reloading = table_offset(classes, { "DT_WeaponCSBase", "DT_BaseCombatWeapon" }, "m_bInReload");
    nv.pin_pulled = table_offset(classes, { "DT_BaseCSGrenade" }, "m_bPinPulled");
    nv.owner = table_offset(classes, { "DT_BaseEntity" }, "m_hOwnerEntity");
    nv.smoke_effect = table_offset(classes, { "DT_SmokeGrenadeProjectile" }, "m_bDidSmokeEffect");
    nv.smoke_tick = table_offset(classes, { "DT_SmokeGrenadeProjectile" }, "m_nSmokeEffectTickBegin");
    nv.explode_tick = table_offset(classes, { "DT_BaseCSGrenadeProjectile" }, "m_nExplodeEffectTickBegin");
    nv.fire_count = table_offset(classes, { "DT_Inferno" }, "m_fireCount");
    nv.fire_burning = table_offset(classes, { "DT_Inferno" }, "m_bFireIsBurning");
    nv.fire_x = table_offset(classes, { "DT_Inferno" }, "m_fireXDelta");
    nv.fire_y = table_offset(classes, { "DT_Inferno" }, "m_fireYDelta");
    nv.fire_z = table_offset(classes, { "DT_Inferno" }, "m_fireZDelta");
}

template <typename T>
T value(const void* entity, int offset, T fallback = T{}) {
    if (!entity || offset < 0)
        return fallback;
    T result{};
    std::memcpy(&result, reinterpret_cast<const unsigned char*>(entity) + offset, sizeof(T));
    return result;
}

using entity_at_t = void* (__thiscall*)(void*, int);
using entity_from_handle_t = void* (__thiscall*)(void*, unsigned int);
using local_player_t = int(__thiscall*)(void*);
using engine_bool_t = bool(__thiscall*)(void*);
using get_player_info_t = bool(__thiscall*)(void*, int, void*);
using get_view_angles_t = void (__thiscall*)(void*, vector3&);
using world_matrix_t = const float* (__thiscall*)(void*);
using render_origin_t = const vector3& (__thiscall*)(void*);
using entity_void_t = void* (__thiscall*)(void*);
using get_client_class_t = client_class_t* (__thiscall*)(void*);
using setup_bones_t = bool(__thiscall*)(void*, matrix3x4_t*, int, int, float);
using dormant_t = bool(__thiscall*)(void*);
using weapon_float_t = float(__thiscall*)(void*);
using get_weapon_data_t = weapon_info_t* (__thiscall*)(void*, unsigned int);

void* entity_at(int index) {
    if (index < 0)
        return nullptr;
    const auto function = virtual_function<entity_at_t>(entity_list, 3);
    return function ? function(entity_list, index) : nullptr;
}

int local_index() {
    const auto function = virtual_function<local_player_t>(engine, 12);
    return function ? function(engine) : 0;
}

vector3 view_angles() {
    vector3 result{};
    const auto function = virtual_function<get_view_angles_t>(engine, 18);
    if (function && function_in_module(engine_module, reinterpret_cast<const void*>(function)))
        function(engine, result);
    return result;
}

void* entity_from_handle(unsigned int handle) {
    if (handle == 0 || handle == 0xFFFFFFFFu)
        return nullptr;
    const auto function = virtual_function<entity_from_handle_t>(entity_list, 4);
    return function ? function(entity_list, handle) : nullptr;
}

bool dormant(void* entity) {
    void* networkable = reinterpret_cast<unsigned char*>(entity) + 8;
    const auto function = virtual_function<dormant_t>(networkable, 9);
    return !function || function(networkable);
}

void* client_renderable(void* entity) {
    // IClientEntity's renderable subobject is immediately after the primary
    // vtable pointer on the Source 1 x86 entity layout.
    return entity ? reinterpret_cast<unsigned char*>(entity) + 4 : nullptr;
}

vector3 render_origin(void* entity, const vector3& fallback) {
    void* renderable = client_renderable(entity);
    const auto function = virtual_function<render_origin_t>(renderable, 1);
    if (!function || !function_in_module(client_module, reinterpret_cast<const void*>(function)))
        return fallback;

    // GetRenderOrigin is the interpolated position used by the model renderer;
    // m_vecOrigin can belong to a different network/interpolation snapshot.
    const auto& value = function(renderable);
    if (!readable_range(&value, sizeof(vector3)))
        return fallback;
    const vector3 result = value;
    return std::isfinite(result.x) && std::isfinite(result.y) && std::isfinite(result.z)
        ? result : fallback;
}

bool current_rendered_snapshot(const rendered_player_snapshot& snapshot, int frame,
                               ULONGLONG now) {
    // The age guard also protects a map/restart transition where frame_count
    // can start over while an old index still happens to match.
    return snapshot.valid && snapshot.frame == frame && now >= snapshot.time &&
        now - snapshot.time <= 100;
}

bool read_rendered_origin(int index, vector3& output) {
    if (index < 1 || index > 64)
        return false;
    const int frame = snapshot_frame();
    const ULONGLONG now = GetTickCount64();
    std::lock_guard<std::mutex> lock(rendered_players_mutex);
    const auto& snapshot = rendered_players[index];
    if (!current_rendered_snapshot(snapshot, frame, now))
        return false;
    output = snapshot.origin;
    return std::isfinite(output.x) && std::isfinite(output.y) && std::isfinite(output.z);
}

bool read_rendered_bones(int index, matrix3x4_t* output) {
    if (!output || index < 1 || index > 64)
        return false;
    const int frame = snapshot_frame();
    const ULONGLONG now = GetTickCount64();
    std::lock_guard<std::mutex> lock(rendered_players_mutex);
    const auto& snapshot = rendered_players[index];
    if (!snapshot.bones_valid || !current_rendered_snapshot(snapshot, frame, now))
        return false;
    std::memcpy(output, snapshot.bones, sizeof(snapshot.bones));
    return true;
}

void clear_rendered_players() {
    std::lock_guard<std::mutex> lock(rendered_players_mutex);
    for (auto& snapshot : rendered_players)
        snapshot.valid = false;
}

vector3 current_player_origin(int index, void* entity) {
    vector3 rendered;
    if (read_rendered_origin(index, rendered))
        return rendered;
    return render_origin(entity, value<vector3>(entity, nv.origin, {}));
}

client_class_t* entity_class(void* entity) {
    if (!entity)
        return nullptr;
    void* networkable = reinterpret_cast<unsigned char*>(entity) + 8;
    const auto get_class = virtual_function<get_client_class_t>(networkable, 2);
    if (!get_class || !function_in_module(client_module, reinterpret_cast<const void*>(get_class)))
        return nullptr;
    auto* client_class = get_class(networkable);
    return readable_range(client_class, sizeof(client_class_t)) ? client_class : nullptr;
}

bool renderable_entity(void* renderable, renderable_entity_data& output) {
    output = {};
    if (!renderable || !client_module)
        return false;
    const auto get_unknown = virtual_function<entity_void_t>(renderable, 0);
    if (!get_unknown || !function_in_module(client_module, reinterpret_cast<const void*>(get_unknown)))
        return false;
    void* unknown = get_unknown(renderable);
    if (!unknown)
        return false;
    // IClientUnknown inherits IHandleEntity's destructor: networkable=4,
    // base entity=7. IClientNetworkable::GetClientClass is slot 2.
    const auto get_networkable = virtual_function<entity_void_t>(unknown, 4);
    const auto get_base_entity = virtual_function<entity_void_t>(unknown, 7);
    if (!get_networkable || !get_base_entity ||
        !function_in_module(client_module, reinterpret_cast<const void*>(get_networkable)) ||
        !function_in_module(client_module, reinterpret_cast<const void*>(get_base_entity))) {
        return false;
    }
    void* networkable = get_networkable(unknown);
    void* entity = get_base_entity(unknown);
    if (!networkable || !entity)
        return false;
    const auto get_class = virtual_function<get_client_class_t>(networkable, 2);
    if (!get_class || !function_in_module(client_module, reinterpret_cast<const void*>(get_class)))
        return false;
    const auto* client_class = get_class(networkable);
    if (!readable_range(client_class, sizeof(client_class_t)))
        return false;
    output.entity = entity;
    output.class_id = client_class->class_id;
    if (readable_range(client_class->name, 1)) {
        output.is_player = std::strstr(client_class->name, "CSPlayer") != nullptr;
        output.viewmodel = std::strstr(client_class->name, "ViewModel") != nullptr;
        output.ragdoll = std::strstr(client_class->name, "CSRagdoll") != nullptr;
    }
    return true;
}

int item_definition(void* weapon) {
    return value<unsigned short>(weapon, nv.weapon_id, 0);
}

const char* weapon_name(int id) {
    switch (id) {
    case 1: return "Desert Eagle";
    case 2: return "Dual Berettas";
    case 3: return "Five-SeveN";
    case 4: return "Glock-18";
    case 7: return "AK-47";
    case 8: return "AUG";
    case 9: return "AWP";
    case 10: return "FAMAS";
    case 11: return "G3SG1";
    case 13: return "Galil AR";
    case 14: return "M249";
    case 16: return "M4A4";
    case 17: return "MAC-10";
    case 19: return "P90";
    case 23: return "MP5-SD";
    case 24: return "UMP-45";
    case 25: return "XM1014";
    case 26: return "PP-Bizon";
    case 27: return "MAG-7";
    case 28: return "Negev";
    case 29: return "Sawed-Off";
    case 30: return "Tec-9";
    case 31: return "Zeus x27";
    case 32: return "P2000";
    case 33: return "MP7";
    case 34: return "MP9";
    case 35: return "Nova";
    case 36: return "P250";
    case 37: return "Shield";
    case 38: return "SCAR-20";
    case 39: return "SG 553";
    case 40: return "SSG 08";
    case 41: case 42: case 59: return "Knife";
    case 43: return "Flashbang";
    case 44: return "HE Grenade";
    case 45: return "Smoke Grenade";
    case 46: return "Molotov";
    case 47: return "Decoy";
    case 48: return "Incendiary";
    case 49: return "C4";
    case 57: return "Healthshot";
    case 60: return "M4A1-S";
    case 61: return "USP-S";
    case 63: return "CZ75-Auto";
    case 64: return "R8 Revolver";
    case 68: return "Tactical Grenade";
    case 69: return "Fists";
    case 70: return "Breach Charge";
    case 72: return "Tablet";
    case 74: case 75: case 76: case 78: return "Melee";
    default: return id >= 500 ? "Knife" : "Weapon";
    }
}

int fallback_max_clip(int id) {
    switch (id) {
    case 1: return 7;                       // Desert Eagle
    case 2: case 7: case 8: case 16:        // Duals, AK, AUG, M4A4
    case 17: case 23: case 33: case 34:     // MAC-10, MP5, MP7, MP9
        return 30;
    case 3: case 4: return 20;              // Five-SeveN, Glock
    case 9: case 27: return 5;              // AWP, MAG-7
    case 10: case 24: return 25;            // FAMAS, UMP-45
    case 11: case 38: return 20;            // Autosnipers
    case 13: return 35;                     // Galil AR
    case 14: return 100;                    // M249
    case 19: return 50;                     // P90
    case 25: case 29: return 7;             // XM1014, Sawed-Off
    case 26: return 64;                     // PP-Bizon
    case 28: return 150;                    // Negev
    case 30: return 24;                     // Tec-9
    case 32: case 36: return 13;            // P2000, P250
    case 35: return 8;                      // Nova
    case 39: return 30;                     // SG 553
    case 40: return 10;                     // SSG 08
    case 60: return 20;                     // M4A1-S
    case 61: case 63: return 12;            // USP-S, CZ75-Auto
    case 64: return 8;                      // R8 Revolver
    default: return 0;
    }
}

weapon_info_t* weapon_info(int id) {
    if (!weapon_system || id <= 0)
        return nullptr;
    const auto function = virtual_function<get_weapon_data_t>(weapon_system, 2);
    if (!function || !function_in_module(client_module, reinterpret_cast<const void*>(function)))
        return nullptr;
    auto* result = function(weapon_system, static_cast<unsigned int>(id));
    return readable_range(result, sizeof(weapon_info_t)) ? result : nullptr;
}

float vector_length(const vector3& value) {
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

vector3 transform(const vector3& point, const matrix3x4_t& matrix) {
    return {
        point.x * matrix.m[0][0] + point.y * matrix.m[0][1] + point.z * matrix.m[0][2] + matrix.m[0][3],
        point.x * matrix.m[1][0] + point.y * matrix.m[1][1] + point.z * matrix.m[1][2] + matrix.m[1][3],
        point.x * matrix.m[2][0] + point.y * matrix.m[2][1] + point.z * matrix.m[2][2] + matrix.m[2][3]
    };
}

vector3 bone_origin(const matrix3x4_t& matrix) {
    return { matrix.m[0][3], matrix.m[1][3], matrix.m[2][3] };
}

void collect_model_geometry(void* entity, int hitbox_set_index, player_data& output,
                            bool collect_skeleton, bool collect_hitboxes) {
    if ((!collect_skeleton && !collect_hitboxes) || !studio_hdr_offset || nv.force_bone < 0)
        return;
    const auto* studio_field = reinterpret_cast<const unsigned char*>(entity) + studio_hdr_offset;
    if (!readable_range(studio_field, sizeof(void*)))
        return;
    const auto* studio_wrapper = *reinterpret_cast<studio_hdr_t* const*>(studio_field);
    if (!readable_range(studio_wrapper, sizeof(studio_hdr_t)) ||
        !readable_range(studio_wrapper->studio, sizeof(studiohdr_t))) {
        return;
    }
    const auto* studio = studio_wrapper->studio;
    if (studio->bone_count <= 0 || studio->bone_count > 256 || studio->bone_index <= 0)
        return;
    alignas(16) matrix3x4_t bones[256]{};
    if (!read_rendered_bones(output.index, bones)) {
        // Fallback for configurations where the DME hook is not installed.
        // When DME is active, collect_model_geometry normally consumes the
        // exact matrices submitted by the renderer instead of rebuilding them.
        // SetupBones is IClientRenderable slot 13, not a method in the primary
        // C_CSPlayer vtable. Using the latter with this signature corrupts the
        // x86 stack and can also perturb the model's subsequent bone state.
        void* renderable = client_renderable(entity);
        const auto setup = virtual_function<setup_bones_t>(renderable, 13);
        if (!setup || !function_in_module(client_module, reinterpret_cast<const void*>(setup)))
            return;
        const float time = global_vars ? global_vars->current_time : 0.f;
        if (!setup(renderable, bones, 256, 0x0007FF00, time))
            return;
    }

    const auto* bone_array = reinterpret_cast<const mstudiobone_t*>(
        reinterpret_cast<const unsigned char*>(studio) + studio->bone_index);
    if (!readable_range(bone_array, static_cast<std::size_t>(studio->bone_count) * sizeof(mstudiobone_t)))
        return;
    if (collect_skeleton) {
        for (int i = 0; i < studio->bone_count; ++i) {
            const auto& bone = bone_array[i];
            if (!(bone.flags & 0x00000100) || bone.parent < 0 || bone.parent >= studio->bone_count)
                continue;
            output.skeleton.push_back({ bone_origin(bones[i]), bone_origin(bones[bone.parent]) });
        }
    }

    if (!collect_hitboxes || hitbox_set_index < 0 || hitbox_set_index >= studio->hitbox_set_count ||
        studio->hitbox_set_index <= 0) {
        return;
    }
    const auto* set = reinterpret_cast<const mstudiohitboxset_t*>(
        reinterpret_cast<const unsigned char*>(studio) + studio->hitbox_set_index) + hitbox_set_index;
    if (!readable_range(set, sizeof(mstudiohitboxset_t)) || set->hitbox_count < 0 ||
        set->hitbox_count > 64 || set->hitbox_index <= 0) {
        return;
    }
    static constexpr int edges[12][2] = {
        { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
        { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
        { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
    };
    for (int i = 0; i < set->hitbox_count; ++i) {
        const auto* box = set->hitbox(i);
        if (!readable_range(box, sizeof(mstudiobbox_t)) || box->bone < 0 || box->bone >= studio->bone_count)
            continue;
        vector3 points[8];
        for (int corner = 0; corner < 8; ++corner) {
            const vector3 local{
                (corner & 1) ? box->maxs.x : box->mins.x,
                (corner & 2) ? box->maxs.y : box->mins.y,
                (corner & 4) ? box->maxs.z : box->mins.z
            };
            points[corner] = transform(local, bones[box->bone]);
        }
        const vector3 local_center{
            (box->mins.x + box->maxs.x) * 0.5f,
            (box->mins.y + box->maxs.y) * 0.5f,
            (box->mins.z + box->maxs.z) * 0.5f
        };
        float radius = box->radius;
        if (!std::isfinite(radius) || radius <= 0.f) {
            radius = std::min(box->maxs.x - box->mins.x,
                box->maxs.y - box->mins.y) * 0.5f;
        }
        output.hitbox_markers.push_back({ transform(local_center, bones[box->bone]),
            std::clamp(radius, 0.5f, 16.f), i, box->group });
        for (const auto& edge : edges)
            output.hitboxes.push_back({ points[edge[0]], points[edge[1]] });
    }
}

void scan_inventory(void* player, player_data& output) {
    if (nv.my_weapons < 0)
        return;
    for (int slot = 0; slot < 64; ++slot) {
        const auto handle = value<unsigned int>(player, nv.my_weapons + slot * 4, 0xFFFFFFFFu);
        void* weapon = entity_from_handle(handle);
        if (!weapon)
            continue;
        const int id = item_definition(weapon);
        output.bomb = output.bomb || id == 49;
        output.taser = output.taser || id == 31;
    }
}

float calculate_hit_chance(void* local_weapon, float distance, bool immunity) {
    if (!local_weapon || immunity || distance <= 1.f)
        return -1.f;
    const auto inaccuracy_fn = virtual_function<weapon_float_t>(local_weapon, 483);
    const auto spread_fn = virtual_function<weapon_float_t>(local_weapon, 453);
    if (!inaccuracy_fn || !spread_fn ||
        !function_in_module(client_module, reinterpret_cast<const void*>(inaccuracy_fn)) ||
        !function_in_module(client_module, reinterpret_cast<const void*>(spread_fn))) {
        return -1.f;
    }
    const float inaccuracy = inaccuracy_fn(local_weapon);
    const float spread = spread_fn(local_weapon);
    if (!std::isfinite(inaccuracy) || !std::isfinite(spread) || inaccuracy < 0.f || spread < 0.f)
        return -1.f;
    // GetInaccuracy already includes the weapon's base spread on CS:GO; use
    // the larger cone rather than counting that base component twice.
    const float cone = std::max(0.00001f, std::max(inaccuracy, spread));
    // A centre-mass target is approximated by an 8-unit radius. This is an ESP
    // estimate, not a claim that an aimbot has selected or will fire at the target.
    const float target_angle = std::atan2(8.f, distance);
    return std::clamp(target_angle / cone, 0.f, 1.f) * 100.f;
}

bool read_player_info(int index, unsigned char (&info)[512]) {
    const auto get_info = virtual_function<get_player_info_t>(engine, 8);
    return get_info && function_in_module(engine_module, reinterpret_cast<const void*>(get_info)) &&
        get_info(engine, index, info);
}

std::string player_name(int index) {
    // The final Source 1 player_info_t is below 512 bytes. Name follows the two
    // 64-bit fields at offset 16; the oversized buffer prevents layout overflow.
    unsigned char info[512]{};
    if (!read_player_info(index, info))
        return {};
    const char* name = reinterpret_cast<const char*>(info + 16);
    std::size_t length = 0;
    while (length < 128 && name[length])
        ++length;
    return std::string(name, length);
}

bool acquire() {
    client_module = GetModuleHandleW(L"client.dll");
    engine_module = GetModuleHandleW(L"engine.dll");
    if (!client_module || !engine_module)
        return false;
    client = create_interface(client_module, "VClient018");
    engine = create_interface(engine_module, "VEngineClient014");
    entity_list = create_interface(client_module, "VClientEntityList003");
    if (!client || !engine || !entity_list)
        return false;
    // DME/chams need only the core entity and local-player interfaces. ESP-only
    // methods are validated at their call sites so a missing projection/name
    // helper cannot disable chams as a side effect.
    if (!checked_virtual<get_all_classes_t>(client, 8, client_module) ||
        !checked_virtual<entity_at_t>(entity_list, 3, client_module) ||
        !checked_virtual<entity_from_handle_t>(entity_list, 4, client_module) ||
        !checked_virtual<local_player_t>(engine, 12, engine_module) ||
        !checked_virtual<engine_bool_t>(engine, 26, engine_module)) {
        return false;
    }

    resolve_netvars();
    if (nv.team < 0 || nv.health < 0 || nv.life_state < 0 || nv.origin < 0)
        return false;

    static constexpr int global_pattern[] = {
        0xA1, -1, -1, -1, -1, 0xF3, 0x0F, 0x10, 0x40, -1,
        0x0F, 0x5A, 0xC0, 0xF2, 0x0F, 0x11, 0x04
    };
    static constexpr int weapon_system_pattern[] = {
        0x8B, 0x35, -1, -1, -1, -1, 0xFF, 0x10, 0x0F, 0xB7, 0xC0
    };
    static constexpr int studio_pattern[] = {
        0x8B, 0xB6, -1, -1, -1, -1, 0x85, 0xF6, 0x74, 0x05,
        0x83, 0x3E, 0x00, 0x75, 0x02, 0x33, 0xF6, 0xF3, 0x0F, 0x10, 0x44, 0x24
    };
    const auto* global_address = find_pattern(client_module, global_pattern);
    const auto* weapon_address = find_pattern(client_module, weapon_system_pattern);
    global_vars = resolve_absolute_pointer<global_vars_t>(global_address ? global_address + 1 : nullptr);
    weapon_system = resolve_absolute_pointer<void>(weapon_address ? weapon_address + 2 : nullptr);
    if (!readable_range(global_vars, sizeof(global_vars_t)))
        global_vars = nullptr;
    if (const auto* studio = find_pattern(client_module, studio_pattern)) {
        std::uint32_t displacement = 0;
        std::memcpy(&displacement, studio + 2, sizeof(displacement));
        if (displacement > 0 && displacement < 0x10000)
            studio_hdr_offset = displacement;
    }
    return true;
}
} // namespace

namespace visuals_source {
bool initialize() {
    if (core_ready.load(std::memory_order_acquire))
        return true;
    const ULONGLONG now = GetTickCount64();
    if (now < next_initialize_attempt.load(std::memory_order_relaxed))
        return false;
    next_initialize_attempt.store(now + 2000, std::memory_order_relaxed);
    std::scoped_lock lock(initialize_mutex);
    if (core_ready.load(std::memory_order_relaxed))
        return true;
    if (!acquire())
        return false;
    core_ready.store(client && engine && entity_list && nv.team >= 0 && nv.health >= 0 &&
                     nv.life_state >= 0 && nv.origin >= 0, std::memory_order_release);
    return core_ready.load(std::memory_order_relaxed);
}

bool ready() {
    return core_ready.load(std::memory_order_acquire);
}

bool is_in_game() {
    if (!ready())
        return false;
    // Interface lifetime matches core_ready. Cache the validated slot instead
    // of VirtualQuery-ing the engine vtable on every preview/model draw.
    static const auto function = virtual_function<engine_bool_t>(engine, 26);
    return function && function(engine);
}

bool is_console_visible() {
    if (!ready())
        return false;
    // VEngineClient014: Con_IsVisible is slot 11, immediately before
    // GetLocalPlayer (12). Used only to avoid capturing console pixels into
    // the foreground preview texture; it never hides the menu.
    static const auto function = virtual_function<engine_bool_t>(engine, 11);
    static const bool validated = function &&
        function_in_module(engine_module, reinterpret_cast<const void*>(function));
    return validated && function(engine);
}

int player_side_from_entity(void* player) {
    if (!player || !is_in_game())
        return -1;
    void* local_player = entity_at(local_index());
    if (!local_player || player == local_player || dormant(player))
        return -1;
    if (value<unsigned char>(player, nv.life_state, 0) != 0 ||
        value<int>(player, nv.health, 0) <= 0) {
        return -1;
    }
    const int team = value<int>(player, nv.team, 0);
    const int local_team = value<int>(local_player, nv.team, 0);
    if ((team != 2 && team != 3) || (local_team != 2 && local_team != 3))
        return -1;
    return team == local_team ? 1 : 0;
}

int player_side(int entity_index) {
    if (entity_index < 1 || entity_index > 64)
        return -1;
    return player_side_from_entity(entity_at(entity_index));
}

bool resolve_renderable_entity(void* renderable, renderable_entity_data& output) {
    if (!ready())
        return false;
    return renderable_entity(renderable, output);
}

int entity_team_side(void* entity) {
    if (!entity || !is_in_game())
        return -1;
    void* local = entity_at(local_index());
    if (!local)
        return -1;
    const int team = value<int>(entity, nv.team, 0);
    const int local_team = value<int>(local, nv.team, 0);
    if ((team != 2 && team != 3) || (local_team != 2 && local_team != 3))
        return -1;
    return team == local_team ? 1 : 0;
}

bool is_local_entity(void* entity) {
    return entity && is_in_game() && entity == entity_at(local_index());
}

bool local_player_alive() {
    if (!is_in_game())
        return false;
    void* local = entity_at(local_index());
    return local && value<unsigned char>(local, nv.life_state, 0) == 0 &&
        value<int>(local, nv.health, 0) > 0;
}

bool entity_parented_to_local(void* entity, bool local_alive) {
    if (!entity || nv.move_parent < 0 || !local_alive)
        return false;
    void* parent = entity_from_handle(value<unsigned int>(entity, nv.move_parent, 0xFFFFFFFFu));
    return parent && is_local_entity(parent);
}

int local_user_id() {
    unsigned char info[512]{};
    if (!is_in_game() || !read_player_info(local_index(), info))
        return -1;
    int user_id = -1;
    std::memcpy(&user_id, info + 144, sizeof(user_id));
    return user_id;
}

int aimed_player_index() {
    if (!is_in_game())
        return 0;
    const int local_idx = local_index();
    void* local = entity_at(local_idx);
    if (!local)
        return 0;
    const int local_team = value<int>(local, nv.team, 0);
    if (local_team != 2 && local_team != 3)
        return 0;
    const vector3 origin = value<vector3>(local, nv.origin);
    const vector3 view_offset = value<vector3>(local, nv.view_offset, { 0.f, 0.f, 64.f });
    const vector3 eye{ origin.x + view_offset.x, origin.y + view_offset.y,
        origin.z + view_offset.z };
    vector3 angles = view_angles();
    const vector3 punch = value<vector3>(local, nv.aim_punch);
    angles.x += punch.x;
    angles.y += punch.y;
    angles.z += punch.z;
    constexpr float deg_to_rad = 3.14159265358979323846f / 180.f;
    const float pitch = angles.x * deg_to_rad;
    const float yaw = angles.y * deg_to_rad;
    const vector3 forward{ std::cos(pitch) * std::cos(yaw),
        std::cos(pitch) * std::sin(yaw), -std::sin(pitch) };
    float best_angle = 0.20f;
    int best_index = 0;
    for (int index = 1; index <= 64; ++index) {
        if (index == local_idx)
            continue;
        void* player = entity_at(index);
        if (!player || dormant(player) || value<unsigned char>(player, nv.life_state, 0) != 0 ||
            value<int>(player, nv.health, 0) <= 0) {
            continue;
        }
        const int team = value<int>(player, nv.team, 0);
        if ((team != 2 && team != 3) || team == local_team)
            continue;
        player_data geometry;
        geometry.index = index;
        collect_model_geometry(player, value<int>(player, nv.hitbox_set, 0), geometry, false, true);
        vector3 point = value<vector3>(player, nv.origin);
        point.z += 64.f;
        for (const auto& marker : geometry.hitbox_markers) {
            if (marker.hitbox == 0) {
                point = marker.center;
                break;
            }
        }
        const vector3 delta{ point.x - eye.x, point.y - eye.y, point.z - eye.z };
        const float along = delta.x * forward.x + delta.y * forward.y + delta.z * forward.z;
        if (along < 1.f || along > 8192.f)
            continue;
        const float length_squared = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
        const float perpendicular = std::sqrt(std::max(0.f, length_squared - along * along));
        const float angle = std::atan2(perpendicular, along);
        if (std::isfinite(angle) && angle < best_angle) {
            best_angle = angle;
            best_index = index;
        }
    }
    return best_index;
}

int frame_number() {
    return global_vars ? global_vars->frame_count : 0;
}

bool player_geometry_requested(int side) {
    if (side == 2) {
        const auto& settings = player_visuals::self_esp_settings();
        return settings.esp_enabled && settings.main[player_visuals::main_skeleton];
    }
    if (side != 0 && side != 1)
        return false;
    const auto& settings = player_visuals::settings(side);
    return settings.esp_enabled &&
        (settings.main[player_visuals::main_skeleton] ||
         settings.aimbot[player_visuals::aimbot_hitboxes]);
}

void submit_rendered_player(int index, const vector3& origin, const void* bones,
                            bool include_bones) {
    if (index < 1 || index > 64 || !std::isfinite(origin.x) ||
        !std::isfinite(origin.y) || !std::isfinite(origin.z)) {
        return;
    }
    const int frame = snapshot_frame();
    const ULONGLONG now = GetTickCount64();
    std::lock_guard<std::mutex> lock(rendered_players_mutex);
    auto& snapshot = rendered_players[index];
    // Keep the first real player draw for this frame. Later chams/reflection
    // passes must not replace a coherent snapshot with another view of it.
    if (!snapshot.valid || snapshot.frame != frame || now < snapshot.time ||
        now - snapshot.time > 100) {
        snapshot = {};
        snapshot.frame = frame;
        snapshot.time = now;
        snapshot.origin = origin;
        snapshot.valid = true;
    }
    if (include_bones && bones && !snapshot.bones_valid &&
        readable_range(bones, sizeof(snapshot.bones))) {
        std::memcpy(snapshot.bones, bones, sizeof(snapshot.bones));
        snapshot.bones_valid = true;
    }
}

bool entity_exists(int entity_index) {
    return entity_index > 0 && entity_index <= 64 && entity_at(entity_index) != nullptr;
}

void capture_world(frame_data& output, const vector3& local_origin) {
    if (!player_visuals::world_settings().enabled)
        return;
    output.world.reserve(64);
    for (int index = 65; index < 2048; ++index) {
        void* entity = entity_at(index);
        if (!entity || dormant(entity))
            continue;
        const auto* client_class = entity_class(entity);
        if (!client_class || !readable_range(client_class->name, 1))
            continue;
        const char* class_name = client_class->name;
        const bool smoke_projectile = std::strstr(class_name, "SmokeGrenadeProjectile") != nullptr;
        const bool inferno = std::strcmp(class_name, "CInferno") == 0 ||
            std::strcmp(class_name, "C_Inferno") == 0;
        const bool planted_c4 = std::strstr(class_name, "PlantedC4") != nullptr;
        const bool projectile = std::strstr(class_name, "Projectile") != nullptr;
        const int definition = item_definition(entity);
        if (definition <= 0 && !smoke_projectile && !inferno && !planted_c4 && !projectile)
            continue;

        const bool smoke_active = smoke_projectile && value<bool>(entity, nv.smoke_effect, false);
        if (projectile && !smoke_active && nv.explode_tick >= 0 &&
            value<bool>(entity, nv.explode_tick + 4, false)) {
            continue;
        }
        const bool special = smoke_projectile || inferno || planted_c4 || projectile;
        const unsigned int owner = value<unsigned int>(entity, nv.owner, 0xFFFFFFFFu);
        if (!special && owner != 0 && owner != 0xFFFFFFFFu)
            continue;

        world_data data;
        data.index = index;
        data.weapon_id = definition;
        data.kind = inferno ? visuals_source::world_inferno :
            (planted_c4 ? visuals_source::world_planted_c4 :
            (smoke_active ? visuals_source::world_smoke :
                (projectile || smoke_projectile ? visuals_source::world_projectile
                    : visuals_source::world_weapon)));
        data.origin = value<vector3>(entity, nv.origin);
        data.mins = value<vector3>(entity, nv.mins, { -4.f, -4.f, -4.f });
        data.maxs = value<vector3>(entity, nv.maxs, { 4.f, 4.f, 4.f });
        data.distance = vector_length({ data.origin.x - local_origin.x,
            data.origin.y - local_origin.y, data.origin.z - local_origin.z });
        data.alpha = 1.f - std::clamp(data.distance / 1500.f, 0.f, 1.f);
        if (data.alpha <= 0.f)
            continue;
        data.name = planted_c4 ? "C4" : (definition > 0 ? weapon_name(definition) : "Grenade");
        if (data.kind == visuals_source::world_weapon) {
            data.clip = value<int>(entity, nv.clip, -1);
            if (const auto* info = weapon_info(definition))
                data.max_clip = info->max_clip1;
            if (data.max_clip <= 0)
                data.max_clip = fallback_max_clip(definition);
        }

        if (data.kind == visuals_source::world_smoke) {
            data.range = 160.5f;
            if (global_vars && nv.smoke_tick >= 0) {
                const int tick = value<int>(entity, nv.smoke_tick, 0);
                data.time_remaining = tick * global_vars->interval_per_tick + 17.5f -
                    global_vars->current_time;
            }
        } else if (data.kind == visuals_source::world_inferno) {
            // Airflow derives the visible inferno radius from active fire cells,
            // not from a fixed guess. The arrays are bounded to their network
            // schema limit so a malformed server value cannot cause a walk.
            const bool fire_arrays_valid = nv.fire_count >= 0 && nv.fire_burning >= 0 &&
                nv.fire_x >= 0 && nv.fire_y >= 0 && nv.fire_z >= 0;
            const int count = fire_arrays_valid
                ? std::clamp(value<int>(entity, nv.fire_count, 0), 0, 63) : 0;
            float radius = -1.f;
            vector3 average{};
            if (fire_arrays_valid) {
                for (int fire = 0; fire <= count; ++fire) {
                    if (!value<bool>(entity, nv.fire_burning + fire, false))
                        continue;
                    const float x = static_cast<float>(value<int>(entity, nv.fire_x + fire * 4, 0));
                    const float y = static_cast<float>(value<int>(entity, nv.fire_y + fire * 4, 0));
                    const float z = static_cast<float>(value<int>(entity, nv.fire_z + fire * 4, 0));
                    radius = std::max(radius, std::sqrt(x * x + y * y) + 14.4f);
                    average.x += x;
                    average.y += y;
                    average.z += z;
                }
            }
            if (count > 1) {
                data.origin.x += average.x / count;
                data.origin.y += average.y / count;
                data.origin.z += average.z / count;
            }
            data.range = std::max(100.f, radius);
            data.time_remaining = -1.f;
        }
        output.world.push_back(std::move(data));
    }
}

void capture_local_player(frame_data& output, void* local, void* local_weapon, int local_team) {
    const auto& settings = player_visuals::self_esp_settings();
    if (!output.local_alive || (local_team != 2 && local_team != 3) || dormant(local) ||
        !settings.esp_enabled) {
        return;
    }

    // Self has its own ESP settings, but uses the same value-type snapshot and
    // renderer as the other players. side 2 is local-only and never enters DME
    // team classification or the enemy/teammate chams settings lookup.
    player_data data;
    data.index = output.local_index;
    data.side = 2;
    data.team = local_team;
    data.health = value<int>(local, nv.health, 0);
    data.origin = output.local_origin;
    data.mins = value<vector3>(local, nv.mins, { -16.f, -16.f, 0.f });
    data.maxs = value<vector3>(local, nv.maxs, { 16.f, 16.f, 72.f });
    data.distance = 0.f;
    data.name = player_name(output.local_index);
    data.armor_valid = nv.armor >= 0;
    data.money_valid = nv.money >= 0;
    data.immunity_valid = nv.immunity >= 0;
    data.armor = std::max(0, value<int>(local, nv.armor, 0));
    data.money = std::max(0, value<int>(local, nv.money, 0));
    data.helmet = value<bool>(local, nv.helmet, false);
    data.defuser = value<bool>(local, nv.defuser, false);
    data.defusing = value<bool>(local, nv.defusing, false);
    data.scoped = value<bool>(local, nv.scoped, false);
    data.immunity = value<bool>(local, nv.immunity, false);
    data.blind = value<float>(local, nv.flash_duration, 0.f) > 0.01f;
    data.slowed = value<float>(local, nv.velocity_modifier, 1.f) < 0.99f;
    data.hostage = entity_from_handle(value<unsigned int>(local, nv.carried_hostage, 0xFFFFFFFFu)) != nullptr;
    if (global_vars && nv.simulation_time >= 0) {
        const float simulation = value<float>(local, nv.simulation_time, global_vars->current_time);
        data.delay_ms = std::max(0.f, (global_vars->current_time - simulation) * 1000.f);
    }

    if (local_weapon) {
        data.weapon_id = item_definition(local_weapon);
        if (data.weapon_id > 0)
            data.weapon_name = weapon_name(data.weapon_id);
        data.clip = value<int>(local_weapon, nv.clip, -1);
        data.reserve_ammo = value<int>(local_weapon, nv.reserve_ammo, -1);
        data.reloading = value<bool>(local_weapon, nv.reloading, false);
        data.pin_pulled = value<bool>(local_weapon, nv.pin_pulled, false);
        if (const auto* info = weapon_info(data.weapon_id)) {
            data.max_clip = info->max_clip1;
            if (global_vars && info->cycle_time > 0.001f && info->cycle_time < 10.f) {
                float ready_time = value<float>(local_weapon, nv.next_primary_attack, global_vars->current_time);
                ready_time = std::max(ready_time,
                    value<float>(local_weapon, nv.postpone_fire_ready, ready_time));
                data.readiness = std::clamp(1.f - std::max(0.f,
                    ready_time - global_vars->current_time) / info->cycle_time, 0.f, 1.f);
            }
        }
        if (data.max_clip <= 0)
            data.max_clip = fallback_max_clip(data.weapon_id);
    }
    if (settings.flags[player_visuals::flag_bomb] ||
        settings.weapon[player_visuals::weapon_bomb] ||
        settings.weapon[player_visuals::weapon_taser]) {
        scan_inventory(local, data);
    }
    data.bomb = data.bomb || data.weapon_id == 49;
    data.taser = data.taser || data.weapon_id == 31;
    collect_model_geometry(local, value<int>(local, nv.hitbox_set, 0), data,
        settings.main[player_visuals::main_skeleton], false);
    data.alpha = 1.f;
    output.players.push_back(std::move(data));
}

void capture(frame_data& output) {
    output = {};
    if (!is_in_game()) {
        clear_rendered_players();
        return;
    }
    output.in_game = true;
    output.local_index = local_index();
    void* local = entity_at(output.local_index);
    if (!local) {
        clear_rendered_players();
        output.in_game = false;
        return;
    }
    if (const auto matrix_fn = virtual_function<world_matrix_t>(engine, 37);
        matrix_fn && function_in_module(engine_module, reinterpret_cast<const void*>(matrix_fn))) {
        const float* matrix = matrix_fn(engine);
        if (matrix) {
            std::memcpy(output.view_matrix, matrix, sizeof(output.view_matrix));
            output.has_view_matrix = true;
        }
    }

    output.local_alive = value<unsigned char>(local, nv.life_state, 0) == 0 &&
        value<int>(local, nv.health, 0) > 0;
    const int local_team = value<int>(local, nv.team, 0);
    const vector3 local_origin = current_player_origin(output.local_index, local);
    output.local_origin = local_origin;
    output.view_angles = view_angles();
    void* local_weapon = entity_from_handle(value<unsigned int>(local, nv.active_weapon, 0xFFFFFFFFu));

    output.players.reserve(32);
    for (int index = 1; index <= 64; ++index) {
        if (index == output.local_index)
            continue;
        void* player = entity_at(index);
        if (!player)
            continue;
        const auto life_state = value<unsigned char>(player, nv.life_state, 0);
        const int health = value<int>(player, nv.health, 0);
        const int team = value<int>(player, nv.team, 0);
        // Player ESP is deliberately fresh-only. A dead, missing or dormant
        // entity must not resurrect a previous frame's transform.
        if (team != 2 && team != 3)
            continue;
        if (life_state != 0 || health <= 0 || dormant(player))
            continue;
        if (local_team != 2 && local_team != 3)
            continue;
        player_data data;
        data.index = index;
        data.team = team;
        data.health = health;
        data.side = data.team == local_team ? 1 : 0;
        const auto& side_settings = player_visuals::settings(data.side);
        if (!side_settings.esp_enabled)
            continue;
        data.origin = current_player_origin(index, player);
        data.mins = value<vector3>(player, nv.mins, { -16.f, -16.f, 0.f });
        data.maxs = value<vector3>(player, nv.maxs, { 16.f, 16.f, 72.f });
        data.distance = vector_length({ data.origin.x - local_origin.x,
            data.origin.y - local_origin.y, data.origin.z - local_origin.z }) * 0.0254f;
        data.name = player_name(index);
        data.armor_valid = nv.armor >= 0;
        data.money_valid = nv.money >= 0;
        data.immunity_valid = nv.immunity >= 0;
        data.armor = std::max(0, value<int>(player, nv.armor, 0));
        data.money = std::max(0, value<int>(player, nv.money, 0));
        data.helmet = value<bool>(player, nv.helmet, false);
        data.defuser = value<bool>(player, nv.defuser, false);
        data.defusing = value<bool>(player, nv.defusing, false);
        data.scoped = value<bool>(player, nv.scoped, false);
        data.immunity = value<bool>(player, nv.immunity, false);
        data.blind = value<float>(player, nv.flash_duration, 0.f) > 0.01f;
        data.slowed = value<float>(player, nv.velocity_modifier, 1.f) < 0.99f;
        data.hostage = entity_from_handle(value<unsigned int>(player, nv.carried_hostage, 0xFFFFFFFFu)) != nullptr;
        if (global_vars && nv.simulation_time >= 0) {
            const float simulation = value<float>(player, nv.simulation_time, global_vars->current_time);
            data.delay_ms = std::max(0.f, (global_vars->current_time - simulation) * 1000.f);
        }

        void* weapon = entity_from_handle(value<unsigned int>(player, nv.active_weapon, 0xFFFFFFFFu));
        if (weapon) {
            data.weapon_id = item_definition(weapon);
            if (data.weapon_id > 0)
                data.weapon_name = weapon_name(data.weapon_id);
            data.clip = value<int>(weapon, nv.clip, -1);
            data.reserve_ammo = value<int>(weapon, nv.reserve_ammo, -1);
            data.reloading = value<bool>(weapon, nv.reloading, false);
            data.pin_pulled = value<bool>(weapon, nv.pin_pulled, false);
            if (const auto* info = weapon_info(data.weapon_id)) {
                data.max_clip = info->max_clip1;
                if (global_vars && info->cycle_time > 0.001f && info->cycle_time < 10.f) {
                    float ready_time = value<float>(weapon, nv.next_primary_attack, global_vars->current_time);
                    ready_time = std::max(ready_time,
                        value<float>(weapon, nv.postpone_fire_ready, ready_time));
                    data.readiness = std::clamp(1.f - std::max(0.f,
                        ready_time - global_vars->current_time) / info->cycle_time, 0.f, 1.f);
                }
            }
            if (data.max_clip <= 0)
                data.max_clip = fallback_max_clip(data.weapon_id);
        }
        if (side_settings.flags[player_visuals::flag_bomb] ||
            side_settings.weapon[player_visuals::weapon_bomb] ||
            side_settings.weapon[player_visuals::weapon_taser]) {
            scan_inventory(player, data);
        }
        // The active weapon is still useful when the inventory array is not
        // available; never require a second netvar for an item already in hand.
        data.bomb = data.bomb || data.weapon_id == 49;
        data.taser = data.taser || data.weapon_id == 31;
        if (side_settings.flags[player_visuals::flag_vulnerable]) {
            data.hit_chance = calculate_hit_chance(local_weapon, data.distance * 39.3701f, data.immunity);
            data.vulnerable = data.immunity_valid && !data.immunity && data.hit_chance >= 5.f;
        }
        collect_model_geometry(player, value<int>(player, nv.hitbox_set, 0), data,
            side_settings.main[player_visuals::main_skeleton],
            side_settings.aimbot[player_visuals::aimbot_hitboxes]);
        data.alpha = 1.f;
        output.players.push_back(std::move(data));
    }

    capture_local_player(output, local, local_weapon, local_team);

    // Order Priority is a deterministic closest-first target order, calculated
    // independently for enemies and teammates rather than read from a fake prop.
    for (int side = 0; side < 2; ++side) {
        std::vector<player_data*> ordered;
        for (auto& player : output.players)
            if (player.side == side)
                ordered.push_back(&player);
        std::sort(ordered.begin(), ordered.end(), [](const player_data* a, const player_data* b) {
            if (a->distance != b->distance)
                return a->distance < b->distance;
            return a->index < b->index;
        });
        for (std::size_t i = 0; i < ordered.size(); ++i)
            ordered[i]->order_priority = static_cast<int>(i + 1);
    }
    capture_world(output, local_origin);
}

int atlas_weapon_id(int id) {
    switch (id) {
    case 32: return 0;
    case 61: return 1;
    case 4: return 2;
    case 36: return 3;
    case 63: return 4;
    case 3: return 5;
    case 30: return 6;
    case 2: return 7;
    case 1: return 8;
    case 64: return 9;
    case 13: return 10;
    case 10: return 11;
    case 7: return 12;
    case 16: return 13;
    case 60: return 14;
    case 39: return 15;
    case 8: return 16;
    case 40: return 17;
    case 9: return 18;
    case 11: return 19;
    case 38: return 20;
    case 33: return 21;
    case 34: return 22;
    case 17: return 23;
    case 24: return 24;
    case 23: return 25;
    case 19: return 26;
    case 26: return 27;
    case 35: return 28;
    case 25: return 29;
    case 27: return 30;
    case 29: return 31;
    case 14: return 32;
    case 28: return 33;
    case 31: return 34;
    default: return -1;
    }
}
} // namespace visuals_source
