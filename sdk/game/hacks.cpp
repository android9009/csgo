#include "globals.hpp"
#include <utility>
#include "../../runtime/debug_console.hpp"

// Startup of the base, in the order main's c_hacks::init uses, minus every
// feature step (config, listeners, skins, chams materials, hooks, ...).
c_hacks::c_hacks()
{
}

bool c_hacks::init()
{
	while (!(window = FindWindowA("Valve001", NULL)))
		std::this_thread::sleep_for(std::chrono::milliseconds(200));

	while (!GetModuleHandleA("serverbrowser.dll"))
		std::this_thread::sleep_for(std::chrono::milliseconds(200));

	AIRFLOW_DEBUG_LOG("      SDK: modules");
	modules.init(false);
	AIRFLOW_DEBUG_LOG("      SDK: patterns");
	offsets::init();
	// init_interfaces dereferences these; a signature that no longer matches the
	// installed game build is reported here instead of crashing the game.
	const std::pair<const char*, std::uintptr_t> required[] = {
		{ "client_state", offsets::client_state.pointer }, { "d3d_device", offsets::d3d_device.pointer },
		{ "game_rules", offsets::game_rules.pointer }, { "global_vars", offsets::global_vars.pointer },
		{ "glow_object_manager", offsets::glow_object_manager.pointer }, { "input", offsets::input.pointer },
		{ "item_system", offsets::item_system.pointer }, { "move_helper", offsets::move_helper.pointer },
		{ "view_render", offsets::view_render.pointer }, { "view_render_beams", offsets::view_render_beams.pointer },
		{ "weapon_system", offsets::weapon_system.pointer },
	};
	bool patterns_found = true;
	for (const auto& [name, pointer] : required)
	{
		if (pointer)
			continue;
		AIRFLOW_DEBUG_LOGF("[FAIL] SDK: pattern '%s' was not found in this game build", name);
		patterns_found = false;
	}
	if (!patterns_found)
		return false;

	AIRFLOW_DEBUG_LOG("      SDK: interfaces");
	init_interfaces();
	AIRFLOW_DEBUG_LOG("      SDK: convars");
	convars.init();
	AIRFLOW_DEBUG_LOG("      SDK: netvars");
	netvars::init();
	AIRFLOW_DEBUG_LOG("      SDK: ready");

	char path[MAX_PATH]{};
	exe_path = std::string(path, GetModuleFileNameA(NULL, path, MAX_PATH));
	if (exe_path.size() > 8)
	{
		exe_path.erase(exe_path.end() - 8, exe_path.end());
		exe_path.append("\\csgo\\");
	}

	cheat_init = client && engine && entity_list && d3d_device && panel;
	return cheat_init;
}

void c_hacks::init_local_player()
{
	local = **offsets::local.cast<c_cs_player***>();

	if (!local || !local->is_alive())
	{
		weapon = nullptr;
		weapon_info = nullptr;
		return;
	}

	weapon = local->get_handle_entity<c_base_combat_weapon>(local->active_weapon());
	if (weapon)
		weapon_info = weapon_system->get_weapon_data(weapon->item_definition_index());
}
