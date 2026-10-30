#include "globals.hpp"
#include "fov_and_view.hpp"

void c_fov_and_view::reset()
{
	if (modifiers_applied)
	{
		if (HACKS->convars.cl_wpn_sway_scale)
			HACKS->convars.cl_wpn_sway_scale->set_value(original_sway);
		if (HACKS->convars.cl_bobamt_lat)
			HACKS->convars.cl_bobamt_lat->set_value(original_bob_lat);
		if (HACKS->convars.cl_bobamt_vert)
			HACKS->convars.cl_bobamt_vert->set_value(original_bob_vert);
		if (HACKS->convars.cl_bob_lower_amt)
			HACKS->convars.cl_bob_lower_amt->set_value(original_lower);
	}

	override_sensitivity = false;
	modifiers_applied = false;
	last_fov = 0;
}

void c_fov_and_view::change_viewmodel_modifiers()
{
	auto& convars = HACKS->convars;
	if (!convars.cl_wpn_sway_scale || !convars.cl_bobamt_lat || !convars.cl_bobamt_vert || !convars.cl_bob_lower_amt)
		return;

	if (!g_cfg.misc.viewmodel_modifiers)
	{
		if (modifiers_applied)
		{
			convars.cl_wpn_sway_scale->set_value(original_sway);
			convars.cl_bobamt_lat->set_value(original_bob_lat);
			convars.cl_bobamt_vert->set_value(original_bob_vert);
			convars.cl_bob_lower_amt->set_value(original_lower);
			modifiers_applied = false;
		}
		return;
	}

	if (!modifiers_applied)
	{
		original_sway = convars.cl_wpn_sway_scale->get_float();
		original_bob_lat = convars.cl_bobamt_lat->get_float();
		original_bob_vert = convars.cl_bobamt_vert->get_float();
		original_lower = convars.cl_bob_lower_amt->get_float();
		modifiers_applied = true;
	}

	convars.cl_wpn_sway_scale->set_value(original_sway * g_cfg.misc.viewmodel_sway * 0.01f);
	convars.cl_bobamt_lat->set_value(original_bob_lat * g_cfg.misc.viewmodel_bob * 0.01f);
	convars.cl_bobamt_vert->set_value(original_bob_vert * g_cfg.misc.viewmodel_bob * 0.01f);
	convars.cl_bob_lower_amt->set_value(original_lower * g_cfg.misc.viewmodel_lower * 0.01f);
}

void c_fov_and_view::change_zoom_sensitivity()
{
	if (!HACKS->local || !HACKS->weapon || !HACKS->weapon_info)
		return;

	bool should_override = g_cfg.misc.fix_sensitivity;
	if (override_sensitivity != should_override)
	{
		HACKS->convars.zoom_sensitivity_ratio_mouse->set_value(should_override ? 0.f : 1.f);
		override_sensitivity = should_override;
	}
}

void c_fov_and_view::change_fov()
{
	if (!HACKS->local || !HACKS->weapon || !HACKS->weapon_info)
		return;

	float newfov = 90.f + g_cfg.misc.fovs[world];

	bool invalid_wpn = !HACKS->weapon->is_sniper() 
		|| HACKS->weapon->item_definition_index() == WEAPON_SG556 
		|| HACKS->weapon->item_definition_index() == WEAPON_AUG;

	int zoom_level = HACKS->weapon->zoom_level();
	if (zoom_level > 1 && g_cfg.misc.skip_second_zoom)
		zoom_level = 1;

	// get fov that should be added to real
	float zoom_fov = HACKS->weapon->get_zoom_fov(zoom_level) + g_cfg.misc.fovs[world];
	float fov_delta = newfov - zoom_fov;
	float zoom_scale = zoom_level > 1 ? g_cfg.misc.zoom_fov_second : g_cfg.misc.fovs[zoom];
	float total_fov = fov_delta * (1.f - zoom_scale * 0.01f);

	HACKS->local->default_fov() = HACKS->local->fov_start() = HACKS->local->fov() = newfov;

	// do anim for snipers zoom
	if (!invalid_wpn)
	{
		float out = zoom_fov + total_fov;

		// smooth zoom in
		if (HACKS->local->is_scoped())
		{
			if (zoom_level > 1)
				HACKS->local->fov_start() = out / (zoom_level - 1);

			HACKS->local->fov() = out / zoom_level;
			last_fov = HACKS->local->fov();

			if (g_cfg.misc.skip_second_zoom || g_cfg.misc.instant_scope)
				HACKS->local->fov_rate() = 0.f;
			else
				HACKS->local->fov_rate() = HACKS->weapon->get_zoom_time(zoom_level);
		}
		else
		{
			// smooth zoom out
			if (last_fov && HACKS->local->fov() == HACKS->local->fov_start())
			{
				HACKS->local->fov_start() = last_fov;
				HACKS->local->fov() = newfov;

				HACKS->local->fov_rate() = 0.05f;
			}
			else
			{
				HACKS->local->fov_start() = newfov;
				last_fov = 0;
			}
		}
	}
	else
		last_fov = 0;
}

void c_fov_and_view::change_fov_dead_and_remove_recoil(c_view_setup* setup)
{
	if (!HACKS->local)
		return;

	if (!HACKS->local->is_alive())
		setup->fov = 90.f + g_cfg.misc.fovs[world];

	if (g_cfg.misc.removals & vis_recoil)
		setup->angles -= HACKS->local->aim_punch_angle() * 0.9f + HACKS->local->view_punch_angle();

	if (HACKS->weapon_info && HACKS->weapon)
#ifdef LEGACY
		*(bool*)((uintptr_t)HACKS->weapon_info + XORN(0x01BD)) = !g_cfg.misc.viewmodel_scope;
#else
		HACKS->weapon_info->hide_viewmodel_in_zoom = !g_cfg.misc.viewmodel_scope;
#endif
}