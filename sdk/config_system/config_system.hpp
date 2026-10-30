#pragma once
#include "../typedefs/c_color.hpp"

#include <vector>
#include <string>
#include <memory>
#include <array>

class c_dumped_item;

#define g_cfg g_config_system

template <typename T>
struct item_t {
    T* pointer;
    std::string category;
    std::string name;
    T default_value;
};

// ============================================================================
// WEAPON GROUPS
// ============================================================================
enum weapon_group_t : int {
    WEAPON_GROUP_GENERAL = 0,   // Общие настройки
    WEAPON_GROUP_PISTOL,        // Пистолеты
    WEAPON_GROUP_HEAVY_PISTOL,  // Дигл, R8
    WEAPON_GROUP_SMG,           // SMG
    WEAPON_GROUP_RIFLE,         // M4, AK
    WEAPON_GROUP_AUTO,          // Авто снайперки
    WEAPON_GROUP_SCOUT,         // SSG 08
    WEAPON_GROUP_AWP,           // AWP
    WEAPON_GROUP_SHOTGUN,       // Дробовики
    WEAPON_GROUP_LMG,           // LMG
    WEAPON_GROUP_MAX
};

const char* const weapon_group_names[] = {
    "General",
    "Pistols",
    "Heavy Pistol",
    "SMG",
    "Rifle",
    "Auto Sniper",
    "Scout",
    "AWP",
    "Shotgun",
    "LMG"
};

// ============================================================================
// CONFIG SYSTEM
// ============================================================================

class c_config_system {
private:
    std::vector<item_t<bool>> booleans;
    std::vector<item_t<int>> ints;
    std::vector<item_t<float>> floats;
    std::vector<item_t<int>> keybinds;
    std::vector<item_t<std::string>> strings;
    std::vector<item_t<c_color>> colors_rgb;
    
public:
    void push_item(bool* pointer, std::string category, std::string name, bool def_value);
    void push_item(int* pointer, std::string category, std::string name, int def_value);
    void push_item(float* pointer, std::string category, std::string name, float def_value);
    void push_item(c_color* pointer, std::string category, std::string name, c_color def_value);

    void setup_values();
    void read(std::string path);
    void save(std::string path);
    void load(std::string path);
    void remove(std::string path);
    void refresh();
    void reset();
    void menu();
    std::vector<std::string> config_files;

    // ========================================================================
    // RAGE BOT - С ГРУППАМИ ОРУЖИЙ
    // ========================================================================
    
    // Настройки для каждой группы оружий
    struct weapon_config_t {
        bool m_enabled = true;
        bool m_override_general = false;  // Использовать свои настройки вместо General
        
        // Hitboxes (6 body parts)
        bool m_hitboxes[6] = {true, true, true, false, false, false};  // Head, Chest, Stomach, Arms, Legs, Feet
        bool m_multi_points[6] = {true, true, true, false, false, false};  // Head, Chest, Stomach, Arms, Legs, Feet
        int m_head_scale = 50;  // -1 = Auto
        int m_body_scale = 50;  // -1 = Auto
        
        // Accuracy
        int m_hit_chance = 60;  // -1 = Auto
        int m_hit_chance_seed = 0;  // 0-256
        int m_dt_hit_chance = 0;
        int m_extrapolation_hit_chance = 0;
        bool m_force_shot = false;
        int m_minimum_hit_chance = 0;
        int m_extrapolate = 0;  // 0-16 ticks
        bool m_backtrack = false;
        int m_backtrack_ticks = 12;  // 0-64 ticks (0 = all unlag window)
        int m_minimum_damage = 20;  // -1 = Auto
        bool m_delay_shot = false;
        
        // Auto Stop (multibox - can select multiple)
        bool m_auto_stop = false;
        bool m_auto_stop_early = false;
        bool m_auto_stop_full = true;
        bool m_auto_stop_between = false;
        bool m_auto_stop_air = false;
        bool m_auto_stop_duck = false;
        
        // Other
        bool m_auto_scope = false;
        bool m_penetration = false;

