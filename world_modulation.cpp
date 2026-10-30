#include "globals.hpp"
#include "legacy ui/legacy_str.h"
#include "world_modulation.hpp"

std::vector<std::string> skybox_list
{
	xor_strs::aa_default.c_str(),
	XOR("cs_tibet"),
	XOR("cs_baggage_skybox_"),
	XOR("italy"),
	XOR("jungle"),
	XOR("office"),
	XOR("sky_cs15_daylight01_hdr"),
	XOR("sky_cs15_daylight02_hdr"),
	XOR("vertigoblue_hdr"),
	XOR("vertigo"),
	XOR("sky_day02_05_hdr"),
	XOR("nukeblank"),
	XOR("sky_venice"),
	XOR("sky_cs15_daylight03_hdr"),
	XOR("sky_cs15_daylight04_hdr"),
	XOR("sky_csgo_cloudy01"),
	XOR("sky_csgo_night02"),
	XOR("sky_csgo_night02b"),
	XOR("sky_csgo_night_flat"),
	XOR("sky_dust"),
	XOR("vietnam"),
	XOR("sky_lunacy"),
	XOR("embassy"),
	XOR("Custom")
};


std::string get_world_map_name()
{
#ifndef LEGACY
	if (!HACKS->client_state)
		return {};

	const char* name = HACKS->client_state->level_name_short;
	return name && name[0] ? std::string(name) : std::string();
#else
	return {};
#endif
}

world_settings_t* get_map_world_settings(bool create)
{
	const auto map_name = get_world_map_name();
	if (map_name.empty())
		return nullptr;

	auto it = g_cfg.misc.world_profiles.find(map_name);
	if (it == g_cfg.misc.world_profiles.end() && create)
	{
		const auto& global = static_cast<const world_settings_t&>(g_cfg.misc);
		it = g_cfg.misc.world_profiles.emplace(map_name, global).first;
	}

	return it != g_cfg.misc.world_profiles.end() ? &it->second : nullptr;
}

world_settings_t& get_world_settings()
{
	if (g_cfg.misc.per_map_world)
	{
		if (auto* profile = get_map_world_settings(false))
			return *profile;
	}

	return g_cfg.misc;
}

void c_world_modulation::override_shadows()
{
	auto& world = get_world_settings();
	bool sunset = (world.world_modulation & 2);

	if (!HACKS->in_game)
	{
		old_sunset_mode = !sunset;

		old_max_shadow_dist = -1.f;
		old_rot_x = -1.f;
		old_rot_y = -1.f;
		old_rot_z = -1.f;
		return;
	}

	if (old_sunset_mode != sunset)
	{
		HACKS->convars.cl_csm_rot_override->fn_change_callbacks.remove_count();
		HACKS->convars.cl_csm_max_shadow_dist->fn_change_callbacks.remove_count();

		HACKS->convars.cl_csm_rot_override->set_value((int)sunset);
		HACKS->convars.cl_csm_max_shadow_dist->set_value(sunset ? 10000.f : -1.f);
		old_sunset_mode = sunset;
	}

	if (old_rot_x != world.sunset_angle.x)
	{
		HACKS->convars.cl_csm_rot_x->fn_change_callbacks.remove_count();

		HACKS->convars.cl_csm_rot_x->set_value(world.sunset_angle.x);
		old_rot_x = world.sunset_angle.x;
	}

	if (old_rot_y != world.sunset_angle.y)
	{
		HACKS->convars.cl_csm_rot_y->fn_change_callbacks.remove_count();

		HACKS->convars.cl_csm_rot_y->set_value(world.sunset_angle.y);
		old_rot_y = world.sunset_angle.y;
	}

	if (old_rot_z != world.sunset_angle.z)
	{
		HACKS->convars.cl_csm_rot_z->fn_change_callbacks.remove_count();

		HACKS->convars.cl_csm_rot_z->set_value(world.sunset_angle.z);
		old_rot_z = world.sunset_angle.z;
	}
}

void c_world_modulation::override_sky_convar()
{
	auto& world = get_world_settings();
	if (!HACKS->in_game || !HACKS->local)
		return;

	bool nightmode = world.skybox > 0;

	world.skybox = std::clamp(world.skybox, 0, 23);

	const auto sky_name = world.skybox == 23 ? world.skybox_name : skybox_list[world.skybox].c_str();
	if (nightmode && old_sky_name != sky_name)
	{
		offsets::load_named_sky.cast<void(__fastcall*)(const char*)>()(sky_name);
		old_sky_name = sky_name;
	}
	else if (!nightmode && old_sky_name != ingame_sky_name)
	{
		offsets::load_named_sky.cast<void(__fastcall*)(const char*)>()(ingame_sky_name.c_str());
		old_sky_name = ingame_sky_name;
	}

	// disable 3d sky
	HACKS->local->skybox_area() = 255;
}

void c_world_modulation::override_prop_color()
{
	auto& world = get_world_settings();
	if (!HACKS->in_game)
		return;

	auto& props_color = world.world_clr[props];
	if (HACKS->static_prop_manager && HACKS->static_prop_manager->props_count && HACKS->static_prop_manager->props_base)
	{
		for (int i = 0; i < HACKS->static_prop_manager->props_count; ++i)
		{
			auto& static_props = HACKS->static_prop_manager->props_base[i];
			static_props.diffuse_modulation.x = !(world.world_modulation & 1) ? 1.f : props_color[0];
			static_props.diffuse_modulation.y = !(world.world_modulation & 1) ? 1.f : props_color[1];
			static_props.diffuse_modulation.w = !(world.world_modulation & 1) ? 1.f : props_color[2];

			auto alpha_property = static_props.alpha_property;
			if (alpha_property)
				alpha_property->set_alpha_modulation(255 * (world.prop_alpha / 100.f));
		}
	}
}

void c_world_modulation::override_world_color(i_material* material, float* r, float* g, float* b)
{
	if (!material || material->is_error_material())
		return;

	auto& world = get_world_settings();
	if (!(world.world_modulation & 1))
		return;

	constexpr auto world_tex = HASH("World textures");
	constexpr auto skybox_tex = HASH("SkyBox textures");
	constexpr auto prop_tex = HASH("StaticProp textures");

	auto name = CONST_HASH(material->get_name());
	auto tex_name = CONST_HASH(material->get_texture_group_name());

	if (name == HASH("smoke"))
		return;

	switch (tex_name)
	{
	case world_tex:
	{
		*r *= world.world_clr[walls][0];
		*g *= world.world_clr[walls][1];
		*b *= world.world_clr[walls][2];
	}
	break;
	case skybox_tex:
	{
		*r *= world.world_clr[sky][0];
		*g *= world.world_clr[sky][1];
		*b *= world.world_clr[sky][2];
	}
	break;
	case prop_tex:
	{
		*r *= world.world_clr[props][0];
		*g *= world.world_clr[props][1];
		*b *= world.world_clr[props][2];
	}
	break;
	}
}