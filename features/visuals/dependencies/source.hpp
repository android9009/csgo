#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace visuals_source {
struct vector3 {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

struct line3 {
    vector3 from;
    vector3 to;
};

struct hitbox_marker {
    vector3 center;
    float radius = 0.f;
    int hitbox = -1;
    int group = 0;
};

// Fixed-capacity sequence for the per-frame snapshot. The render path runs at
// Present rate, so player geometry must not allocate: Fatality and the other
// references keep their player records in fixed arrays for the same reason.
// Overflow is dropped silently instead of growing the heap.
template <typename T, std::size_t Capacity>
struct fixed_list {
    std::array<T, Capacity> items{};
    int count = 0;

    void clear() { count = 0; }
    void push_back(const T& value) {
        if (count < static_cast<int>(Capacity))
            items[static_cast<std::size_t>(count++)] = value;
    }
    bool empty() const { return count == 0; }
    std::size_t size() const { return static_cast<std::size_t>(count); }
    const T* begin() const { return items.data(); }
    const T* end() const { return items.data() + count; }
    const T& operator[](std::size_t index) const { return items[index]; }
};

// A CS:GO player skeleton stays well below 128 bones and the standard hitbox
// sets below 192 edges (16 sets of 12); the marker list covers one per hitbox.
inline constexpr std::size_t skeleton_capacity = 128;
inline constexpr std::size_t hitbox_capacity = 192;
inline constexpr std::size_t hitbox_marker_capacity = 16;
using line_list = fixed_list<line3, skeleton_capacity>;
using hitbox_line_list = fixed_list<line3, hitbox_capacity>;
using hitbox_marker_list = fixed_list<hitbox_marker, hitbox_marker_capacity>;

struct renderable_entity_data {
    void* entity = nullptr;
    int class_id = -1;
    bool is_player = false;
    bool viewmodel = false;
    bool ragdoll = false;
};

struct player_data {
    int index = 0;
    int side = 0; // 0 = enemy, 1 = teammate, 2 = local player (ESP only)
    int team = 0;
    int health = 0;
    int armor = 0;
    int money = 0;
    int weapon_id = 0;
    int clip = -1;
    int max_clip = -1;
    int reserve_ammo = -1;
    float distance = 0.f;
    float readiness = -1.f;
    // Airflow keeps the complete last live value snapshot for dormant/fade
    // rendering. Live entities always overwrite it at FRAME_RENDER_END.
    float alpha = 1.f;
    bool dormant = false;
    bool armor_valid = false;
    bool money_valid = false;
    bool immunity_valid = false;
    bool helmet = false;
    bool defuser = false;
    bool blind = false;
    bool scoped = false;
    bool reloading = false;
    bool immunity = false;
    bool slowed = false;
    bool bomb = false;
    bool hostage = false;
    bool defusing = false;
    bool pin_pulled = false;
    bool taser = false;
    // Fixed text buffers keep the record trivially copyable: it is copied into
    // the dormant cache and published to the renderer every frame.
    char name[64] = {};
    char weapon_name[32] = {};
    vector3 origin;
    vector3 mins;
    vector3 maxs;
    // Airflow's two-point player box only reads pose 1 (stand) and pose 12
    // (pitch). Capturing those two instead of the whole pose array removes 22
    // netvar reads per player per frame.
    float stand_pose = 0.f;
    float body_pitch = 0.f;
    bool poses_valid = false;
    line_list skeleton;
    // Twelve edges per hitbox, already transformed by the current bone matrix.
    hitbox_line_list hitboxes;
    hitbox_marker_list hitbox_markers;
};

enum world_kind : int {
    world_weapon = 0,
    world_projectile,
    world_smoke,
    world_inferno,
    world_planted_c4,
    world_hostage,
    world_window,
};

struct world_data {
    int index = 0;
    int kind = world_weapon;
    int weapon_id = 0;
    int clip = -1;
    int max_clip = -1;
    float alpha = 1.f;
    float distance = 0.f;
    float time_remaining = -1.f;
    float range = 0.f;
    vector3 origin;
    vector3 velocity;
    vector3 mins;
    vector3 maxs;
    std::string name;
};

struct local_view_state {
    bool available = false;
    bool alive = false;
    bool scoped = false;
    // Self > Models > Transparency conditions. Fatality reads the same two
    // states straight off the local player (is_scoped / active grenade); the
    // zoom level only feeds the separate x2 slider.
    bool holding_grenade = false;
    int zoom_level = 1;
    // World > Crosshairs / Grenades read the same active-weapon state. Knife,
    // C4, sniper and pin state are plain weapon flags, so they cost one
    // definition index instead of a second entity walk per frame.
    int weapon_id = 0;
    bool holding_knife = false;
    bool holding_c4 = false;
    bool holding_sniper = false;
    bool pin_pulled = false;
    vector3 aim_punch;
    vector3 view_punch;
    float inaccuracy = 0.f;
    float spread = 0.f;
};

struct frame_data {
    bool in_game = false;
    bool has_view_matrix = false;
    bool local_alive = false;
    int local_index = 0;
    vector3 local_origin;
    vector3 view_angles;
    float view_matrix[4][4] = {};
    std::vector<player_data> players;
    std::vector<world_data> world;
};

// Retry-safe Source 1 acquisition. Core player/team netvars gate chams; ESP-only
// fields remain optional so one missing prop does not disable the whole module.
bool initialize();
bool ready();
bool is_in_game();
bool is_console_visible();
int player_side(int entity_index); // -1 invalid/local/dormant, 0 enemy, 1 teammate
int player_side_for_sound(int entity_index); // team relation without rejecting a dormant player
int player_side_from_entity(void* entity); // same check using DrawModelState's entity
bool resolve_renderable_entity(void* renderable, renderable_entity_data& output);
int entity_team_side(void* entity); // -1 invalid, 0 enemy, 1 teammate
bool is_local_entity(void* entity);
bool local_player_alive();
bool get_local_view_state(local_view_state& output, bool include_accuracy = false);
void set_flash_removed(bool enabled);
bool entity_parented_to_local(void* entity, bool local_alive);
int local_user_id();
int aimed_player_index(); // enemy nearest to the local shot ray, 0 when none
int frame_number();
bool entity_exists(int entity_index);
// The renderer borrows the immutable entity snapshot; only its render-view
// fields are refreshed at Present, avoiding a deep bone/line copy per frame.
std::shared_ptr<const frame_data> capture_shared();
void capture(frame_data& output);

int atlas_weapon_id(int item_definition_index); // -1 when no embedded glyph exists
} // namespace visuals_source