        // Safety (UI only: 0 = Off, 1 = Prefer, 2 = Force)
        int m_body_aim = 0;
        int m_safe_points = 0;
        bool m_hitbox_safety[6] = {true, true, true, false, false, false};
    };
    
    struct rage_bot_t {
        // Глобальные настройки
        bool m_enabled = false;
        bool m_silent = true;
        
        // Текущая выбранная группа в меню
        int m_selected_group = 0;
        
        // Настройки по группам оружий
        weapon_config_t m_weapon_config[WEAPON_GROUP_MAX];
        
        // Override keybind
        int m_override_damage_key_bind = 0;
        int m_override_damage_key_bind_style = 0;
        
        // Resolver
        bool m_resolver_enabled = true;
        bool m_resolver_override = false;
        int m_resolver_override_key = 0;
        int m_resolver_override_key_style = 0;
        
        // Exploits
        bool m_double_tap = false;
        int m_double_tap_key = 0;
        int m_double_tap_key_style = 0;
        bool m_hide_shots = false;
        int m_hide_shots_key = 0;
        int m_hide_shots_key_style = 0;

        // Field of View (rage, degrees)
        int m_fov = 10;

        // Peek Assist (+ gear: Style / Auto Stop / Retreat Mode / Max Distance)
        bool m_peek_assist = false;
        int m_peek_assist_style = 1;         // 0=Static, 1=Gradient, 2=Dynamic
        int m_peek_assist_auto_stop = 0;     // 0=Select, 1=Early, 2=Full Stop
        int m_peek_assist_retreat = 1;       // 0=Off, 1=On Shot, 2=On Hurt
        int m_peek_assist_max_distance = 100; // 0..100 (100 = Inf.)

        // Получение конфига для текущего оружия
        weapon_config_t& get_weapon_config(int group) {
            if (group < 0 || group >= WEAPON_GROUP_MAX)
                group = WEAPON_GROUP_GENERAL;
            
            auto& cfg = m_weapon_config[group];
            
            // Если не override - возвращаем General
            if (group != WEAPON_GROUP_GENERAL && !cfg.m_override_general) {
                return m_weapon_config[WEAPON_GROUP_GENERAL];
            }
            
            return cfg;
        }
    } rage_bot;

    // ========================================================================
    // LEGIT BOT
    // ========================================================================
    struct legit_bot_t {
        bool m_enabled = false;
        int m_target_selection = 0;
        float m_fov = 5.0f;
        float m_smooth = 15.0f;
        bool m_hitboxes[4] = {true, true, true, true};
        bool m_aim_backtrack = false;
        bool m_silent = false;
        
        // Triggerbot
        bool m_triggerbot_enabled = false;
        int m_triggerbot_key = 0;
        int m_triggerbot_key_style = 1;
        int m_triggerbot_delay = 0;
        int m_triggerbot_hitchance = 100;
        bool m_triggerbot_head_only = false;
        bool m_triggerbot_magnet = false;
        
        // Backtrack
        bool m_backtrack_enabled = false;
        int m_backtrack_ticks = 6;
        bool m_backtrack_visualize = false;
    } legit_bot;

    // ========================================================================
    // ANTI-AIM - РАСШИРЕННЫЙ
    // ========================================================================
    struct anti_hit_t {
        bool m_enabled = false;
        
        // ====== PITCH ======
        int m_pitch_mode = 0;  // 0=off, 1=down, 2=up, 3=zero, 4=custom
        int m_pitch_amount = 0;
        
        // ====== YAW ======
        int m_yaw_mode = 0;  // 0=off, 1=backward, 2=sideways, 3=spin, 4=at target, 5=custom
        bool m_at_target = false;
        int m_yaw_amount = 0;
        int m_yaw_jitter = 0;  // UI combo: 0=Disabled, 1=Low, 2=Medium, 3=High

        // ====== MISC (rage anti-aim box) ======
        bool m_aa_slow_walk = false;
        int m_aa_slow_walk_speed = 34;  // 1-100%
        bool m_freestanding = false;
        int m_freestanding_mode = 0;  // 0=Auto, 1=Left, 2=Right
        bool m_leg_jitter = false;  // дёрганье анимаций ножек
        
