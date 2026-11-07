#include "../misc.hpp"
#include "gameplay.hpp"
#include "source_types.hpp"
#include "../../../sdk/config_system/config_system.hpp"
#include "../../visuals/world/event_log.hpp"

// Forward declared on purpose: world_render.hpp pulls the ImGui drawing helpers,
// which must not be included from the event runtime.
namespace world_visuals {
void push_death_notice(std::string text, bool assistant, bool by_local);
}
#include "../../visuals/world/world.hpp"
#include "../../visuals/sound_esp.hpp"
#include "../../visuals/esp/esp.hpp"

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <string_view>

namespace {
using namespace misc_runtime;
using create_interface_fn = void* (__cdecl*)(const char*, int*);

struct global_vars {
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

struct recv_table;
struct recv_prop {
    char* name;
    int type;
    int flags;
    int buffer_size;
    int inside_array;
    const void* extra;
    recv_prop* array_prop;
    void* array_length_proxy;
    void* proxy;
    void* data_table_proxy;
    recv_table* data_table;
    int offset;
    int element_stride;
    int elements;
    const char* parent_array_prop_name;
};

struct recv_table {
    recv_prop* props;
    int prop_count;
    void* decoder;
    char* name;
    bool initialized;
    bool in_main_list;
};

struct client_class {
    void* create_fn;
    void* create_event_fn;
    char* name;
    recv_table* table;
    client_class* next;
    int class_id;
};

struct typedescription;
struct datamap {
    typedescription* desc;
    int size;
    const char* name;
    datamap* base;
};

struct typedescription {
    int type;
    const char* name;
    int offset[2];
    unsigned short size;
    short flags;
    const char* external_name;
    void* save_restore_ops;
    void* input_func;
    datamap* embedded;
    int bytes;
    typedescription* override_field;
    int override_count;
    float tolerance;
    unsigned char padding[8];
};

struct plane_t {
    vec3 normal;
    float distance;
    unsigned char type;
    unsigned char sign_bits;
    unsigned char padding[2];
};

struct surface_t {
    const char* name;
    short surface_props;
    unsigned short flags;
};

struct game_trace {
    vec3 start;
    vec3 end;
    plane_t plane;
    float fraction;
    int contents;
    unsigned short disp_flags;
    bool all_solid;
    bool start_solid;
    float fraction_left_solid;
    surface_t surface;
    unsigned int hitgroup;
    short physics_bone;
    unsigned short world_surface_index;
    void* entity;
    int hitbox;
};

struct ray {
    vec3_aligned start;
    vec3_aligned delta;
    vec3_aligned start_offset;
    vec3_aligned extents;
    const void* world_axis_transform;
    bool is_ray;
    bool is_swept;

    ray(const vec3& from, const vec3& to, const vec3& mins, const vec3& maxs) {
        const vec3 d = to - from;
        const vec3 e = (maxs - mins) * 0.5f;
        const vec3 offset = (maxs + mins) * 0.5f;
        const vec3 adjusted = from + offset;
        delta = { d.x, d.y, d.z, 0.f };
        extents = { e.x, e.y, e.z, 0.f };
        start_offset = { -offset.x, -offset.y, -offset.z, 0.f };
        start = { adjusted.x, adjusted.y, adjusted.z, 0.f };
        world_axis_transform = nullptr;
        is_ray = e.length() < 0.000001f;
        is_swept = d.length() != 0.f;
    }
};

#if defined(_WIN32) && !defined(_WIN64)
static_assert(offsetof(ray, is_ray) == 68);
static_assert(offsetof(game_trace, fraction) == 44);
static_assert(offsetof(game_trace, entity) == 76);
#endif

class ground_trace_filter {
public:
    virtual bool should_hit_entity(void*, int) { return false; }
    virtual int get_trace_type() const { return 0; }
};

struct netvar_offsets {
    int flags = -1;
    int velocity = -1;
    int origin = -1;
    int view_offset = -1;
    int active_weapon = -1;
    int weapon_id = -1;
    int pin_pulled = -1;
    int throw_strength = -1;
    int throw_time = -1;
    int team = -1;
    int health = -1;
    int armor = -1;
    int life_state = -1;
    int buy_zone = -1;
    int mins = -1;
    int maxs = -1;
};

HMODULE client_module = nullptr;
HMODULE engine_module = nullptr;
void* client = nullptr;
void* engine = nullptr;
void* entity_list = nullptr;
void* engine_trace = nullptr;
void* engine_sound = nullptr;
void* game_event_manager = nullptr;
void* weapon_system = nullptr;
void* world_input = nullptr;
void* cvar_interface = nullptr;
void* panel_interface = nullptr;
void* client_mode_instance = nullptr;
global_vars* globals = nullptr;
netvar_offsets nv;
int move_type_offset = -2;
int water_level_offset = -2;
bool core_ready = false;
bool create_move_installed = false;
bool sound_installed = false;
bool events_installed = false;
ULONGLONG next_acquire_attempt = 0;


constexpr unsigned int mask_solid = 0x1 | 0x2 | 0x8 | 0x4000 | 0x2000000;

bool readable_range(const void* address, std::size_t size) {
    if (!address || !size)
        return false;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
        return false;
    }
    const auto begin = reinterpret_cast<std::uintptr_t>(address);
    const auto end = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
    return begin <= end && size <= end - begin;
}

bool function_in_module(HMODULE module, const void* function) {
    if (!module || !function)
        return false;
    const auto* base = reinterpret_cast<const std::uint8_t*>(module);
    if (!readable_range(base, sizeof(IMAGE_DOS_HEADER)))
        return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (!readable_range(nt, sizeof(IMAGE_NT_HEADERS)) || nt->Signature != IMAGE_NT_SIGNATURE)
        return false;
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned int i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (std::memcmp(section[i].Name, ".text", 5) != 0)
            continue;
        const auto begin = reinterpret_cast<std::uintptr_t>(base + section[i].VirtualAddress);
        const auto end = begin + section[i].Misc.VirtualSize;
        const auto address = reinterpret_cast<std::uintptr_t>(function);
        return address >= begin && address < end;
    }
    return false;
}

