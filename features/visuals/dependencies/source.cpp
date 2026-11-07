#include "source.hpp"
#include "../settings.hpp"
#include "../../../sdk/config_system/config_system.hpp"

#include <Windows.h>
#include "MinHook.h"
#include "../../../runtime/debug_console.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <utility>

namespace {
static_assert(sizeof(void*) == 4, "Source 1 player visuals require the x86 ABI");

using visuals_source::line3;
using visuals_source::player_data;
using visuals_source::world_data;
using visuals_source::renderable_entity_data;
using visuals_source::frame_data;
using visuals_source::vector3;

struct matrix3x4_t {
    float m[3][4];
};
static_assert(sizeof(matrix3x4_t) == 48, "Source 1 matrix3x4_t layout changed");

template <typename T>
struct utl_vector_t {
    T* memory;
    int allocation_count;
    int grow_size;
    int size;
};
static_assert(sizeof(utl_vector_t<matrix3x4_t>) == 16, "Source 1 CUtlVector layout changed");

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
    int velocity = -1;
    int view_offset = -1;
    int aim_punch = -1;
    int view_punch = -1;
    int flash_max_alpha = -1;
    int mins = -1;
    int maxs = -1;
    int pose_parameters = -1;
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
    int zoom_level = -1;
    int accuracy_penalty = -1;
    int player_fov = -1;
    int spotted = -1;
    int spotted_by_mask = -1;
    int clip = -1;
    int reserve_ammo = -1;
    int next_primary_attack = -1;
    int postpone_fire_ready = -1;
    int reloading = -1;
    int pin_pulled = -1;
    int viewmodel = -1;
    int angles = -1;
    int observer_target = -1;
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

// Airflow's persistent ESP state is updated at FRAME_RENDER_END. It contains
// values only, never entity pointers; dormant records are the deliberate
// short-lived fade state used by the main renderer.
struct cached_player {
    player_data data;
    ULONGLONG last_seen = 0;
    ULONGLONG last_name_query = 0;
    float game_time = 0.f;
    bool valid = false;
};
cached_player player_cache[65];
ULONGLONG last_capture_time = 0;

std::mutex staged_frame_mutex;
std::shared_ptr<frame_data> staged_frame;
// Writer-owned spare buffer. Reusing it keeps the players/world vectors (and
// their capacity) alive between frames instead of reallocating every Present.
std::shared_ptr<frame_data> spare_frame;
std::atomic<bool> frame_stage_hook_ready = false;
const std::uint8_t* view_matrix_pattern = nullptr;

float airflow_system_time() {
    // Same timing dependency as main: clock() / 1000.f, not ImGui::GetTime().
    return static_cast<float>(std::clock()) / 1000.f;
}

float airflow_last_time = -1.f;
float airflow_speed = 0.f;

float airflow_animation_speed(bool reset = false) {
    if (reset) {
        airflow_last_time = -1.f;
        airflow_speed = 0.f;
        return airflow_speed;
    }
    const float now = airflow_system_time();
    if (airflow_last_time < 0.f)
        airflow_last_time = now;
    airflow_speed = std::fabs(airflow_last_time - now) * 5.f;
    airflow_last_time = now;
    return airflow_speed;
}

float current_airflow_animation_speed() {
    return airflow_speed;
}


// VirtualQuery is needed for safety, but not once per netvar read. Cache the
// last validated region only for the duration of one capture; no validation is
// carried across frames or entity lifetimes.
struct capture_memory_cache {
    std::uintptr_t region_begin = 0;
    std::uintptr_t region_end = 0;
};
thread_local capture_memory_cache* active_capture_memory_cache = nullptr;

struct capture_memory_cache_scope {
    capture_memory_cache cache;
    capture_memory_cache* previous = nullptr;