        // ====== JITTER ======
        bool m_jitter_enabled = false;
        int m_jitter_mode = 0;  // 0=offset, 1=center, 2=random
        int m_jitter_amount = 0;
        
        // ====== DESYNC ======
        bool m_desync_enabled = false;
        int m_desync_mode = 0;  // 0=static, 1=jitter, 2=switch
        int m_desync_left = 58;
        int m_desync_right = 58;
        int m_flip_key = 0;
        int m_flip_key_style = 2;
        
        // ====== FAKELAG ======
        bool m_fakelag_enabled = false;
        int m_fakelag_mode = 0;  // 0=static, 1=random, 2=adaptive
        int m_fakelag_limit = 7;
        bool m_fakelag_on_peek = false;
        bool m_fakelag_on_ground_only = false;
        
        // ====== ANTI BRUTEFORCE ======
        bool m_anti_bruteforce = false;
        int m_anti_bruteforce_mode = 0;  // 0=switch, 1=random
        
        // ====== УСЛОВИЯ ======
        // Настройки для разных условий (стоя, в движении, в воздухе, slow walk, etc.)
        struct condition_t {
            bool m_override = false;
            int m_pitch_mode = 0;
            int m_yaw_mode = 0;
            int m_yaw_amount = 0;
            bool m_jitter = false;
            int m_jitter_amount = 0;
        };
        
        condition_t m_standing;
        condition_t m_moving;
        condition_t m_air;
        condition_t m_slow_walk;
        
        // Legacy (для совместимости)
        bool m_override_pitch = false;
        bool m_override_yaw = false;
    } anti_hit;

    // ========================================================================
    // VISUALS
    // ========================================================================
    struct visuals_t {
        struct player_esp_t {
            bool m_enabled = true;
            bool m_teammates = false;
            bool m_bounding_box = true;
            bool m_health_bar = true;
            bool m_name = true;
            bool m_weapon = true;
            bool m_ammo_bar = true;
            bool m_flags = true;
            bool m_skeleton = false;
            bool m_distance = false;
            bool m_armor_bar = false;
            
            c_color m_name_color{1.f, 1.f, 1.f, 1.f};
            c_color m_box_color{1.f, 1.f, 1.f, 1.f};
            c_color m_skeleton_color{1.f, 1.f, 1.f, 0.8f};
        } m_player_esp;
        
        struct chams_t {
            bool m_enabled = false;
            int m_material = 0;
            c_color m_visible_color{0.2f, 0.8f, 0.2f, 1.f};
            c_color m_hidden_color{0.8f, 0.2f, 0.2f, 1.f};
            bool m_xqz = true;
        } m_chams;
        
        struct glow_t {
            bool m_enabled = false;
            int m_style = 0;
            c_color m_color{1.f, 0.5f, 0.f, 1.f};
        } m_glow;
        
        struct effects_t {
            bool m_hitmarker = true;
            bool m_hitmarker_sound = true;
            bool m_kill_effect = false;
            bool m_bullet_tracers = false;
            c_color m_tracer_color{0.42f, 0.65f, 1.0f, 1.f};
            bool m_bullet_impacts = false;
        } m_effects;
        
        struct indicators_t {
            bool m_crosshair = false;
            bool m_spread_circle = false;
            bool m_aa_lines = false;
            bool m_desync = false;
            bool m_manual_aa = false;
        } m_indicators;
    } visuals;

    // ========================================================================
    // WORLD
    // ========================================================================
    struct world_esp_t {
        bool m_enable_thirdperson = false;
        bool m_enable_override_fov = false;
        int m_distance = 120;
        int m_override_fov = 0;
        int m_thirdperson_key_bind = 0;
        int m_thirdperson_key_bind_style = 2;
    } world_esp;

    struct world_modulation_t {
        bool m_custom_skybox = false;
        c_color m_sky{0.36f, 0.36f, 0.36f, 1.f};
        c_color m_sky_clouds{0.37f, 0.84f, 0.98f, 1.f};
        c_color m_wall{1.f, 1.f, 1.f, 1.f};
        c_color m_lighting{0.35f, 0.35f, 0.52f, 1.f};
        