template <std::size_t N>
const std::uint8_t* find_pattern(HMODULE module, const int (&pattern)[N]) {
    if (!module)
        return nullptr;
    const auto* base = reinterpret_cast<const std::uint8_t*>(module);
    if (!readable_range(base, sizeof(IMAGE_DOS_HEADER)))
        return nullptr;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || !readable_range(nt, sizeof(IMAGE_NT_HEADERS)) ||
        nt->Signature != IMAGE_NT_SIGNATURE) {
        return nullptr;
    }
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned int i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (std::memcmp(section[i].Name, ".text", 5) != 0)
            continue;
        const std::size_t size = section[i].Misc.VirtualSize;
        const auto* text = base + section[i].VirtualAddress;
        if (size < N || !readable_range(text, size))
            continue;
        for (std::size_t offset = 0; offset + N <= size; ++offset) {
            bool matches = true;
            for (std::size_t j = 0; j < N; ++j) {
                if (pattern[j] >= 0 && text[offset + j] != static_cast<std::uint8_t>(pattern[j])) {
                    matches = false;
                    break;
                }
            }
            if (matches)
                return text + offset;
        }
    }
    return nullptr;
}

void* create_interface(HMODULE module, const char* name) {
    if (!module)
        return nullptr;
    const auto factory = reinterpret_cast<create_interface_fn>(GetProcAddress(module, "CreateInterface"));
    return factory ? factory(name, nullptr) : nullptr;
}

template <typename T>
T virtual_function(void* instance, std::size_t index) {
    if (!instance || !readable_range(instance, sizeof(void*)))
        return nullptr;
    void** table = *reinterpret_cast<void***>(instance);
    if (!readable_range(table, (index + 1) * sizeof(void*)))
        return nullptr;
    return reinterpret_cast<T>(table[index]);
}

template <typename T>
T* absolute_pointer(const std::uint8_t* operand) {
    if (!operand || !readable_range(operand, sizeof(std::uintptr_t)))
        return nullptr;
    std::uintptr_t address = 0;
    std::memcpy(&address, operand, sizeof(address));
    if (!readable_range(reinterpret_cast<const void*>(address), sizeof(void*)))
        return nullptr;
    T* result = nullptr;
    std::memcpy(&result, reinterpret_cast<const void*>(address), sizeof(result));
    return result;
}

using get_all_classes_t = client_class* (__thiscall*)(void*);
using entity_at_t = void* (__thiscall*)(void*, int);
using entity_from_handle_t = void* (__thiscall*)(void*, unsigned int);
using local_player_t = int(__thiscall*)(void*);
using engine_bool_t = bool(__thiscall*)(void*);
using player_for_user_t = int(__thiscall*)(void*, int);
using get_player_info_t = bool(__thiscall*)(void*, int, void*);
using client_command_t = void(__thiscall*)(void*, const char*, const char*);
using trace_ray_t = void(__thiscall*)(void*, const ray&, unsigned int, void*, game_trace*);
using get_weapon_data_t = void* (__thiscall*)(void*, unsigned int);
using pred_desc_map_t = datamap* (__thiscall*)(void*);

get_all_classes_t get_all_classes = nullptr;
entity_at_t entity_at_fn = nullptr;
entity_from_handle_t entity_from_handle_fn = nullptr;
local_player_t local_player_fn = nullptr;
engine_bool_t is_in_game_fn = nullptr;
player_for_user_t player_for_user_fn = nullptr;
get_player_info_t get_player_info_fn = nullptr;
client_command_t client_command_fn = nullptr;
trace_ray_t trace_ray_fn = nullptr;
get_weapon_data_t get_weapon_data_fn = nullptr;

