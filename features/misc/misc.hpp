#pragma once

namespace misc_features {
struct state {
    bool no_fall_damage = false;
    bool edge_quick_stop = false;
    bool standalone_quick_stop = false;
    bool edge_jump = false;
    bool slow_walk = false;
    int slow_walk_speed = 34;
    bool fast_ladder = false;

    bool quick_switch = false;
    bool super_toss = false;
    bool hit_sound = false;
    bool automatic_purchase = false;
    bool automatic_grenade_release = false;
    int grenade_release_damage = 50;
    int grenade_release_molly_radius = 100;
    bool grenade_release_disable_in_air = false;
    bool auto_accept_matchmaking = false;
    bool reveal_enemy_chat = false;
    bool filter_console_output = false;
    bool filter_chat_messages = false;
    bool log_damage_dealt = false;
    float log_color[4] = { 0.66f, 0.60f, 1.f, 1.f };
};

// Shared Misc state: menu controls write it and the runtime modules consume it.
// Kept outside the menu so feature code has no GUI dependency.
state& settings();

// Called from the render thread. Acquisition and hook installation are lazy and
// retry-safe while the game is still loading or changing servers.
void initialize();
} // namespace misc_features