        bool m_night_mode = false;
        float m_brightness = 0.5f;
        int m_exposure = 20;
        bool m_render_fog = false;
        bool m_remove_smoke = false;
        bool m_remove_flash = false;
        
        int m_fov = 0;
        int m_viewmodel_fov = 0;
        int m_removals = 0;
    } world;

    // ========================================================================
    // MISC
    // ========================================================================
    struct misc_t {
        // Movement
        bool m_bunny_hop = false;
        bool m_auto_strafe = false;
        int m_strafe_type = 0;  // 0=legit, 1=directional, 2=rage
        int m_strafe_smooth = 70;
        
        bool m_edge_jump = false;
        int m_edge_jump_key = 0;
        int m_edge_jump_key_style = 1;
        
        bool m_jump_bug = false;
        int m_jump_bug_key = 0;
        int m_jump_bug_key_style = 1;
        
        bool m_edge_bug = false;
        int m_edge_bug_key = 0;
        int m_edge_bug_key_style = 1;
        
        bool m_long_jump = false;
        int m_long_jump_key = 0;
        int m_long_jump_key_style = 1;
        
        bool m_infinite_duck = false;
        bool m_fast_duck = false;
        bool m_slide_walk = false;
        
        // Slow Walk
        bool m_slow_walk = false;
        int m_slow_walk_key = 0;
        int m_slow_walk_key_style = 1;
        int m_slow_walk_speed = 34;
        
        // Other
        bool m_reveal_ranks = false;
        bool m_clan_tag = false;
        bool m_watermark = true;
        bool m_spectator_list = true;
        bool m_keybinds_list = true;
        bool m_auto_accept = false;
        
        // Buy bot
        bool m_buy_bot_enabled = false;
        int m_buy_bot_primary = 0;
        int m_buy_bot_secondary = 0;
        bool m_buy_bot_armor = false;
        bool m_buy_bot_defuser = false;
        bool m_buy_bot_grenades = false;
    } misc;

    // ========================================================================
    // SKINS
    // ========================================================================
    struct skins_t {
        struct {
            int m_selected = 0;
            int m_selected_ct = 0;
        } m_agents;

        struct skin_data_t {
            int m_paint_kit = 0;
            int m_item_def_index = 0;
            int m_prev_skin = -1;
        } m_skin_settings[100];

        struct {
            int m_selected = 0;
        } m_knives;

        struct {
            int m_selected = 0;
        } m_gloves;

        int m_selected_weapon = 0;
        int m_selected_type = 0;
        c_dumped_item* m_selected_item = nullptr;
    } skins;
    
    // ========================================================================
    // HELPER - Определение группы оружия по weapon type
    // ========================================================================
    static weapon_group_t get_weapon_group(int weapon_type, int item_def_index) {
        // Особые случаи по item_def_index
        switch (item_def_index) {
            case 1:   // Deagle
            case 64:  // R8 Revolver
                return WEAPON_GROUP_HEAVY_PISTOL;
            case 40:  // SSG 08 (Scout)
                return WEAPON_GROUP_SCOUT;
            case 9:   // AWP
                return WEAPON_GROUP_AWP;
            case 11:  // G3SG1
            case 38:  // SCAR-20
                return WEAPON_GROUP_AUTO;
        }
        
        // По типу оружия
        switch (weapon_type) {
            case 1:  // Pistol
                return WEAPON_GROUP_PISTOL;
            case 2:  // SMG
                return WEAPON_GROUP_SMG;
            case 3:  // Rifle
                return WEAPON_GROUP_RIFLE;
            case 4:  // Shotgun
                return WEAPON_GROUP_SHOTGUN;
            case 5:  // Sniper
                return WEAPON_GROUP_SCOUT;  // Default to scout if not awp/auto
            case 6:  // LMG
                return WEAPON_GROUP_LMG;
            default:
                return WEAPON_GROUP_GENERAL;
        }
    }
};

inline const auto g_config_system = std::make_unique<c_config_system>();