int find_offset(const recv_table* table, const char* name, int depth = 0) {
    if (!table || depth > 24 || !readable_range(table, sizeof(recv_table)) ||
        table->prop_count < 0 || table->prop_count > 4096 ||
        !readable_range(table->props, static_cast<std::size_t>(table->prop_count) * sizeof(recv_prop))) {
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

int table_offset(std::initializer_list<const char*> tables, const char* property) {
    for (const char* table_name : tables) {
        for (auto* current = get_all_classes ? get_all_classes(client) : nullptr;
             current; current = current->next) {
            if (!readable_range(current, sizeof(client_class)))
                break;
            if (!readable_range(current->table, sizeof(recv_table)) ||
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

void resolve_netvars() {
    nv.flags = table_offset({ "DT_CSPlayer", "DT_BasePlayer" }, "m_fFlags");
    nv.velocity = table_offset({ "DT_CSPlayer", "DT_BasePlayer" }, "m_vecVelocity[0]");
    if (nv.velocity < 0)
        nv.velocity = table_offset({ "DT_CSPlayer", "DT_BasePlayer" }, "m_vecVelocity");
    nv.origin = table_offset({ "DT_CSPlayer", "DT_BaseEntity" }, "m_vecOrigin");
    nv.view_offset = table_offset({ "DT_CSPlayer", "DT_BasePlayer" }, "m_vecViewOffset[0]");
    if (nv.view_offset < 0)
        nv.view_offset = table_offset({ "DT_CSPlayer", "DT_BasePlayer" }, "m_vecViewOffset");
    nv.active_weapon = table_offset({ "DT_CSPlayer", "DT_BaseCombatCharacter", "DT_BasePlayer" }, "m_hActiveWeapon");
    nv.weapon_id = table_offset({ "DT_BaseAttributableItem" }, "m_iItemDefinitionIndex");
    nv.pin_pulled = table_offset({ "DT_BaseCSGrenade" }, "m_bPinPulled");
    nv.throw_strength = table_offset({ "DT_BaseCSGrenade" }, "m_flThrowStrength");
    nv.throw_time = table_offset({ "DT_BaseCSGrenade" }, "m_fThrowTime");
    nv.team = table_offset({ "DT_CSPlayer", "DT_BaseEntity" }, "m_iTeamNum");
    nv.health = table_offset({ "DT_CSPlayer", "DT_BasePlayer" }, "m_iHealth");
    nv.armor = table_offset({ "DT_CSPlayer" }, "m_ArmorValue");
    nv.life_state = table_offset({ "DT_CSPlayer", "DT_BasePlayer" }, "m_lifeState");
    nv.buy_zone = table_offset({ "DT_CSPlayer" }, "m_bInBuyZone");
    nv.mins = table_offset({ "DT_CSPlayer", "DT_BaseEntity" }, "m_vecMins");
    nv.maxs = table_offset({ "DT_CSPlayer", "DT_BaseEntity" }, "m_vecMaxs");
}

template <typename T>
T value(const void* entity, int offset, T fallback = T{}) {
    if (!entity || offset < 0)
        return fallback;
    T result{};
    std::memcpy(&result, reinterpret_cast<const unsigned char*>(entity) + offset, sizeof(T));
    return result;
}

int datamap_offset(datamap* map, const char* name, int depth = 0) {
    if (!map || depth > 24 || !readable_range(map, sizeof(datamap)) ||
        map->size < 0 || map->size > 4096) {
        return -1;
    }
    if (!readable_range(map->desc, static_cast<std::size_t>(map->size) * sizeof(typedescription)))
        return -1;
    for (int i = 0; i < map->size; ++i) {
        const auto& field = map->desc[i];
        if (readable_range(field.name, 1) && std::strcmp(field.name, name) == 0)
            return field.offset[0];
    }
    for (int i = 0; i < map->size; ++i) {
        const auto& field = map->desc[i];
        if (field.type == 10 && field.embedded) {
            const int nested = datamap_offset(field.embedded, name, depth + 1);
            if (nested >= 0)
                return nested;
        }
    }
    return map->base ? datamap_offset(map->base, name, depth + 1) : -1;
}

int local_datamap_offset(void* local, int& cached, const char* name) {
    if (cached != -2)
        return cached;
    const auto get_map = virtual_function<pred_desc_map_t>(local, 17);
    if (!get_map || !function_in_module(client_module, reinterpret_cast<const void*>(get_map))) {
        cached = -1;
        return cached;
    }
    cached = datamap_offset(get_map(local), name);
    return cached;
}

int local_index() {
    return local_player_fn ? local_player_fn(engine) : 0;
}

void* entity_at(int index) {
    return index > 0 && entity_at_fn ? entity_at_fn(entity_list, index) : nullptr;
}

void* local_player() {
    return entity_at(local_index());
}

bool is_in_game() {
    return is_in_game_fn && is_in_game_fn(engine);
}

bool alive(void* player) {
    return player && value<int>(player, nv.health, 0) > 0 && value<int>(player, nv.life_state, 0) == 0;
}

void* active_weapon(void* local) {
    if (!local || !entity_from_handle_fn)
        return nullptr;
    return entity_from_handle_fn(entity_list, value<unsigned int>(local, nv.active_weapon, 0xFFFFFFFFu));
}

int weapon_id(void* weapon) {
    return value<unsigned short>(weapon, nv.weapon_id, 0);
}

bool grenade(int id) {
    return id >= 43 && id <= 48;
}

int move_type(void* local) {
    return value<int>(local, local_datamap_offset(local, move_type_offset, "m_MoveType"), 0);
}

int water_level(void* local) {
    return value<unsigned char>(local, local_datamap_offset(local, water_level_offset, "m_nWaterLevel"), 0);
}

float tick_interval() {
    return globals && globals->interval_per_tick > 0.001f && globals->interval_per_tick < 0.1f
        ? globals->interval_per_tick : 0.015625f;
}

float current_time() {
    return globals ? globals->current_time : 0.f;
}

void client_command(const std::string& command) {
    if (client_command_fn && !command.empty())
        client_command_fn(engine, command.c_str(), nullptr);
}

std::string player_name(int index) {
    if (!get_player_info_fn || index <= 0)
        return {};
    unsigned char info[512]{};
    if (!get_player_info_fn(engine, index, info))
        return {};
    const char* name = reinterpret_cast<const char*>(info + 16);
    std::size_t length = 0;
    while (length < 128 && name[length])
        ++length;
    return std::string(name, length);
}

int player_for_user_id(int user_id) {
    return player_for_user_fn ? player_for_user_fn(engine, user_id) : 0;
}

struct console_color {
    unsigned char r, g, b, a;
};
using con_color_msg_t = void(__cdecl*)(const console_color&, const char*, ...);

void console_print(const std::string& text, bool use_log_color = false) {
    if (text.empty())
        return;
    std::string line = "opaihook: " + text;
    for (char& c : line) {
        if (c == '\r' || c == '\n' || c == ';' || c == '"')
            c = ' ';
    }
    if (line.size() > 768)
        line.resize(768);
    const auto tier0 = GetModuleHandleW(L"tier0.dll");
    const auto print = tier0 ? reinterpret_cast<con_color_msg_t>(GetProcAddress(tier0, "ConColorMsg")) : nullptr;
    if (print) {
        console_color color{ 180, 180, 180, 255 };
        if (use_log_color) {
            const auto& state = misc_features::settings();
            color = {
                static_cast<unsigned char>(std::clamp(state.log_color[0], 0.f, 1.f) * 255.f),
                static_cast<unsigned char>(std::clamp(state.log_color[1], 0.f, 1.f) * 255.f),
                static_cast<unsigned char>(std::clamp(state.log_color[2], 0.f, 1.f) * 255.f),
                static_cast<unsigned char>(std::clamp(state.log_color[3], 0.f, 1.f) * 255.f)
            };
        }
        print(color, "%s\n", line.c_str());
    } else {
        client_command("echo " + line);
    }
}

bool hook_slot(void* instance, std::size_t index, void* replacement, void** original) {
    if (!instance || !replacement || !original || !readable_range(instance, sizeof(void*)))
        return false;
    void** table = *reinterpret_cast<void***>(instance);
    if (!readable_range(table, (index + 1) * sizeof(void*)))
        return false;
    if (table[index] == replacement)
        return *original != nullptr;
    DWORD old_protect = 0;
    if (!VirtualProtect(&table[index], sizeof(void*), PAGE_READWRITE, &old_protect))
        return false;
    void* previous = table[index];
    table[index] = replacement;
    DWORD ignored = 0;
    VirtualProtect(&table[index], sizeof(void*), old_protect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), &table[index], sizeof(void*));
    *original = previous;
    return previous != nullptr;
}



bool trace_ground(const vec3& from, float distance, const vec3& mins, const vec3& maxs,
                  game_trace& output) {
    if (!trace_ray_fn)
        return false;
    ray trace_ray(from, { from.x, from.y, from.z - distance }, mins, maxs);
    ground_trace_filter filter;
    std::memset(&output, 0, sizeof(output));
    output.fraction = 1.f;
    trace_ray_fn(engine_trace, trace_ray, mask_solid, &filter, &output);
    return true;
}

void player_hull(void* local, vec3& mins, vec3& maxs) {
    mins = value<vec3>(local, nv.mins, { -16.f, -16.f, 0.f });
    maxs = value<vec3>(local, nv.maxs, { 16.f, 16.f, 72.f });
    if (mins.x >= maxs.x || mins.y >= maxs.y) {
        mins = { -16.f, -16.f, 0.f };
        maxs = { 16.f, 16.f, 72.f };
    }
    maxs.z = 54.f;
}

bool walkable_ground_ahead(void* local, const vec3& origin, const vec3& velocity, float ticks) {
    if (!trace_ray_fn)
        return true; // Fail closed: never jump or stop on an unverified edge.
    vec3 mins, maxs;
    player_hull(local, mins, maxs);
    const vec3 predicted = origin + velocity * (tick_interval() * ticks);
    const vec3 start{ predicted.x, predicted.y, origin.z + 2.f };
    game_trace trace{};
    if (!trace_ground(start, 34.f, mins, maxs, trace))
        return true;
    if (trace.start_solid)
        return true;
    return trace.fraction < 1.f && !trace.all_solid && trace.plane.normal.z >= 0.7f;
}

void instant_stop(user_cmd* cmd, const vec3& velocity) {
    const float speed = velocity.length_2d();
    if (speed < 0.1f) {
        cmd->forwardmove = 0.f;
        cmd->sidemove = 0.f;
        return;
    }
    const float velocity_yaw = std::atan2(velocity.y, velocity.x) * 180.f / pi;
    const float yaw = normalize_yaw(cmd->viewangles.y - velocity_yaw);
    const vec3 direction = angle_vectors({ 0.f, yaw, 0.f });
    const vec3 stop = direction * -speed;
    cmd->forwardmove = std::clamp(stop.x, -450.f, 450.f);
    cmd->sidemove = std::clamp(stop.y, -450.f, 450.f);
}

bool fall_damage_expected(float height, float vertical_velocity) {
    const float interval = tick_interval();
    const float half_gravity = 800.f * interval * 0.5f;
    float velocity = vertical_velocity;
    int iterations = 0;
    while (height > 11.f && iterations++ < 512) {
        const float next = velocity - half_gravity;
        height += interval * next;
        velocity = next - half_gravity;
    }
    return velocity <= -580.f && height >= 9.f;
}

void no_fall_damage(user_cmd* cmd, void* local, const vec3& origin, const vec3& velocity, int flags) {
    if (!misc_features::settings().no_fall_damage || !trace_ray_fn || (flags & fl_on_ground) ||
        velocity.z >= 0.f) {
        return;
    }
    vec3 mins, maxs;
    player_hull(local, mins, maxs);
    vec3 start = origin;
    if (!(flags & fl_ducking))
        start.z += 9.f;
    game_trace trace{};
    if (!trace_ground(start, 1000.f, mins, maxs, trace) || trace.fraction >= 1.f ||
        trace.start_solid || trace.all_solid || trace.plane.normal.z < 0.7f) {
        return;
    }
    const float height = trace.fraction * 1000.f;
    if (height < 11.f && velocity.z < -580.f && height > 9.f) {
        cmd->buttons |= in_jump;
        cmd->buttons &= ~in_duck;
    } else if (fall_damage_expected(height, velocity.z)) {
        cmd->buttons |= in_duck;
        cmd->buttons &= ~in_jump;
    }
}


void fast_ladder(user_cmd* cmd) {
    if (!misc_features::settings().fast_ladder)
        return;
    cmd->viewangles.y = std::floor(cmd->viewangles.y + 0.5f);
    if (cmd->forwardmove > 0.f) {
        if (cmd->viewangles.x >= 45.f)
            return;
        cmd->viewangles.x = 89.f;
        cmd->buttons |= in_back | in_moveright;
        cmd->buttons &= ~(in_forward | in_moveleft);
        if (cmd->sidemove == 0.f)
            cmd->viewangles.y += 90.f;
        else if (cmd->sidemove < 0.f)
            cmd->viewangles.y += 150.f;
        else
            cmd->viewangles.y += 30.f;
    } else if (cmd->forwardmove < 0.f) {
        cmd->viewangles.x = 89.f;
        cmd->buttons |= in_forward | in_moveleft;
        cmd->buttons &= ~(in_back | in_moveright);
        if (cmd->sidemove == 0.f)
            cmd->viewangles.y += 90.f;
        else if (cmd->sidemove > 0.f)
            cmd->viewangles.y += 150.f;
        else
            cmd->viewangles.y += 30.f;
    }
}


void slow_walk(user_cmd* cmd, int flags) {
    const auto& state = misc_features::settings();
    if (!state.slow_walk || !(flags & fl_on_ground))
        return;
    const float maximum = std::clamp(state.slow_walk_speed, 0, 250) * 1.f;
    const float length = std::sqrt(cmd->forwardmove * cmd->forwardmove + cmd->sidemove * cmd->sidemove);
    if (length <= maximum || length < 0.001f)
        return;
    const float scale = maximum / length;
    cmd->forwardmove *= scale;
    cmd->sidemove *= scale;
}

float grenade_throw_velocity(int id) {
    constexpr float fallback_velocity = 750.f;
    if (!get_weapon_data_fn || !weapon_system || id <= 0)
        return fallback_velocity;
    void* info = get_weapon_data_fn(weapon_system, static_cast<unsigned int>(id));
    if (!readable_range(info, 0x110))
        return fallback_velocity;
    float result = 0.f;
    std::memcpy(&result, static_cast<unsigned char*>(info) + 0x10C, sizeof(result));
    return std::isfinite(result) && result >= 15.f && result <= 2000.f
        ? result : fallback_velocity;
}

bool trace_grenade_release(void*, const vec3& from, const vec3& to, bool hull,
                           misc_gameplay::grenade_release_trace& result) {
    if (!engine_trace || !trace_ray_fn)
        return false;
    const vec3 mins = hull ? vec3{ -2.f, -2.f, -2.f } : vec3{};
    const vec3 maxs = hull ? vec3{ 2.f, 2.f, 2.f } : vec3{};
    ray trace(from, to, mins, maxs);
    ground_trace_filter filter; // Ignore players; retain map collision and world visibility.
    game_trace output{};
    output.fraction = 1.f;
    trace_ray_fn(engine_trace, trace, mask_solid, &filter, &output);
    result.end = output.end;
    result.normal = output.plane.normal;
    result.fraction = output.fraction;
    result.start_solid = output.start_solid;
    result.all_solid = output.all_solid;
    return true;
}

bool get_grenade_release_player(void* context, int index,
                                misc_gameplay::grenade_release_player& result) {
    void* const local = context;
    void* const target = entity_at(index);
    if (!target || target == local || !alive(target))
        return false;
    result.origin = value<vec3>(target, nv.origin);
    result.velocity = value<vec3>(target, nv.velocity);
    result.mins = value<vec3>(target, nv.mins, { -16.f, -16.f, 0.f });
    result.maxs = value<vec3>(target, nv.maxs, { 16.f, 16.f, 72.f });
    result.team = value<int>(target, nv.team, 0);
    result.health = value<int>(target, nv.health, 0);
    result.armor = value<int>(target, nv.armor, 100);
    return result.team == 2 || result.team == 3;
}

bool purchase_pending = false;
int purchase_delay = 0;
bool quick_stop_complete = false;
bool suppress_view_update = false;
vec3 previous_local_velocity{};
bool have_previous_local_velocity = false;
int previous_buttons = 0;

void run_purchase() {
    const std::string command = misc_gameplay::purchase_command(g_cfg->misc);
    if (!command.empty())
        client_command(command);
}

void run_create_move(user_cmd* cmd) {
    const int old_buttons = previous_buttons;
    previous_buttons = cmd->buttons;
    if (!core_ready || !is_in_game())
        return;
    void* local = local_player();
    if (!alive(local))
        return;
    const int type = move_type(local);
    if (type != 2 && type != 9)
        return;
    const int flags = value<int>(local, nv.flags, 0);
    const vec3 velocity = value<vec3>(local, nv.velocity);
    const vec3 previous_velocity = have_previous_local_velocity ? previous_local_velocity : velocity;
    previous_local_velocity = velocity;
    have_previous_local_velocity = true;
    const vec3 origin = value<vec3>(local, nv.origin);
    const auto& state = misc_features::settings();

    if (purchase_pending && state.automatic_purchase) {
        ++purchase_delay;
        const bool in_buy_zone = value<bool>(local, nv.buy_zone, false);
        if ((nv.buy_zone >= 0 && in_buy_zone) || (nv.buy_zone < 0 && purchase_delay >= 2)) {
            run_purchase();
            purchase_pending = false;
            purchase_delay = 0;
        }
    }

    void* weapon = active_weapon(local);
    const int id = weapon_id(weapon);
    const bool holding_grenade = grenade(id);
    const bool pin_pulled = holding_grenade && value<bool>(weapon, nv.pin_pulled, false);
    const float throw_strength = holding_grenade ? value<float>(weapon, nv.throw_strength, 0.3f) : 0.f;
    const float throw_time = holding_grenade ? value<float>(weapon, nv.throw_time, 0.f) : 0.f;
    const bool attack_held = (cmd->buttons & (in_attack | in_attack2)) != 0;
    bool automatic_release = false;
    if (holding_grenade && state.automatic_grenade_release && attack_held && pin_pulled &&
        throw_strength > 0.f) {
        misc_gameplay::grenade_release_input prediction{};
        prediction.eye_position = origin + value<vec3>(local, nv.view_offset);
        prediction.view_angles = cmd->viewangles;
        prediction.player_velocity = velocity;
        prediction.weapon_id = id;
        prediction.local_team = value<int>(local, nv.team, 0);
        prediction.local_flags = flags;
        prediction.max_clients = globals ? globals->max_clients : 0;
        prediction.damage_threshold = state.grenade_release_damage;
        prediction.molly_radius = state.grenade_release_molly_radius;
        prediction.throw_strength = throw_strength;
        prediction.throw_velocity = grenade_throw_velocity(id);
        prediction.tick_interval = tick_interval();
        prediction.disable_in_air = state.grenade_release_disable_in_air;
        automatic_release = misc_gameplay::grenade_release_should_throw(
            prediction, local, trace_grenade_release, get_grenade_release_player);
    }
    if (automatic_release)
        cmd->buttons &= ~(in_attack | in_attack2);

    bool grenade_command = automatic_release;
    const bool manual_release =
        ((old_buttons & in_attack) && !(cmd->buttons & in_attack)) ||
        ((old_buttons & in_attack2) && !(cmd->buttons & in_attack2));
    const bool recent_throw = globals && throw_time > 0.f &&
        throw_time >= current_time() - tick_interval() * 2.f;
    if (holding_grenade && state.super_toss &&
        (manual_release || automatic_release || recent_throw)) {
        // Send the compensated command angles without syncing them to the local camera.
        const float throw_velocity = grenade_throw_velocity(id);
        suppress_view_update = misc_gameplay::compensate_grenade_throw(
            cmd, velocity, previous_velocity, throw_velocity, throw_strength);
        grenade_command = true;
    }

    misc_gameplay::bunny_hop(cmd, flags, g_cfg->misc.m_bunny_hop);
    if (type == 9) {
        if (!(flags & fl_on_ground))
            fast_ladder(cmd);
        return;
    }
    if (water_level(local) >= 2)
        return;

    if (flags & fl_on_ground) {
        bool stopped = false;
        if (state.edge_quick_stop && !state.edge_jump && !(cmd->buttons & in_jump) &&
            velocity.length_2d() > 15.f &&
            !walkable_ground_ahead(local, origin, velocity, 4.f)) {
            instant_stop(cmd, velocity);
            stopped = true;
        }
        if (state.edge_jump && velocity.length_2d() > 1.f && !(cmd->buttons & in_jump) &&
            !walkable_ground_ahead(local, origin, velocity, 1.f)) {
            cmd->buttons |= in_jump;
            cmd->buttons &= ~in_duck;
        }
        const bool movement_input = (cmd->buttons & (in_forward | in_back | in_moveleft | in_moveright)) != 0;
        if (!state.standalone_quick_stop || movement_input || (flags & fl_on_ground) == 0) {
            quick_stop_complete = false;
        } else if (velocity.length_2d() <= 15.f) {
            quick_stop_complete = true;
        } else if (!quick_stop_complete && !stopped) {
            instant_stop(cmd, velocity);
        }
    } else {
        quick_stop_complete = false;
        no_fall_damage(cmd, local, origin, velocity, flags);
        if (!grenade_command)
            misc_gameplay::air_strafe(cmd, velocity, flags, g_cfg->misc.m_auto_strafe,
                g_cfg->misc.m_strafe_type, g_cfg->misc.m_strafe_smooth);
    }
    slow_walk(cmd, flags);
}

using create_move_t = bool(__thiscall*)(void*, float, user_cmd*);
create_move_t original_create_move = nullptr;

bool __fastcall hooked_create_move(void* self, void*, float sample_time, user_cmd* cmd) {
    suppress_view_update = false;
    if (!original_create_move)
        return false;
    const bool result = original_create_move(self, sample_time, cmd);
    if (cmd && cmd->command_number)
        run_create_move(cmd);
    // Airflow's CreateMove hook returns false after retaining command edits so
    // the engine does not copy compensated grenade angles into the local view.
    return result && !suppress_view_update;
}

using emit_sound_t = int(__thiscall*)(void*, void*, int, int, const char*, unsigned int,
    const char*, float, float, int, int, int, const vec3*, const vec3*, void*, bool, float, int, int);
emit_sound_t original_emit_sound = nullptr;
ULONGLONG last_auto_accept = 0;

void auto_accept(const char* sound_entry) {
    if (!sound_entry || !misc_features::settings().auto_accept_matchmaking || is_in_game() ||
        std::strcmp(sound_entry, "UIPanorama.popup_accept_match_beep") != 0) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    if (now - last_auto_accept < 2000)
        return;
    static constexpr int pattern[] = {
        0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x8B, 0x4D, 0x08, 0xBA,
        -1, -1, -1, -1, 0xE8, -1, -1, -1, -1, 0x85, 0xC0, 0x75, 0x12
    };
    static const std::uint8_t* address = find_pattern(client_module, pattern);
    if (!address)
        return;
    using set_ready_t = char(__stdcall*)(const char*);
    reinterpret_cast<set_ready_t>(address)("accept");
    last_auto_accept = now;
    if (const HWND window = FindWindowW(L"Valve001", nullptr)) {
        FLASHWINFO flash{};
        flash.cbSize = sizeof(flash);
        flash.hwnd = window;
        flash.dwFlags = FLASHW_ALL | FLASHW_TIMERNOFG;
        flash.uCount = 0;
        flash.dwTimeout = 0;
        FlashWindowEx(&flash);
    }
}

int __fastcall hooked_emit_sound(void* self, void*, void* filter, int entity_index, int channel,
    const char* sound_entry, unsigned int sound_hash, const char* sample, float volume,
    float attenuation, int seed, int flags, int pitch, const vec3* origin, const vec3* direction,
    void* origins, bool update_positions, float sound_time, int speaker_entity, int unknown) {
    auto_accept(sound_entry);
    const char* visual_sound = sound_entry && sound_entry[0] ? sound_entry : sample;
    sound_esp::on_sound(entity_index, speaker_entity, visual_sound, origin != nullptr,
        origin ? origin->x : 0.f, origin ? origin->y : 0.f, origin ? origin->z : 0.f);
    return original_emit_sound(self, filter, entity_index, channel, sound_entry, sound_hash,
        sample, volume, attenuation, seed, flags, pitch, origin, direction, origins,
        update_positions, sound_time, speaker_entity, unknown);
}

using event_name_t = const char* (__thiscall*)(const void*);
using event_get_int_t = int(__thiscall*)(const void*, const char*, int);
using event_get_string_t = const char* (__thiscall*)(const void*, const char*, const char*);
using event_get_float_t = float(__thiscall*)(const void*, const char*, float);
using event_add_listener_t = bool(__thiscall*)(void*, void*, const char*, bool);
using event_remove_listener_t = void(__thiscall*)(void*, void*);

const char* hitgroup_name(int group) {
    switch (group) {
    case 1: return "head";
    case 2: return "chest";
    case 3: return "stomach";
    case 4: return "left arm";
    case 5: return "right arm";
    case 6: return "left leg";
    case 7: return "right leg";
    default: return "body";
    }
}

class misc_event_listener {
public:
    virtual ~misc_event_listener() = default;

    virtual void fire_game_event(void* event) {
        if (!event)
            return;
        const auto get_name = virtual_function<event_name_t>(event, 1);
        const char* name = get_name ? get_name(event) : nullptr;
        if (!name)
            return;
        const auto get_int = virtual_function<event_get_int_t>(event, 6);
        const auto get_string = virtual_function<event_get_string_t>(event, 9);
        const auto get_float = virtual_function<event_get_float_t>(event, 8);
        const auto& state = misc_features::settings();

        if (std::strcmp(name, "round_start") == 0) {
            purchase_pending = true;
            purchase_delay = 0;
            quick_stop_complete = false;
            return;
        }
        if (std::strcmp(name, "game_newmap") == 0) {
            purchase_pending = false;
            purchase_delay = 0;
            quick_stop_complete = false;
            previous_buttons = 0;
            have_previous_local_velocity = false;
            previous_local_velocity = {};
            return;
        }
        if (std::strcmp(name, "bullet_impact") == 0) {
            if (!get_int || !get_float || (!g_cfg->visuals.m_effects.m_bullet_impacts &&
                !g_cfg->visuals.m_effects.m_bullet_tracers))
                return;
            const int shooter = player_for_user_id(get_int(event, "userid", 0));
            const int local = local_index();
            void* shooter_entity = entity_at(shooter);
            void* local_entity = entity_at(local);
            if (!shooter_entity || !local_entity)
                return;
            int side = 0;
            if (shooter == local)
                side = 2;
            else if (value<int>(shooter_entity, nv.team, 0) == value<int>(local_entity, nv.team, -1))
                side = 1;
            vec3 start = value<vec3>(shooter_entity, nv.origin);
            start.z += 64.f;
            player_esp::record_bullet_impact(side, start.x, start.y, start.z,
                get_float(event, "x", 0.f), get_float(event, "y", 0.f), get_float(event, "z", 0.f));
            return;
        }
        if (std::strcmp(name, "weapon_fire") == 0) {
            if (get_int && get_string &&
                player_for_user_id(get_int(event, "userid", 0)) == local_index()) {
                // World > Windows > Event logs: every local shot opens a pending
                // entry that player_hurt resolves into hit / kill, or the pending
                // deadline turns into a miss (same model as the leaked event_log).
                player_visuals::event_log().begin_trigger_shot(
                    std::string(get_string(event, "weapon", "shot")), false);
            }
            if (!state.quick_switch || !get_int || !get_string ||
                player_for_user_id(get_int(event, "userid", 0)) != local_index()) {
                return;
            }
            const std::string_view weapon = get_string(event, "weapon", "");
            if (misc_gameplay::quick_switch_weapon(weapon))
                client_command("slot3; lastinv");
            return;
        }
        if (std::strcmp(name, "player_hurt") == 0) {
            if (!get_int)
                return;
            const int attacker = player_for_user_id(get_int(event, "attacker", 0));
            const int victim = player_for_user_id(get_int(event, "userid", 0));
            const int local = local_index();
            if (attacker != local || victim == local)
                return;
            void* local_entity = entity_at(local);
            void* victim_entity = entity_at(victim);
            if (local_entity && victim_entity &&
                value<int>(local_entity, nv.team, 0) == value<int>(victim_entity, nv.team, 0)) {
                return;
            }
            if (victim_entity) {
                vec3 impact = value<vec3>(victim_entity, nv.origin);
                impact.z += 48.f;
                player_esp::record_hit_marker(get_int(event, "dmg_health", 0),
                    impact.x, impact.y, impact.z);
                const int damage = get_int(event, "dmg_health", 0);
                if (damage > 0)
                    player_visuals::event_log().resolve_latest_trigger_shot(damage,
                        get_int(event, "health", 0) <= 0);
                else
                    player_visuals::event_log().push("shot evaded",
                        player_visuals::event_category::evaded);
            }
            if (state.hit_sound)
                client_command("play buttons/arena_switch_press_02.wav");
            if (state.log_damage_dealt) {
                char message[512];
                std::snprintf(message, sizeof(message), "Damage dealt to %s in the %s: %d (%d HP remaining)",
                    player_name(victim).c_str(), hitgroup_name(get_int(event, "hitgroup", 0)),
                    get_int(event, "dmg_health", 0), get_int(event, "health", 0));
                console_print(message, true);
            }
            return;
        }
        if (std::strcmp(name, "player_death") == 0) {
            if (!get_int || !get_string)
                return;
            // World > HUD > Death notices: our own feed so preserve mode and the
            // teammate-assist filter can actually act on the entries.
            const int attacker = player_for_user_id(get_int(event, "attacker", 0));
            const int victim = player_for_user_id(get_int(event, "userid", 0));
            const bool by_local = attacker == local_index();
            const bool assistant = get_int(event, "assister", 0) != 0;
            const bool headshot = get_int(event, "headshot", 0) != 0;
            std::string text = player_name(attacker) + " killed " + player_name(victim);
            if (headshot)
                text += " (headshot)";
            world_visuals::push_death_notice(std::move(text), assistant, by_local);
            return;
        }
        if (std::strcmp(name, "player_say") == 0) {
            if (!state.reveal_enemy_chat || !get_int || !get_string ||
                get_int(event, "teamonly", 0) == 0) {
                return;
            }
            const int speaker = player_for_user_id(get_int(event, "userid", 0));
            if (speaker == local_index())
                return;
            void* local = local_player();
            void* enemy = entity_at(speaker);
            if (!local || !enemy || value<int>(local, nv.team, 0) == value<int>(enemy, nv.team, 0))
                return;
            const char* text = get_string(event, "text", "");
            if (text && text[0])
                console_print("[enemy team] " + player_name(speaker) + ": " + text);
        }
    }

    virtual int get_event_debug_id() { return 43; }
};

misc_event_listener event_listener;

bool register_events() {
    if (events_installed)
        return true;
    if (!game_event_manager)
        return false;
    const auto add = virtual_function<event_add_listener_t>(game_event_manager, 3);
    if (!add || !function_in_module(engine_module, reinterpret_cast<const void*>(add)))
        return false;
    static constexpr const char* names[] = {
        "round_start", "game_newmap", "weapon_fire", "bullet_impact", "player_hurt", "player_death",
        "player_say"
    };
    for (const char* name : names) {
        if (!add(game_event_manager, &event_listener, name, false)) {
            const auto remove = virtual_function<event_remove_listener_t>(game_event_manager, 5);
            if (remove)
                remove(game_event_manager, &event_listener);
            return false;
        }
    }
    events_installed = true;
    return true;
}

bool install_create_move() {
    if (!create_move_installed) {
        const auto process_input = virtual_function<void*>(client, 10);
        if (!process_input || !function_in_module(client_module, process_input))
            return false;
        const auto* operand = static_cast<const std::uint8_t*>(process_input) + 5;
        if (!readable_range(operand, sizeof(std::uintptr_t)))
            return false;
        std::uintptr_t pointer = 0;
        std::memcpy(&pointer, operand, sizeof(pointer));
        if (!readable_range(reinterpret_cast<const void*>(pointer), sizeof(void*)))
            return false;
        std::memcpy(&client_mode_instance, reinterpret_cast<const void*>(pointer), sizeof(client_mode_instance));
        const auto target = virtual_function<void*>(client_mode_instance, 24);
        if (!target || !function_in_module(client_module, target))
            return false;
        create_move_installed = hook_slot(client_mode_instance, 24,
            reinterpret_cast<void*>(&hooked_create_move), reinterpret_cast<void**>(&original_create_move));
    }
    if (client_mode_instance)
        world_visuals::initialize(client_mode_instance, world_input, cvar_interface, engine, entity_list,
            panel_interface, client_module);
    return create_move_installed;
}

bool install_sound_hook() {
    if (sound_installed)
        return true;
    if (!engine_sound)
        return false;
    const auto target = virtual_function<void*>(engine_sound, 5);
    if (!target || !function_in_module(engine_module, target))
        return false;
    sound_installed = hook_slot(engine_sound, 5, reinterpret_cast<void*>(&hooked_emit_sound),
        reinterpret_cast<void**>(&original_emit_sound));
    return sound_installed;
}

bool acquire() {
    client_module = GetModuleHandleW(L"client.dll");
    engine_module = GetModuleHandleW(L"engine.dll");
    if (!client_module || !engine_module)
        return false;
    client = create_interface(client_module, "VClient018");
    engine = create_interface(engine_module, "VEngineClient014");
    entity_list = create_interface(client_module, "VClientEntityList003");
    engine_trace = create_interface(engine_module, "EngineTraceClient004");
    engine_sound = create_interface(engine_module, "IEngineSoundClient003");
    game_event_manager = create_interface(engine_module, "GAMEEVENTSMANAGER002");
    const HMODULE vstdlib_module = GetModuleHandleW(L"vstdlib.dll");
    const HMODULE vgui2_module = GetModuleHandleW(L"vgui2.dll");
    cvar_interface = create_interface(vstdlib_module, "VEngineCvar007");
    panel_interface = create_interface(vgui2_module, "VGUI_Panel009");
    static constexpr int input_pattern[] = {
        0xB9, -1, -1, -1, -1, 0xF3, 0x0F, 0x11, 0x04, 0x24, 0xFF, 0x50, 0x10
    };
    const auto* input_address = find_pattern(client_module, input_pattern);
    world_input = absolute_pointer<void>(input_address ? input_address + 1 : nullptr);
    if (!client || !engine || !entity_list)
        return false;

    get_all_classes = virtual_function<get_all_classes_t>(client, 8);
    entity_at_fn = virtual_function<entity_at_t>(entity_list, 3);
    entity_from_handle_fn = virtual_function<entity_from_handle_t>(entity_list, 4);
    local_player_fn = virtual_function<local_player_t>(engine, 12);
    is_in_game_fn = virtual_function<engine_bool_t>(engine, 26);
    player_for_user_fn = virtual_function<player_for_user_t>(engine, 9);
    get_player_info_fn = virtual_function<get_player_info_t>(engine, 8);
    client_command_fn = virtual_function<client_command_t>(engine, 114);
    trace_ray_fn = virtual_function<trace_ray_t>(engine_trace, 5);
    if (!get_all_classes || !entity_at_fn || !entity_from_handle_fn || !local_player_fn ||
        !is_in_game_fn || !player_for_user_fn || !get_player_info_fn || !client_command_fn ||
        !function_in_module(client_module, reinterpret_cast<const void*>(get_all_classes)) ||
        !function_in_module(client_module, reinterpret_cast<const void*>(entity_at_fn)) ||
        !function_in_module(client_module, reinterpret_cast<const void*>(entity_from_handle_fn)) ||
        !function_in_module(engine_module, reinterpret_cast<const void*>(local_player_fn)) ||
        !function_in_module(engine_module, reinterpret_cast<const void*>(is_in_game_fn)) ||
        !function_in_module(engine_module, reinterpret_cast<const void*>(player_for_user_fn)) ||
        !function_in_module(engine_module, reinterpret_cast<const void*>(get_player_info_fn)) ||
        !function_in_module(engine_module, reinterpret_cast<const void*>(client_command_fn))) {
        return false;
    }
    if (trace_ray_fn && !function_in_module(engine_module, reinterpret_cast<const void*>(trace_ray_fn)))
        trace_ray_fn = nullptr;

    static constexpr int global_pattern[] = {
        0xA1, -1, -1, -1, -1, 0xF3, 0x0F, 0x10, 0x40, -1,
        0x0F, 0x5A, 0xC0, 0xF2, 0x0F, 0x11, 0x04
    };
    static constexpr int weapon_system_pattern[] = {
        0x8B, 0x35, -1, -1, -1, -1, 0xFF, 0x10, 0x0F, 0xB7, 0xC0
    };
    const auto* global_address = find_pattern(client_module, global_pattern);
    // mov eax, dword ptr [global_vars]; the absolute operand follows the opcode.
    globals = absolute_pointer<global_vars>(global_address ? global_address + 1 : nullptr);
    const auto* weapon_address = find_pattern(client_module, weapon_system_pattern);
    weapon_system = absolute_pointer<void>(weapon_address ? weapon_address + 2 : nullptr);
    get_weapon_data_fn = virtual_function<get_weapon_data_t>(weapon_system, 2);
    if (get_weapon_data_fn && !function_in_module(client_module, reinterpret_cast<const void*>(get_weapon_data_fn)))
        get_weapon_data_fn = nullptr;
    if (globals && !readable_range(globals, sizeof(global_vars)))
        globals = nullptr;

    resolve_netvars();
    if (nv.flags < 0 || nv.velocity < 0 || nv.origin < 0 || nv.active_weapon < 0 ||
        nv.weapon_id < 0 || nv.team < 0 || nv.health < 0 || nv.life_state < 0) {
        return false;
    }
    return true;
}

bool console_filter_applied = false;
bool chat_filter_applied = false;

void update_filters() {
    const auto& state = misc_features::settings();
    if (state.filter_console_output && !console_filter_applied) {
        client_command("developer 0; con_filter_enable 1; con_filter_text opaihook");
        console_filter_applied = true;
    } else if (!state.filter_console_output && console_filter_applied) {
        client_command("con_filter_enable 0; con_filter_text \"\"");
        console_filter_applied = false;
    }
    if (state.filter_chat_messages && !chat_filter_applied) {
        client_command("cl_chatfilters 0");
        chat_filter_applied = true;
    } else if (!state.filter_chat_messages && chat_filter_applied) {
        client_command("cl_chatfilters 63");
        chat_filter_applied = false;
    }
}
} // namespace

namespace misc_features {
void initialize() {
    if (!core_ready) {
        const ULONGLONG now = GetTickCount64();
        if (now < next_acquire_attempt)
            return;
        next_acquire_attempt = now + 1000;
        core_ready = acquire();
        if (!core_ready)
            return;
    }
    install_create_move();
    install_sound_hook();
    register_events();
    update_filters();
}
} // namespace misc_features