    capture_memory_cache_scope() : previous(active_capture_memory_cache) {
        active_capture_memory_cache = &cache;
    }
    ~capture_memory_cache_scope() {
        active_capture_memory_cache = previous;
    }
};

bool readable_range(const void* address, std::size_t size) {
    if (!address || !size)
        return false;
    auto current = reinterpret_cast<std::uintptr_t>(address);
    const auto end = current + size;
    if (end < current)
        return false;
    if (active_capture_memory_cache) {
        const auto& cache = *active_capture_memory_cache;
        if (cache.region_begin <= current && end <= cache.region_end)
            return true;
    }
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
        if (active_capture_memory_cache) {
            active_capture_memory_cache->region_begin = begin;
            active_capture_memory_cache->region_end = region_end;
        }
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

template <typename Fn>
struct virtual_cache {
    const void* table = nullptr;
    HMODULE module = nullptr;
    Fn function = nullptr;
};

template <typename Fn>
Fn cached_virtual(void* instance, std::size_t index, HMODULE module,
                  virtual_cache<Fn>& cache) {
    if (!instance)
        return nullptr;
    // Engine-owned objects keep their vtable for the object's lifetime. A
    // changed vtable invalidates the slot and takes the fully validated path
    // again; the steady-state path avoids repeated VirtualQuery/PE scans.
    const void* table = *reinterpret_cast<const void* const*>(instance);
    if (table && table == cache.table && module == cache.module)
        return cache.function;
    cache.function = virtual_function<Fn>(instance, index);
    if (!function_in_module(module, reinterpret_cast<const void*>(cache.function)))
        cache.function = nullptr;
    cache.table = table;
    cache.module = module;
    return cache.function;
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
    nv.velocity = table_offset(classes, { "DT_BaseEntity" }, "m_vecVelocity[0]");
    if (nv.velocity < 0)
        nv.velocity = table_offset(classes, { "DT_BaseEntity" }, "m_vecVelocity");
    nv.view_offset = table_offset(classes, { "DT_CSPlayer", "DT_BasePlayer" }, "m_vecViewOffset[0]");
    if (nv.view_offset < 0)
        nv.view_offset = table_offset(classes, { "DT_CSPlayer", "DT_BasePlayer" }, "m_vecViewOffset");
    nv.aim_punch = table_offset(classes, { "DT_CSPlayer", "DT_BasePlayer" }, "m_aimPunchAngle");
    nv.view_punch = table_offset(classes, { "DT_CSPlayer", "DT_BasePlayer" }, "m_viewPunchAngle");
    nv.flash_max_alpha = table_offset(classes, { "DT_CSPlayer" }, "m_flFlashMaxAlpha");
    nv.mins = table_offset(classes, { "DT_CSPlayer", "DT_BaseEntity" }, "m_vecMins");
    nv.maxs = table_offset(classes, { "DT_CSPlayer", "DT_BaseEntity" }, "m_vecMaxs");
    nv.pose_parameters = table_offset(classes, { "DT_CSPlayer", "DT_BaseAnimating" }, "m_flPoseParameter");
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
    nv.zoom_level = table_offset(classes, { "DT_WeaponCSBase" }, "m_zoomLevel");
    if (nv.zoom_level < 0)
        nv.zoom_level = table_offset(classes, { "DT_WeaponCSBase" }, "m_iZoomLevel");
    // World > HUD > Inaccuracy overlay: the accuracy penalty is a plain netvar,
    // so the ring no longer calls GetInaccuracy/GetSpread virtuals (a vtable
    // index mismatch there is what crashed the overlay).
    nv.accuracy_penalty = table_offset(classes, { "DT_WeaponCSBase", "DT_LocalWeaponData" },
        "m_fAccuracyPenalty");
    if (nv.accuracy_penalty < 0)
        nv.accuracy_penalty = table_offset(classes, { "DT_WeaponCSBase" }, "m_fAccuracyPenalty");
    // World > Scope options: the engine's current zoom FOV tells the first and
    // second scope apart (second zoom is <= 20 degrees on every sniper).
    nv.player_fov = table_offset(classes, { "DT_BasePlayer" }, "m_iFOV");
    // World > HUD > Radar: "reveal" is the in-game radar flag, not a custom map.
    nv.spotted = table_offset(classes, { "DT_BaseEntity" }, "m_bSpotted");
    nv.spotted_by_mask = table_offset(classes, { "DT_BaseEntity" }, "m_bSpottedByMask");
    nv.clip = table_offset(classes, { "DT_BaseCombatWeapon" }, "m_iClip1");
    nv.reserve_ammo = table_offset(classes, { "DT_BaseCombatWeapon" }, "m_iPrimaryReserveAmmoCount");
    nv.next_primary_attack = table_offset(classes, { "DT_BaseCombatWeapon" }, "m_flNextPrimaryAttack");
    nv.postpone_fire_ready = table_offset(classes, { "DT_WeaponCSBase" }, "m_flPostponeFireReadyTime");
    nv.reloading = table_offset(classes, { "DT_WeaponCSBase", "DT_BaseCombatWeapon" }, "m_bInReload");
    nv.pin_pulled = table_offset(classes, { "DT_BaseCSGrenade" }, "m_bPinPulled");
    // World > Viewmodel > Angles: m_hViewModel[0] handle + the entity angles the
    // game applies to the viewmodel (the same pair the NL Lua writes).
    nv.viewmodel = table_offset(classes, { "DT_BasePlayer" }, "m_hViewModel");
    nv.angles = table_offset(classes, { "DT_BaseEntity" }, "m_angRotation");
    nv.observer_target = table_offset(classes, { "DT_BasePlayer" }, "m_hObserverTarget");
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

// value<T>() runs thousands of times per frame, so it cannot VirtualQuery every
// single read. The page result is cached instead. This guard exists because a
// stale entity/weapon pointer (a handle whose entity was already freed, or an
// entity that left the map) reached memcpy and crashed with a read at a garbage
// address - the crash dump from the frame stage showed exactly that: memcpy of
// two bytes from 0x3F58229A.
std::uintptr_t readable_region_end(std::uintptr_t page) {
    struct entry {
        std::uintptr_t page;
        std::uintptr_t end;
    };
    static thread_local entry cache[128]{};
    auto& slot = cache[(page >> 12) & 127];
    if (slot.page == page)
        return slot.end;
    MEMORY_BASIC_INFORMATION info{};
    std::uintptr_t end = 0;
    if (VirtualQuery(reinterpret_cast<const void*>(page), &info, sizeof(info)) &&
        info.State == MEM_COMMIT && (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) == 0) {
        end = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (end <= page)
            end = 0;
    }
    slot.page = page;
    slot.end = end;
    return end;
}

bool readable_range_cached(const void* address, std::size_t size) {
    if (!address || !size)
        return false;
    auto current = reinterpret_cast<std::uintptr_t>(address);
    const auto end = current + size;
    if (end < current)
        return false;
    while (current < end) {
        const auto region_end = readable_region_end(current & ~static_cast<std::uintptr_t>(0xFFF));
        if (region_end == 0 || region_end <= current)
            return false;
        current = region_end;
    }
    return true;
}

template <typename T>
T value(const void* entity, int offset, T fallback = T{}) {
    if (!entity || offset < 0)
        return fallback;
    const auto* address = reinterpret_cast<const unsigned char*>(entity) + offset;
    if (!readable_range_cached(address, sizeof(T)))
        return fallback;
    T result{};
    std::memcpy(&result, address, sizeof(T));
    return result;
}

using entity_at_t = void* (__thiscall*)(void*, int);
using entity_from_handle_t = void* (__thiscall*)(void*, unsigned int);
using local_player_t = int(__thiscall*)(void*);
using engine_bool_t = bool(__thiscall*)(void*);
using get_player_info_t = bool(__thiscall*)(void*, int, void*);
using get_view_angles_t = void (__thiscall*)(void*, vector3&);
using world_matrix_t = const float* (__thiscall*)(void*);
using abs_origin_t = const vector3& (__thiscall*)(void*);
using frame_stage_t = void (__thiscall*)(void*, int);
frame_stage_t original_frame_stage = nullptr;
using entity_void_t = void* (__thiscall*)(void*);
using get_client_class_t = client_class_t* (__thiscall*)(void*);
using setup_bones_t = bool(__thiscall*)(void*, matrix3x4_t*, int, int, float);
using dormant_t = bool(__thiscall*)(void*);
using get_weapon_data_t = weapon_info_t* (__thiscall*)(void*, unsigned int);

int highest_entity_index() {
    using highest_t = int(__thiscall*)(void*);
    static thread_local virtual_cache<highest_t> cache;
    const auto function = cached_virtual(entity_list, 6, client_module, cache);
    const int highest = function ? function(entity_list) : 0;
    return std::clamp(highest, 0, 2047);
}

void* entity_at(int index) {
    if (index < 0)
        return nullptr;
    static thread_local virtual_cache<entity_at_t> cache;
    const auto function = cached_virtual(entity_list, 3, client_module, cache);
    return function ? function(entity_list, index) : nullptr;
}

int local_index() {
    static thread_local virtual_cache<local_player_t> cache;
    const auto function = cached_virtual(engine, 12, engine_module, cache);
    return function ? function(engine) : 0;
}

vector3 view_angles() {
    vector3 result{};
    static thread_local virtual_cache<get_view_angles_t> cache;
    const auto function = cached_virtual(engine, 18, engine_module, cache);
    if (function)
        function(engine, result);
    return result;
}

void* entity_from_handle(unsigned int handle) {
    if (handle == 0 || handle == 0xFFFFFFFFu)
        return nullptr;
    static thread_local virtual_cache<entity_from_handle_t> cache;
    const auto function = cached_virtual(entity_list, 4, client_module, cache);
    return function ? function(entity_list, handle) : nullptr;
}

bool dormant(void* entity) {
    void* networkable = reinterpret_cast<unsigned char*>(entity) + 8;
    static thread_local virtual_cache<dormant_t> cache;
    const auto function = cached_virtual(networkable, 9, client_module, cache);
    return !function || function(networkable);
}

void* client_renderable(void* entity) {
    // IClientEntity's renderable subobject is immediately after the primary
    // vtable pointer on the Source 1 x86 entity layout.
    return entity ? reinterpret_cast<unsigned char*>(entity) + 4 : nullptr;
}

vector3 abs_origin(void* entity, const vector3& fallback) {
    // Airflow's ESP reads IClientEntity::GetAbsOrigin (slot 10), rather than
    // mixing a network origin with a separately interpolated render origin.
    static thread_local virtual_cache<abs_origin_t> cache;
    const auto function = cached_virtual(entity, 10, client_module, cache);
    if (!function)
        return fallback;
    const auto& value = function(entity);
    if (!readable_range(&value, sizeof(vector3)))
        return fallback;
    const vector3 result = value;
    return std::isfinite(result.x) && std::isfinite(result.y) && std::isfinite(result.z)
        ? result : fallback;
}

const float* airflow_view_matrix() {
    if (!view_matrix_pattern)
        return nullptr;
    // main: *(pattern + 3) is the absolute matrix address; +176 selects the
    // same world-to-screen matrix used by Airflow's c_render.
    std::uintptr_t address = 0;
    std::memcpy(&address, view_matrix_pattern + 3, sizeof(address));
    const auto* matrix = reinterpret_cast<const float*>(address + 176);
    if (!readable_range(matrix, sizeof(float) * 16))
        return nullptr;
    for (int i = 0; i < 16; ++i)
        if (!std::isfinite(matrix[i]))
            return nullptr;
    return matrix;
}

void update_view_projection(frame_data& output) {
    output.has_view_matrix = false;
    if (const float* matrix = airflow_view_matrix()) {
        std::memcpy(output.view_matrix, matrix, sizeof(output.view_matrix));
        output.has_view_matrix = true;
    } else {
        static thread_local virtual_cache<world_matrix_t> cache;
        const auto matrix_fn = cached_virtual(engine, 37, engine_module, cache);
        if (!matrix_fn)
            return;
        const float* engine_matrix = matrix_fn(engine);
        if (engine_matrix && readable_range(engine_matrix, sizeof(output.view_matrix))) {
            std::memcpy(output.view_matrix, engine_matrix, sizeof(output.view_matrix));
            output.has_view_matrix = true;
        }
    }
}

void refresh_render_view(frame_data& output) {
    // Main stores entity values at FRAME_RENDER_END but projects them with the
    // current render view. Do not recapture players or rebuild their bones.
    update_view_projection(output);
    output.view_angles = view_angles();
    void* local = output.local_index > 0 ? entity_at(output.local_index) : nullptr;
    if (!local) {
        output.local_alive = false;
        output.in_game = false;
        return;
    }
    output.local_alive = value<unsigned char>(local, nv.life_state, 0) == 0 &&
        value<int>(local, nv.health, 0) > 0;
    output.local_origin = abs_origin(local, value<vector3>(local, nv.origin));
}

void clear_player_cache() {
    for (auto& cached : player_cache)
        cached.valid = false;
    last_capture_time = 0;
    airflow_animation_speed(true);
    std::lock_guard<std::mutex> lock(staged_frame_mutex);
    staged_frame.reset();
    spare_frame.reset();
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

template <std::size_t Size>
void copy_text(char (&destination)[Size], const std::string& source) {
    const std::size_t length = std::min(source.size(), Size - 1);
    std::memcpy(destination, source.data(), length);
    destination[length] = '\0';
}

template <std::size_t Size>
void copy_text(char (&destination)[Size], const char* source) {
    const char* text = source ? source : "";
    std::size_t length = 0;
    while (length + 1 < Size && text[length]) {
        destination[length] = text[length];
        ++length;
    }
    destination[length] = '\0';
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
    struct info_cache_entry {
        weapon_info_t* info = nullptr;
        bool resolved = false;
    };
    // Item definitions and their engine-owned CCSWeaponData are stable. Cache
    // both hits and misses instead of repeating GetWeaponData + VirtualQuery
    // for every player holding the same weapon on every frame.
    static thread_local std::array<info_cache_entry, 1024> info_cache;
    const bool cacheable = id < static_cast<int>(info_cache.size());
    if (cacheable && info_cache[id].resolved)
        return info_cache[id].info;
    static thread_local virtual_cache<get_weapon_data_t> cache;
    const auto function = cached_virtual(weapon_system, 2, client_module, cache);
    if (!function)
        return nullptr;
    auto* result = function(weapon_system, static_cast<unsigned int>(id));
    if (!readable_range(result, sizeof(weapon_info_t)))
        result = nullptr;
    if (cacheable)
        info_cache[id] = { result, true };
    return result;
}

bool weapon_capture_needed(const player_visuals::side_settings& settings) {
    return settings.main[player_visuals::main_ammo_bar] ||
        settings.flags[player_visuals::flag_reload] ||
        settings.flags[player_visuals::flag_bomb] ||
        settings.flags[player_visuals::flag_pin_pulled] ||
        settings.weapon[player_visuals::weapon_text] ||
        settings.weapon[player_visuals::weapon_icon] ||
        settings.weapon[player_visuals::weapon_readiness_bar] ||
        settings.weapon[player_visuals::weapon_bomb] ||
        settings.weapon[player_visuals::weapon_taser];
}

void capture_weapon(void* weapon, const player_visuals::side_settings& settings,
                    player_data& output) {
    if (!weapon)
        return;
    // Keep the identity/name snapshot even when its display element is off;
    // dormant records must retain it for a later settings change.
    output.weapon_id = item_definition(weapon);
    if (output.weapon_id > 0)
        copy_text(output.weapon_name, weapon_name(output.weapon_id));
    if (!weapon_capture_needed(settings))
        return;
    const bool ammo = settings.main[player_visuals::main_ammo_bar];
    const bool readiness = settings.weapon[player_visuals::weapon_readiness_bar];
    if (ammo)
        output.clip = value<int>(weapon, nv.clip, -1);
    if (settings.flags[player_visuals::flag_reload])
        output.reloading = value<bool>(weapon, nv.reloading, false);
    if (settings.flags[player_visuals::flag_pin_pulled])
        output.pin_pulled = value<bool>(weapon, nv.pin_pulled, false);
    if (!ammo && !readiness)
        return;
    if (const auto* info = weapon_info(output.weapon_id)) {
        if (ammo)
            output.max_clip = info->max_clip1;
        if (readiness && global_vars && info->cycle_time > 0.001f && info->cycle_time < 10.f) {
            float ready_time = value<float>(weapon, nv.next_primary_attack, global_vars->current_time);
            ready_time = std::max(ready_time,
                value<float>(weapon, nv.postpone_fire_ready, ready_time));
            output.readiness = std::clamp(1.f - std::max(0.f,
                ready_time - global_vars->current_time) / info->cycle_time, 0.f, 1.f);
        }
    }
    if (ammo && output.max_clip <= 0)
        output.max_clip = fallback_max_clip(output.weapon_id);
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

bool read_engine_bone_cache(void* entity, matrix3x4_t* output, int required_bones) {
    if (!entity || !output || required_bones <= 0 || required_bones > 256)
        return false;
    // Airflow Live uses 0x2914; its Legacy layout uses 0x2900. Validate the
    // complete CUtlVector before accepting either layout.
    for (const std::size_t offset : { std::size_t(0x2914), std::size_t(0x2900) }) {
        const auto* cache = reinterpret_cast<const utl_vector_t<matrix3x4_t>*>(
            reinterpret_cast<const unsigned char*>(entity) + offset);
        if (!readable_range(cache, sizeof(utl_vector_t<matrix3x4_t>)) ||
            cache->size < required_bones || cache->size > 256 ||
            cache->allocation_count < cache->size || cache->allocation_count > 4096 ||
            !cache->memory) {
            continue;
        }
        const std::size_t bytes = static_cast<std::size_t>(cache->size) * sizeof(matrix3x4_t);
        if (!readable_range(cache->memory, bytes))
            continue;
        bool finite = true;
        bool nonzero = false;
        for (int bone = 0; bone < required_bones && finite; ++bone) {
            for (int row = 0; row < 3 && finite; ++row) {
                for (int column = 0; column < 4; ++column) {
                    const float component = cache->memory[bone].m[row][column];
                    if (!std::isfinite(component)) {
                        finite = false;
                        break;
                    }
                    nonzero = nonzero || std::fabs(component) > 0.000001f;
                }
            }
        }
        if (!finite || !nonzero)
            continue;
        std::memcpy(output, cache->memory, bytes);
        return true;
    }
    return false;
}

void collect_model_geometry(void* entity, int hitbox_set_index, player_data& output,
                            bool collect_skeleton, bool collect_hitboxes) {
    if (collect_skeleton)
        output.skeleton.clear();
    if (collect_hitboxes) {
        output.hitboxes.clear();
        output.hitbox_markers.clear();
    }
    if ((!collect_skeleton && !collect_hitboxes) || !studio_hdr_offset)
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
    if (!read_engine_bone_cache(entity, bones, studio->bone_count)) {
        // Main normally consumes the engine-maintained bone cache. Keep the
        // renderable SetupBones call only for a not-yet-built cache.
        void* renderable = client_renderable(entity);
        static thread_local virtual_cache<setup_bones_t> cache;
        const auto setup = cached_virtual(renderable, 13, client_module, cache);
        if (!setup)
            return;
        const float time = global_vars ? global_vars->current_time : 0.f;
        if (!setup(renderable, bones, 256, 0x000FFF00, time))
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

bool read_player_info(int index, unsigned char (&info)[512]) {
    static thread_local virtual_cache<get_player_info_t> cache;
    const auto get_info = cached_virtual(engine, 8, engine_module, cache);
    return get_info && get_info(engine, index, info);
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
    static constexpr int view_matrix_pattern_bytes[] = {
        0x0F, 0x10, 0x05, -1, -1, -1, -1, 0x8D, 0x85, -1, -1, -1, -1, 0xB9
    };
    static constexpr int studio_pattern[] = {
        0x8B, 0xB6, -1, -1, -1, -1, 0x85, 0xF6, 0x74, 0x05,
        0x83, 0x3E, 0x00, 0x75, 0x02, 0x33, 0xF6, 0xF3, 0x0F, 0x10, 0x44, 0x24
    };
    const auto* global_address = find_pattern(client_module, global_pattern);
    const auto* weapon_address = find_pattern(client_module, weapon_system_pattern);
    view_matrix_pattern = find_pattern(client_module, view_matrix_pattern_bytes);
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

// Declared at global scope instead of pulling world_render.hpp in: that header
// sets IMGUI_DEFINE_MATH_OPERATORS after imgui.h is already included by this
// translation unit's config headers, and imgui_internal.h rejects that order.
namespace world_visuals {
void run_thirdperson();
}

namespace visuals_source {
void capture_now(frame_data& output, float alpha_blend);
void __fastcall hooked_frame_stage(void* self, void*, int stage);

bool install_frame_stage_hook();

bool initialize() {
    if (core_ready.load(std::memory_order_acquire)) {
        install_frame_stage_hook();
        install_viewmodel_hook();
        return true;
    }
    const ULONGLONG now = GetTickCount64();
    if (now < next_initialize_attempt.load(std::memory_order_relaxed))
        return false;
    next_initialize_attempt.store(now + 2000, std::memory_order_relaxed);
    std::scoped_lock lock(initialize_mutex);
    if (core_ready.load(std::memory_order_relaxed)) {
        install_frame_stage_hook();
        install_viewmodel_hook();
        return true;
    }
    if (!acquire())
        return false;
    const bool acquired = client && engine && entity_list && nv.team >= 0 && nv.health >= 0 &&
        nv.life_state >= 0 && nv.origin >= 0;
    core_ready.store(acquired, std::memory_order_release);
    if (acquired) {
        install_frame_stage_hook();
        install_viewmodel_hook();
    }
    return acquired;
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

int player_side_for_sound(int entity_index) {
    if (entity_index < 1 || entity_index > 64 || !is_in_game())
        return -1;
    void* player = entity_at(entity_index);
    void* local_player = entity_at(local_index());
    if (!player || !local_player || player == local_player ||
        value<unsigned char>(player, nv.life_state, 0) != 0 ||
        value<int>(player, nv.health, 0) <= 0) {
        return -1;
    }
    const int team = value<int>(player, nv.team, 0);
    const int local_team = value<int>(local_player, nv.team, 0);
    if ((team != 2 && team != 3) || (local_team != 2 && local_team != 3))
        return -1;
    return team == local_team ? 1 : 0;
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

bool get_local_view_state(local_view_state& output, bool include_accuracy) {
    output = {};
    if (!is_in_game())
        return false;
    void* local = entity_at(local_index());
    if (!local)
        return false;
    output.available = true;
    output.alive = value<unsigned char>(local, nv.life_state, 1) == 0 &&
        value<int>(local, nv.health, 0) > 0;
    output.scoped = value<bool>(local, nv.scoped, false);
    output.origin = value<vector3>(local, nv.origin);
    const vector3 view_offset = value<vector3>(local, nv.view_offset, { 0.f, 0.f, 64.f });
    output.eye_origin = { output.origin.x + view_offset.x, output.origin.y + view_offset.y,
        output.origin.z + view_offset.z };
    output.aim_punch = value<vector3>(local, nv.aim_punch);
    output.view_punch = value<vector3>(local, nv.view_punch);
    // Self > Models reads the active weapon on every call: the Grenade
    // condition is a weapon state, not an accuracy request.
    void* weapon = entity_from_handle(value<unsigned int>(local, nv.active_weapon, 0xFFFFFFFFu));
    if (weapon) {
        const int definition = item_definition(weapon);
        output.weapon_id = definition;
        // Same 43..48 blast of grenade item definitions the misc runtime uses.
        output.holding_grenade = definition >= 43 && definition <= 48;
        // World > Crosshairs gates its force/hide conditions on the weapon class;
        // 42/59 are the two knife definitions, 49 is the C4 and the four sniper
        // definitions match the engine's WEAPONCLASS_SNIPERRIFLE set.
        output.holding_knife = definition == 42 || definition == 59;
        output.holding_c4 = definition == 49;
        output.holding_sniper = definition == 9 || definition == 11 ||
            definition == 38 || definition == 40;
        output.pin_pulled = output.holding_grenade && value<bool>(weapon, nv.pin_pulled, false);
        output.zoom_level = std::clamp(value<int>(weapon, nv.zoom_level, 1), 0, 2);
    output.player_fov = value<int>(local, nv.player_fov, 0);
    }
    if (weapon && include_accuracy) {
        // Netvar read only: GetInaccuracy/GetSpread live at a fixed vtable slot,
        // and calling a wrong slot on the wrong class is exactly what crashed.
        const float penalty = value<float>(weapon, nv.accuracy_penalty, 0.f);
        if (std::isfinite(penalty))
            output.inaccuracy = std::clamp(penalty, 0.f, 1.f);
        output.spread = 0.f;
    }
    return true;
}

// Writing into a read-only page (client.dll's .rdata, a string literal, a const
// table) is an instant access violation. Every write that goes through a resolved
// pointer checks this first now.
bool writable_range(const void* address, std::size_t size) {
    if (!address || !size)
        return false;
    auto current = reinterpret_cast<std::uintptr_t>(address);
    const auto end = current + size;
    if (end < current)
        return false;
    constexpr DWORD writable = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE |
        PAGE_EXECUTE_WRITECOPY;
    while (current < end) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<const void*>(current), &info, sizeof(info)))
            return false;
        if (info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0 ||
            (info.Protect & writable) == 0)
            return false;
        const auto region_end = reinterpret_cast<std::uintptr_t>(info.BaseAddress) +
            static_cast<std::uintptr_t>(info.RegionSize);
        if (region_end <= current)
            return false;
        current = region_end;
    }
    return true;
}

// World > HUD > Radar. The game radar only lists entities flagged as spotted,
// so "reveal" sets those flags instead of painting a second radar on screen.
void reveal_entity_on_radar(void* entity, bool spotted, int local_index) {
    if (!entity || nv.spotted < 0)
        return;
    auto* spotted_slot = static_cast<unsigned char*>(entity) + nv.spotted;
    if (!writable_range(spotted_slot, sizeof(spotted)))
        return;
    std::memcpy(spotted_slot, &spotted, sizeof(spotted));
    // Fatality only ever touches m_bSpotted; the spotted-by mask is a separate
    // networked field and writing it was an extra way to corrupt entity memory.
    (void)local_index;
}

// ---------------------------------------------------------------------------
//  World > Viewmodel (position / angles / visual recoil)
// ---------------------------------------------------------------------------
// main applies the viewmodel offsets inside CBaseViewModel::CalcViewModelView,
// on the eye position it is handed - not on the entity origin and not through
// viewmodel_offset_* (the game clamps those to +-2 and rewrites the entity
// origin every frame). Same hook here, so all three overrides actually land.
using calc_viewmodel_view_t = void(__fastcall*)(void*, void*, void*, vector3&, vector3&);
calc_viewmodel_view_t original_calc_viewmodel_view = nullptr;
bool viewmodel_hook_installed = false;

void angle_vectors(const vector3& angles, vector3& forward, vector3& right, vector3& up) {
    constexpr float pi = 3.14159265358979323846f;
    const float pitch = angles.x * (pi / 180.f);
    const float yaw = angles.y * (pi / 180.f);
    const float sp = std::sin(pitch), cp = std::cos(pitch);
    const float sy = std::sin(yaw), cy = std::cos(yaw);
    forward = { cp * cy, cp * sy, -sp };
    right = { -sy, cy, 0.f };
    up = { right.y * forward.z - right.z * forward.y,
        right.z * forward.x - right.x * forward.z,
        right.x * forward.y - right.y * forward.x };
}

void __fastcall hooked_calc_viewmodel_view(void* self, void* edx, void* owner, vector3& eye_pos,
                                           vector3& eye_angles) {
    AIRFLOW_DEBUG_STAGE("visuals.viewmodel_view");
    if (owner && is_local_entity(owner)) {
        const auto& page = player_visuals::world_settings();
        local_view_state state{};
        const bool state_valid = get_local_view_state(state);
        vector3 forward{}, right{}, up{};
        angle_vectors(eye_angles, forward, right, up);
        const float offset_x = std::clamp(g_cfg->world.m_viewmodel_offset_x, -20.f, 20.f);
        const float offset_y = std::clamp(g_cfg->world.m_viewmodel_offset_y, -20.f, 20.f);
        const float offset_z = std::clamp(g_cfg->world.m_viewmodel_offset_z, -20.f, 20.f);
        if (page.viewmodel_position_override &&
            (std::fabs(offset_x) > 0.001f || std::fabs(offset_y) > 0.001f ||
                std::fabs(offset_z) > 0.001f) &&
            writable_range(&eye_pos, sizeof(vector3))) {
            eye_pos.x += right.x * offset_x + forward.x * offset_y + up.x * offset_z;
            eye_pos.y += right.y * offset_x + forward.y * offset_y + up.y * offset_z;
            eye_pos.z += right.z * offset_x + forward.z * offset_y + up.z * offset_z;
        }
        if (page.viewmodel_angles_override && writable_range(&eye_angles, sizeof(vector3))) {
            eye_angles.x += std::clamp(page.viewmodel_pitch, -89.f, 89.f);
            eye_angles.y += std::clamp(page.viewmodel_yaw, -180.f, 180.f);
            eye_angles.z = std::clamp(page.viewmodel_roll, -180.f, 180.f);
        }
        // World > Visual Recoil: Enabled keeps the engine's kick (see the
        // OverrideView path for the same inversion).
        if (!g_cfg->world.m_visual_recoil && state_valid && state.alive) {
            eye_angles.x -= state.aim_punch.x * 0.9f + state.view_punch.x;
            eye_angles.y -= state.aim_punch.y * 0.9f + state.view_punch.y;
        }
    }
    if (original_calc_viewmodel_view)
        original_calc_viewmodel_view(self, edx, owner, eye_pos, eye_angles);
}

// A loose prologue (55 8B EC 83 EC 64 56 57) shows up more than once in an 80 MB
// client.dll; hooking the first hit can patch an unrelated function. Only accept a
// signature that resolves to exactly one place.
template <std::size_t N>
const std::uint8_t* find_unique_pattern(HMODULE module, const int (&pattern)[N]) {
    const std::uint8_t* text = nullptr;
    std::size_t size = 0;
    if (!module_text(module, text, size) || N > size)
        return nullptr;
    const std::uint8_t* match = nullptr;
    for (std::size_t offset = 0; offset + N <= size; ++offset) {
        bool matches = true;
        for (std::size_t i = 0; i < N; ++i) {
            if (pattern[i] >= 0 && text[offset + i] != static_cast<std::uint8_t>(pattern[i])) {
                matches = false;
                break;
            }
        }
        if (!matches)
            continue;
        if (match)
            return nullptr;
        match = text + offset;
    }
    return match;
}

bool install_viewmodel_hook() {
    if (viewmodel_hook_installed)
        return true;
    if (!client_module)
        return false;
    // Two prologues across the client builds main supports.
    static constexpr int viewmodel_pattern_a[] = {
        0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x64, 0x56, 0x57
    };
    static constexpr int viewmodel_pattern_b[] = {
        0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x58, 0x56, 0x57
    };
    const std::uint8_t* target = find_unique_pattern(client_module, viewmodel_pattern_a);
    if (!target)
        target = find_unique_pattern(client_module, viewmodel_pattern_b);
    if (!target) {
        // Not found, or ambiguous: leave the game untouched instead of patching a
        // random function and crashing somewhere else later.
        AIRFLOW_DEBUG_LOG("viewmodel hook: CalcViewModelView signature not unique");
        return false;
    }
    if (MH_Initialize() != MH_OK && MH_Initialize() != MH_ERROR_ALREADY_INITIALIZED)
        return false;
    void* address = const_cast<std::uint8_t*>(target);
    if (MH_CreateHook(address, reinterpret_cast<void*>(&hooked_calc_viewmodel_view),
        reinterpret_cast<void**>(&original_calc_viewmodel_view)) != MH_OK)
        return false;
    if (MH_EnableHook(address) != MH_OK)
        return false;
    viewmodel_hook_installed = true;
    return true;
}

// World > HUD > Radar. Fatality reveals enemies in PostDataUpdate by setting
// CBasePlayer::m_bSpotted, and it writes that flag on every single network
// update. It has to: m_bSpotted is a networked property, so the server's copy
// lands on the next update and a write-once shortcut loses the reveal again -
// the old "flagged" cache here only ever wrote the flag once, which is why the
// radar kept showing nothing.
void update_radar_reveal() {
    if (!is_in_game())
        return;
    static std::array<bool, 65> flagged{};
    const auto& options = player_visuals::world_settings().radar_options;
    const bool want_enemies = options[0];
    const bool want_hostages = options[1];
    const bool want_bomb = options[2];
    const bool any_flagged = std::any_of(flagged.begin(), flagged.end(),
        [](bool value) { return value; });
    if (!want_enemies && !want_hostages && !want_bomb && !any_flagged)
        return;
    void* local = entity_at(local_index());
    if (!local)
        return;
    const int local_team = value<int>(local, nv.team, 0);
    const int local_index_value = local_index();
    // The whole walking loop is [1, 64]; hostages and the C4 share the range.
    for (int index = 1; index <= 64; ++index) {
        void* entity = entity_at(index);
        if (!entity) {
            flagged[index] = false;
            continue;
        }
        const client_class_t* client_class = entity_class(entity);
        if (!client_class || !readable_range(client_class->name, 1))
            continue;
        const char* class_name = client_class->name;
        bool wanted = false;
        if (std::strstr(class_name, "CSPlayer") != nullptr) {
            const int team = value<int>(entity, nv.team, 0);
            wanted = want_enemies && team != 0 && team != local_team &&
                value<unsigned char>(entity, nv.life_state, 1) == 0;
        } else if (std::strstr(class_name, "Hostage") != nullptr) {
            wanted = want_hostages;
        } else if (std::strstr(class_name, "PlantedC4") != nullptr) {
            wanted = want_bomb;
        } else {
            continue;
        }
        if (wanted) {
            reveal_entity_on_radar(entity, true, local_index_value);
            flagged[index] = true;
        } else if (flagged[index]) {
            // Only clear what we set ourselves.
            reveal_entity_on_radar(entity, false, local_index_value);
            flagged[index] = false;
        }
    }
}

std::vector<int> local_spectators() {
    std::vector<int> result;
    if (!is_in_game() || nv.observer_target < 0)
        return result;
    const int local = local_index();
    void* local_entity = entity_at(local);
    if (!local_entity)
        return result;
    for (int i = 0; i < 64; ++i) {
        if (i == local)
            continue;
        void* entity = entity_at(i);
        if (!entity)
            continue;
        if (value<unsigned char>(entity, nv.life_state, 0) == 0)
            continue;   // alive players are not spectating
        void* target = entity_from_handle(value<unsigned int>(entity, nv.observer_target,
            0xFFFFFFFFu));
        if (target && target == local_entity)
            result.push_back(i);
    }
    return result;
}

void set_flash_removed(bool enabled) {
    static void* tracked_entity = nullptr;
    static float original_alpha = 255.f;
    static bool has_original = false;
    void* local = is_in_game() ? entity_at(local_index()) : nullptr;
    if (tracked_entity && (tracked_entity != local || !enabled) && has_original &&
        nv.flash_max_alpha >= 0 &&
        writable_range(reinterpret_cast<unsigned char*>(tracked_entity) + nv.flash_max_alpha,
            sizeof(float))) {
        std::memcpy(reinterpret_cast<unsigned char*>(tracked_entity) + nv.flash_max_alpha,
            &original_alpha, sizeof(original_alpha));
        tracked_entity = nullptr;
        has_original = false;
    }
    if (!enabled || !local || nv.flash_max_alpha < 0)
        return;
    if (tracked_entity != local || !has_original) {
        tracked_entity = local;
        original_alpha = value<float>(local, nv.flash_max_alpha, 255.f);
        has_original = true;
    }
    const float zero = 0.f;
    if (writable_range(reinterpret_cast<unsigned char*>(local) + nv.flash_max_alpha, sizeof(zero)))
        std::memcpy(reinterpret_cast<unsigned char*>(local) + nv.flash_max_alpha, &zero, sizeof(zero));
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

bool entity_exists(int entity_index) {
    return entity_index > 0 && entity_index <= 64 && entity_at(entity_index) != nullptr;
}

void capture_world(frame_data& output, const vector3& local_origin) {
    const auto& world_settings = player_visuals::world_settings();
    const bool radar_reveal = std::any_of(world_settings.radar_options.begin(),
        world_settings.radar_options.end(), [](bool value) { return value; });
    if (!world_settings.bomb.enabled && !world_settings.weapon.enabled &&
        !world_settings.hostage.enabled && !world_settings.grenade.enabled &&
        !world_settings.grenade.smoke_radius && !world_settings.grenade.inferno_radius &&
        !world_settings.grenade_trajectory && !world_settings.grenade_proximity_warnings && !radar_reveal)
        return;
    output.world.reserve(64);
    // Iterate to the engine's highest live entity instead of always probing
    // 2048 slots; a normal map keeps ~200-400.
    const int last_index = highest_entity_index();
    for (int index = 65; index <= last_index; ++index) {
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
        const bool hostage = std::strstr(class_name, "Hostage") != nullptr;
        const bool window = std::strstr(class_name, "BreakableSurface") != nullptr ||
            std::strstr(class_name, "FuncBreakable") != nullptr;
        const int definition = item_definition(entity);
        const bool bomb_weapon = definition == 49;
        if (definition <= 0 && !smoke_projectile && !inferno && !planted_c4 &&
            !projectile && !hostage && !window)
            continue;

        const auto& filters = player_visuals::world_settings();
        if (((planted_c4 || bomb_weapon) && !filters.bomb.enabled && !filters.radar_options[2]) ||
            (definition > 0 && !projectile && !planted_c4 && !bomb_weapon && !filters.weapon.enabled) ||
            (hostage && !filters.hostage.enabled && !filters.radar_options[1]) || window ||
            ((projectile || smoke_projectile || inferno) && !filters.grenade.enabled &&
                !(inferno && filters.grenade.inferno_radius) &&
                !(smoke_projectile && filters.grenade.smoke_radius) &&
                !filters.grenade_trajectory && !filters.grenade_proximity_warnings))
            continue;

        const bool smoke_active = smoke_projectile && value<bool>(entity, nv.smoke_effect, false);
        if (projectile && !smoke_active && nv.explode_tick >= 0 &&
            value<bool>(entity, nv.explode_tick + 4, false)) {
            continue;
        }
        const bool special = smoke_projectile || inferno || planted_c4 || projectile || hostage || window;
        const unsigned int owner = value<unsigned int>(entity, nv.owner, 0xFFFFFFFFu);
        if (!special && owner != 0 && owner != 0xFFFFFFFFu)
            continue;

        world_data data;
        data.index = index;
        data.weapon_id = definition;
        data.kind = inferno ? visuals_source::world_inferno :
            ((planted_c4 || bomb_weapon) ? visuals_source::world_planted_c4 :
            (hostage ? visuals_source::world_hostage :
            (window ? visuals_source::world_window :
            (smoke_active ? visuals_source::world_smoke :
                (projectile || smoke_projectile ? visuals_source::world_projectile
                    : visuals_source::world_weapon)))));
        data.origin = abs_origin(entity, value<vector3>(entity, nv.origin));
        data.velocity = value<vector3>(entity, nv.velocity);
        data.mins = value<vector3>(entity, nv.mins, hostage ? vector3{ -12.f, -12.f, 0.f } : vector3{ -4.f, -4.f, -4.f });
        data.maxs = value<vector3>(entity, nv.maxs,
            hostage ? vector3{ 12.f, 12.f, 72.f } : vector3{ 4.f, 4.f, 4.f });
        data.distance = vector_length({ data.origin.x - local_origin.x,
            data.origin.y - local_origin.y, data.origin.z - local_origin.z });
        data.alpha = 1.f - std::clamp(data.distance / 1500.f, 0.f, 1.f);
        if (data.alpha <= 0.f)
            continue;
        data.name = (planted_c4 || bomb_weapon) ? "C4" : (hostage ? "Hostage" :
            (window ? "Breakable" : (definition > 0 ? weapon_name(definition) : "Grenade")));
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

void capture_local_player(frame_data& output, void* local, int local_team) {
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
    data.distance = 0.f;
    data.mins = value<vector3>(local, nv.mins, { -16.f, -16.f, 0.f });
    data.maxs = value<vector3>(local, nv.maxs, { 16.f, 16.f, 72.f });
    if (nv.pose_parameters >= 0) {
        data.poses_valid = true;
        data.stand_pose = value<float>(local, nv.pose_parameters + 4, 0.f);
        data.body_pitch = value<float>(local, nv.pose_parameters + 4 * 12, 0.f);
    }
    copy_text(data.name, player_name(output.local_index));
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
    void* local_weapon = entity_from_handle(value<unsigned int>(local, nv.active_weapon, 0xFFFFFFFFu));
    capture_weapon(local_weapon, settings, data);
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

void reset_frame(frame_data& output) {
    // Clearing keeps the vector capacity owned by the recycled frame buffer.
    // Assigning an empty struct here would free it every Present.
    output.in_game = false;
    output.has_view_matrix = false;
    output.local_alive = false;
    output.local_index = 0;
    output.local_origin = {};
    output.view_angles = {};
    std::memset(output.view_matrix, 0, sizeof(output.view_matrix));
    output.players.clear();
    output.world.clear();
}

void capture_now(frame_data& output, float alpha_blend) {
    capture_memory_cache_scope memory_cache;
    reset_frame(output);
    if (!is_in_game()) {
        clear_player_cache();
        return;
    }

    const ULONGLONG now = GetTickCount64();
    const float delta_time = last_capture_time
        ? std::clamp(static_cast<float>(now - last_capture_time) / 1000.f, 0.f, 0.25f)
        : 1.f / 60.f;
    last_capture_time = now;
    if (alpha_blend < 0.f) {
        // Present is only a fallback when FrameStageNotify is unavailable.
        // Main uses animation_speed * 5; animation_speed itself is delta * 5.
        airflow_animation_speed();
        alpha_blend = std::clamp(delta_time * 25.f, 0.f, 1.f);
    }

    output.in_game = true;
    output.local_index = local_index();
    void* local = entity_at(output.local_index);
    if (!local) {
        clear_player_cache();
        output.in_game = false;
        return;
    }

    update_view_projection(output);

    output.local_alive = value<unsigned char>(local, nv.life_state, 0) == 0 &&
        value<int>(local, nv.health, 0) > 0;
    const int local_team = value<int>(local, nv.team, 0);
    const vector3 local_origin = abs_origin(local, value<vector3>(local, nv.origin));
    output.local_origin = local_origin;
    output.view_angles = view_angles();

    // This is the main branch's complete value-only dormant/fade record. It is
    // intentionally separate from chams history and never stores entity pointers.
    const auto append_cached = [&](int index, bool is_dormant, bool dead) {
        if (index < 1 || index > 64)
            return;
        auto& cached = player_cache[index];
        if (!cached.valid)
            return;
        if (cached.data.side != 0 && cached.data.side != 1) {
            cached.valid = false;
            return;
        }
        const ULONGLONG age = now >= cached.last_seen ? now - cached.last_seen : 5001;
        const bool has_game_time = global_vars && cached.game_time > 0.f &&
            std::isfinite(cached.game_time) && std::isfinite(global_vars->current_time);
        const bool expired = is_dormant && (has_game_time
            ? std::fabs(global_vars->current_time - cached.game_time) > 5.f
            : age > 5000);
        const float target = dead || expired ? 0.f : (is_dormant ? 0.45f : 1.f);
        cached.data.alpha += (target - cached.data.alpha) * alpha_blend;
        cached.data.alpha = std::clamp(cached.data.alpha, 0.f, 1.f);
        cached.data.dormant = is_dormant;
        if (dead)
            cached.data.health = 0;
        const auto& side = player_visuals::settings(cached.data.side);
        if (cached.data.alpha > 0.01f && side.esp_enabled)
            output.players.push_back(cached.data);
        if (cached.data.alpha <= 0.01f && (dead || expired))
            cached.valid = false;
    };

    output.players.reserve(32);
    // Nothing consumes player records when both ESP sides are off and the
    // radar overlay does not need them; skip 64 entity probes per frame.
    const bool collect_players = player_visuals::settings(0).esp_enabled ||
        player_visuals::settings(1).esp_enabled || g_cfg->world.m_draw_radar;
    for (int index = 1; collect_players && index <= 64; ++index) {
        if (index == output.local_index)
            continue;
        void* player = entity_at(index);
        if (!player) {
            append_cached(index, false, true);
            continue;
        }
        const auto life_state = value<unsigned char>(player, nv.life_state, 0);
        const int health = value<int>(player, nv.health, 0);
        if (life_state != 0 || health <= 0) {
            append_cached(index, false, true);
            continue;
        }
        if (dormant(player)) {
            append_cached(index, true, false);
            continue;
        }

        auto& cached = player_cache[index];
        player_data data;
        data.index = index;
        data.team = value<int>(player, nv.team, 0);
        data.health = health;
        if (data.team != 2 && data.team != 3) {
            player_cache[index].valid = false;
            continue;
        }
        if (local_team != 2 && local_team != 3)
            continue;
        data.side = data.team == local_team ? 1 : 0;
        const auto& side_settings = player_visuals::settings(data.side);
        if (!side_settings.esp_enabled && !g_cfg->world.m_draw_radar) {
            // Chams has its own DME path; player data is collected only for
            // enabled ESP sides or the independent World radar overlay.
            player_cache[index].valid = false;
            continue;
        }

        data.origin = abs_origin(player, value<vector3>(player, nv.origin));
        data.mins = value<vector3>(player, nv.mins, { -16.f, -16.f, 0.f });
        data.maxs = value<vector3>(player, nv.maxs, { 16.f, 16.f, 72.f });
        if (nv.pose_parameters >= 0) {
            data.poses_valid = true;
            data.stand_pose = value<float>(player, nv.pose_parameters + 4, 0.f);
            data.body_pitch = value<float>(player, nv.pose_parameters + 4 * 12, 0.f);
        }
        data.distance = vector_length({ data.origin.x - local_origin.x,
            data.origin.y - local_origin.y, data.origin.z - local_origin.z }) * 0.0254f;
        const bool refresh_name = !cached.valid || now < cached.last_name_query ||
            now - cached.last_name_query >= 2000;
        if (refresh_name && side_settings.main[player_visuals::main_name]) {
            copy_text(data.name, player_name(index));
            cached.last_name_query = now;
        } else if (cached.valid) {
            // Preserve a previously captured name while the element is off;
            // enabling it during a dormant fade must not erase the label.
            std::memcpy(data.name, cached.data.name, sizeof(data.name));
        }
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
        // A handle lookup per player per frame is only worth it for the flag.
        data.hostage = side_settings.flags[player_visuals::flag_hostage] &&
            entity_from_handle(value<unsigned int>(player, nv.carried_hostage, 0xFFFFFFFFu)) != nullptr;
        void* weapon = entity_from_handle(value<unsigned int>(player, nv.active_weapon, 0xFFFFFFFFu));
        capture_weapon(weapon, side_settings, data);
        if (side_settings.esp_enabled &&
            (side_settings.flags[player_visuals::flag_bomb] ||
             side_settings.weapon[player_visuals::weapon_bomb] ||
             side_settings.weapon[player_visuals::weapon_taser])) {
            scan_inventory(player, data);
        }
        data.bomb = data.bomb || data.weapon_id == 49;
        data.taser = data.taser || data.weapon_id == 31;
        collect_model_geometry(player, value<int>(player, nv.hitbox_set, 0), data,
            side_settings.esp_enabled && side_settings.main[player_visuals::main_skeleton],
            side_settings.esp_enabled && side_settings.aimbot[player_visuals::aimbot_hitboxes]);

        data.dormant = false;
        data.alpha = cached.valid && cached.data.side == data.side
            ? cached.data.alpha + (1.f - cached.data.alpha) * alpha_blend
            : 1.f;
        data.alpha = std::clamp(data.alpha, 0.f, 1.f);
        cached.data = data;
        cached.last_seen = now;
        cached.game_time = global_vars ? global_vars->current_time : 0.f;
        cached.valid = true;
        if (side_settings.esp_enabled || g_cfg->world.m_draw_radar)
            output.players.push_back(std::move(data));
    }

    if (player_visuals::self_esp_settings().esp_enabled)
        capture_local_player(output, local, local_team);
    capture_world(output, local_origin);
}

void __fastcall hooked_frame_stage(void* self, void*, int stage) {
    AIRFLOW_DEBUG_STAGE("visuals.frame_stage");
    if (stage == 5) // FRAME_RENDER_START: main updates animation timing here.
        airflow_animation_speed();
    if (original_frame_stage)
        original_frame_stage(self, stage);
    // Fatality: the radar flag is written in PostDataUpdate (FRAME_NET_UPDATE_POSTDATAUPDATE_END
    // here) and thirdperson runs at FRAME_RENDER_END.
    if (stage == 3)
        update_radar_reveal();
    if (stage == 6)
        ::world_visuals::run_thirdperson();
    if (stage != 6 || !frame_stage_hook_ready.load(std::memory_order_acquire))
        return;

    // Reuse the buffer the renderer has already released. The owner keeps the
    // previous snapshot published until the new one is ready, so capture_shared()
    // never observes an empty stage.
    std::shared_ptr<frame_data> next;
    {
        std::lock_guard<std::mutex> lock(staged_frame_mutex);
        if (spare_frame && spare_frame.use_count() == 1)
            next = std::move(spare_frame);
        spare_frame.reset();
    }
    if (!next)
        next = std::make_shared<frame_data>();
    const float blend = std::clamp(current_airflow_animation_speed() * 5.f, 0.f, 1.f);
    // FRAME_RENDER_START already advanced the timer; only read its value here.
    capture_now(*next, blend);
    {
        std::lock_guard<std::mutex> lock(staged_frame_mutex);
        spare_frame = std::move(staged_frame);
        staged_frame = std::move(next);
    }
}

bool install_frame_stage_hook() {
    if (frame_stage_hook_ready.load(std::memory_order_acquire))
        return true;
    static std::atomic<ULONGLONG> next_attempt = 0;
    const ULONGLONG now = GetTickCount64();
    if (now < next_attempt.load(std::memory_order_relaxed))
        return false;
    next_attempt.store(now + 2000, std::memory_order_relaxed);
    const auto target = virtual_function<frame_stage_t>(client, 37);
    if (!target || !function_in_module(client_module, reinterpret_cast<const void*>(target)))
        return false;
    const MH_STATUS init_status = MH_Initialize();
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED)
        return false;
    auto* address = reinterpret_cast<void*>(target);
    if (MH_CreateHook(address, reinterpret_cast<void*>(&hooked_frame_stage),
                      reinterpret_cast<void**>(&original_frame_stage)) != MH_OK) {
        return false;
    }
    if (MH_EnableHook(address) != MH_OK) {
        MH_RemoveHook(address);
        original_frame_stage = nullptr;
        return false;
    }
    frame_stage_hook_ready.store(true, std::memory_order_release);
    return true;
}

std::shared_ptr<const frame_data> capture_shared() {
    std::shared_ptr<frame_data> snapshot;
    if (frame_stage_hook_ready.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> lock(staged_frame_mutex);
        snapshot = staged_frame;
    }
    if (snapshot) {
        // Entity fields are published once. Only the view used for projection
        // is refreshed, matching main's FrameStage/render split.
        refresh_render_view(*snapshot);
        return snapshot;
    }

    // Compatibility path for a client where FrameStageNotify could not be
    // validated. It is not a second renderer or a second persistent pipeline.
    auto fresh = std::make_shared<frame_data>();
    capture_now(*fresh, -1.f);
    return fresh;
}

void capture(frame_data& output) {
    const auto snapshot = capture_shared();
    if (snapshot)
        output = *snapshot;
    else
        output = {};
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
