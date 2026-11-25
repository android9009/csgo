#pragma once
// Minimal game-world preamble for menu TUs that need hacks.hpp (via
// sdk/interfaces/interfaces.hpp) without the full globals.hpp chain.
//
// hacks.hpp only *references* the game classes (pointers) - forward
// declarations are enough, plus memory::bits_t for one value member
// (cheat_info_t::eggs). Do NOT add main-tree headers here: the shim SDK
// types (utils/math/*, studiohdr.hpp) collide with the main tree's
// same-named, differently-laid-out definitions.
#include <windows.h>
#include <d3d9.h>
#include <string>
#include "../memory.hpp"

class c_base_combat_weapon;
class c_cs_player_resource;
struct weapon_info_t;
class c_client;
class c_client_mode;
class c_client_state;
class c_convar;
class c_cs_player;
class c_debug_overlay;
class c_engine;
class c_engine_cvar;
class c_engine_trace;
class c_entity_list;
class c_game_event_manager2;
class c_game_movement;
class c_game_rules;
class c_global_vars;
class c_glow_object_manager;
class c_input;
class c_item_schema;
class c_key_values_system;
class c_localize;
class c_material_system;
class c_model_cache;
class c_model_info;
class c_model_render;
class c_move_helper;
class c_network_string_table_container;
class c_panel;
class c_phys_surface_props;
class c_prediction;
class c_render_view;
class c_static_prop_manager;
class c_studio_render;
class c_surface;
class c_user_cmd;
class c_view_render;
class c_view_render_beams;
class c_weapon_system;
