#pragma once

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

struct renderable_entity_data {
    void* entity = nullptr;
    int class_id = -1;
    bool ragdoll = false;
};

struct player_data {
    int index = 0;
    int side = 0; // 0 = enemy, 1 = teammate
    int team = 0;
    int health = 0;
    int armor = 0;
    int money = 0;
    int weapon_id = 0;
    int clip = -1;
    int max_clip = -1;
    int reserve_ammo = -1;
    int order_priority = 0;
    float distance = 0.f;
    float readiness = -1.f;
    float hit_chance = -1.f;
    float delay_ms = -1.f;
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
    bool vulnerable = false;
    bool bomb = false;
    bool hostage = false;
    bool defusing = false;
    bool pin_pulled = false;
    bool taser = false;
    std::string name;
    std::string weapon_name;
    vector3 origin;
    vector3 mins;
    vector3 maxs;
    std::vector<line3> skeleton;
    // Twelve edges per hitbox, already transformed by the current bone matrix.
    std::vector<line3> hitboxes;
    std::vector<hitbox_marker> hitbox_markers;
};

struct frame_data {
    bool in_game = false;
    bool has_view_matrix = false;
    int local_index = 0;
    float view_matrix[4][4] = {};
    std::vector<player_data> players;
};

// Retry-safe Source 1 acquisition. Core player/team netvars gate chams; ESP-only
// fields remain optional so one missing prop does not disable the whole module.
bool initialize();
bool ready();
bool is_in_game();
bool is_console_visible();
int player_side(int entity_index); // -1 invalid/local, 0 enemy, 1 teammate
bool resolve_renderable_entity(void* renderable, renderable_entity_data& output);
int entity_team_side(void* entity); // -1 invalid, 0 enemy, 1 teammate
int local_user_id();
int aimed_player_index(); // enemy nearest to the local shot ray, 0 when none
int frame_number();
bool entity_exists(int entity_index);
void capture(frame_data& output);

int atlas_weapon_id(int item_definition_index); // -1 when no embedded glyph exists
} // namespace visuals_source
