#include "globals.hpp"
#include "thirdpesron.hpp"
#include "anti_aim.hpp"

void c_thirdperson::run_alive()
{
	if (!HACKS->in_game || !HACKS->local)
		return;

	const bool alive = HACKS->local->is_alive();
	float camera_fraction = 1.f;
	vec3_t camera_forward{};

	if (alive && (g_cfg.binds[tp_b].toggled || HACKS->input->camera_in_third_person))
	{
		vec3_t view_angles{};
		HACKS->engine->get_view_angles(view_angles);
		math::angle_vectors(view_angles, camera_forward);

		const auto view_offset = ANTI_AIM->is_fake_ducking() ?
			vec3_t{ 0.f, 0.f, HACKS->game_movement->get_player_view_offset(false).z + 0.064f } : HACKS->local->view_offset();
		const auto origin = HACKS->local->get_render_origin() + view_offset;

		c_trace_filter_world_and_props_only filter{};
		c_game_trace tr{};
		HACKS->engine_trace->trace_ray(ray_t(origin, origin - (camera_forward * g_cfg.misc.thirdperson_dist), { -16.f, -16.f, -16.f }, { 16.f, 16.f, 16.f }), MASK_NPCWORLDSTATIC, (i_trace_filter*)&filter, &tr);
		camera_fraction = tr.fraction;
	}

	const bool collision_disabled = g_cfg.misc.thirdperson_disable_on_collision && camera_fraction < 1.f;
	if (g_cfg.binds[tp_b].toggled)
	{
		if (alive)
		{
			HACKS->input->camera_in_third_person = !collision_disabled;
			if (collision_disabled)
				HACKS->input->camera_offset.z = 0.f;
		}
		else
		{
			if (HACKS->input->camera_in_third_person)
			{
				HACKS->input->camera_in_third_person = false;
				HACKS->input->camera_offset.z = 0.f;
			}

			if (g_cfg.misc.thirdperson_dead && HACKS->local->observer_mode() == 4)
				HACKS->local->observer_mode() = 5;
		}

		enabled = true;
	}
	else if (enabled)
	{
		HACKS->input->camera_in_third_person = false;
		HACKS->input->camera_offset.z = 0.f;
		enabled = false;
	}

	if (HACKS->input->camera_in_third_person)
	{
		vec3_t offset{};
		offset.z = g_cfg.misc.thirdperson_dist * camera_fraction;
		HACKS->input->camera_offset = { offset.x, offset.y, offset.z };
	}
}

void c_thirdperson::run_dead()
{
}

void c_thirdperson::fix_camera_on_fakeduck(c_view_setup* setup)
{
	if (!HACKS->in_game || !HACKS->local || !HACKS->local->is_alive())
		return;

	if (!ANTI_AIM->is_fake_ducking())
		return;

	setup->origin = HACKS->local->get_render_origin() + vec3_t{0.f, 0.f, HACKS->game_movement->get_player_view_offset(false).z + 0.064f};

	if (HACKS->input->camera_in_third_person)
	{
		vec3_t angles = { HACKS->input->camera_offset.x, HACKS->input->camera_offset.y, 0.f };

		vec3_t forward = {};
		math::angle_vectors(angles, forward);

		math::vector_multiply(setup->origin, -HACKS->input->camera_offset.z, forward, setup->origin);
	}
}