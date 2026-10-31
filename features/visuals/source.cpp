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
    int weapon_id = -1;
    int clip = -1;
    int reserve_ammo = -1;
    int next_primary_attack = -1;
    int postpone_fire_ready = -1;
    int reloading = -1;
    int pin_pulled = -1;
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
    return *reinterpret_cast<T**>(reinterpret_cast<const void*>(address));
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
    nv.weapon_id = table_offset(classes, { "DT_BaseAttributableItem" }, "m_iItemDefinitionIndex");
    nv.clip = table_offset(classes, { "DT_BaseCombatWeapon" }, "m_iClip1");
    nv.reserve_ammo = table_offset(classes, { "DT_BaseCombatWeapon" }, "m_iPrimaryReserveAmmoCount");
    nv.next_primary_attack = table_offset(classes, { "DT_BaseCombatWeapon" }, "m_flNextPrimaryAttack");
    nv.postpone_fire_ready = table_offset(classes, { "DT_WeaponCSBase" }, "m_flPostponeFireReadyTime");
    nv.reloading = table_offset(classes, { "DT_WeaponCSBase", "DT_BaseCombatWeapon" }, "m_bInReload");
    nv.pin_pulled = table_offset(classes, { "DT_BaseCSGrenade" }, "m_bPinPulled");
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
using world_matrix_t = const float* (__thiscall*)(void*);
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
    if (studio->bone_count <= 0 || studio->bone_count > 128 || studio->bone_index <= 0)
        return;
    const auto setup = virtual_function<setup_bones_t>(entity, 197);
    if (!setup || !function_in_module(client_module, reinterpret_cast<const void*>(setup)))
        return;
    matrix3x4_t bones[128]{};
    const float time = global_vars ? global_vars->current_time : 0.f;
    if (!setup(entity, bones, 128, 0x0007FF00, time))
        return;

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

std::string player_name(int index) {
    const auto get_info = virtual_function<get_player_info_t>(engine, 8);
    if (!get_info)
        return {};
    // The final Source 1 player_info_t is below 512 bytes. Name follows the two
    // 64-bit fields at offset 16; the oversized buffer prevents layout overflow.
    unsigned char info[512]{};
    if (!get_info(engine, index, info))
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
    if (!checked_virtual<get_all_classes_t>(client, 8, client_module) ||
        !checked_virtual<entity_at_t>(entity_list, 3, client_module) ||
        !checked_virtual<entity_from_handle_t>(entity_list, 4, client_module) ||
        !checked_virtual<local_player_t>(engine, 12, engine_module) ||
        !checked_virtual<engine_bool_t>(engine, 26, engine_module) ||
        !checked_virtual<get_player_info_t>(engine, 8, engine_module) ||
        !checked_virtual<world_matrix_t>(engine, 37, engine_module)) {
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
    const auto function = virtual_function<engine_bool_t>(engine, 26);
    return function && function(engine);
}

int player_side(int entity_index) {
    if (entity_index < 1 || entity_index > 64 || !is_in_game())
        return -1;
    const int local = local_index();
    if (entity_index == local)
        return -1;
    void* player = entity_at(entity_index);
    void* local_player = entity_at(local);
    if (!player || !local_player || dormant(player))
        return -1;
    if (value<unsigned char>(player, nv.life_state, 0) != 0 || value<int>(player, nv.health, 0) <= 0)
        return -1;
    const int team = value<int>(player, nv.team, 0);
    const int local_team = value<int>(local_player, nv.team, 0);
    if ((team != 2 && team != 3) || (local_team != 2 && local_team != 3))
        return -1;
    return team == local_team ? 1 : 0;
}

void capture(frame_data& output) {
    output = {};
    if (!is_in_game())
        return;
    output.in_game = true;
    output.local_index = local_index();
    void* local = entity_at(output.local_index);
    if (!local) {
        output.in_game = false;
        return;
    }
    if (const auto matrix_fn = virtual_function<world_matrix_t>(engine, 37)) {
        const float* matrix = matrix_fn(engine);
        if (matrix) {
            std::memcpy(output.view_matrix, matrix, sizeof(output.view_matrix));
            output.has_view_matrix = true;
        }
    }

    const int local_team = value<int>(local, nv.team, 0);
    const vector3 local_origin = value<vector3>(local, nv.origin);
    void* local_weapon = entity_from_handle(value<unsigned int>(local, nv.active_weapon, 0xFFFFFFFFu));
    output.players.reserve(32);
    for (int index = 1; index <= 64; ++index) {
        if (index == output.local_index)
            continue;
        void* player = entity_at(index);
        if (!player || dormant(player) || value<unsigned char>(player, nv.life_state, 0) != 0)
            continue;
        player_data data;
        data.index = index;
        data.team = value<int>(player, nv.team, 0);
        data.health = value<int>(player, nv.health, 0);
        if (data.health <= 0 || (data.team != 2 && data.team != 3))
            continue;
        if (local_team != 2 && local_team != 3)
            continue;
        data.side = data.team == local_team ? 1 : 0;
        const auto& side_settings = player_visuals::settings(data.side);
        if (!side_settings.esp_enabled)
            continue;
        data.origin = value<vector3>(player, nv.origin);
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
        if (side_settings.aimbot[player_visuals::aimbot_hit_chance] ||
            side_settings.flags[player_visuals::flag_vulnerable]) {
            data.hit_chance = calculate_hit_chance(local_weapon, data.distance * 39.3701f, data.immunity);
            data.vulnerable = data.immunity_valid && !data.immunity && data.hit_chance >= 5.f;
        }
        collect_model_geometry(player, value<int>(player, nv.hitbox_set, 0), data,
            side_settings.main[player_visuals::main_skeleton],
            side_settings.aimbot[player_visuals::aimbot_hitboxes]);
        output.players.push_back(std::move(data));
    }

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
