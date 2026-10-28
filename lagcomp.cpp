#include "globals.hpp"
#include "animations.hpp"
#include "server_bones.hpp"
#include "entlistener.hpp"
#include "lagcomp.hpp"

void c_lag_comp::build_roll_matrix(c_cs_player* player, matrix_t* side, int side_index, float& fresh_tick, vec3_t& fresh_angles, clamp_bones_info_t& clamp_info)
{
	RESTORE(player->eye_angles().z);

	math::memcpy_sse(side->roll_matrix, side->matrix, sizeof(side->roll_matrix));
	player->eye_angles().z = 50.f * -side_index;

	auto& builder = side->bone_builder;
	builder.clamp_bones_in_bbox(player, side->roll_matrix, builder.mask, fresh_tick, fresh_angles, clamp_info);
}

void c_lag_comp::clamp_matrix(c_cs_player* player, matrix_t* side, float& fresh_tick, vec3_t& fresh_angles, clamp_bones_info_t& clamp_info) 
{
	auto& builder = side->bone_builder;
	builder.clamp_bones_in_bbox(player, builder.matrix, builder.mask, fresh_tick, fresh_angles, clamp_info);
}

void threaded_update_tick(std::unique_ptr<c_lag_comp>& ptr, c_cs_player* player, float& fresh_tick)
{
	if (!player->is_alive())
		return;

	auto anim = ANIMFIX->get_anims(player->index());
	if (!anim || !anim->ptr || anim->ptr != player || anim->records.empty() || anim->dormant_ticks < 1)
		return;
	
	for (auto& i : anim->records)
		ptr->is_tick_valid(i.break_lc, i.sim_time);
}

void c_lag_comp::update_tick_validation()
{
	if (!HACKS->in_game || !HACKS->local || HACKS->client_state->delta_tick == -1)
		return;

	auto fresh_tick = HACKS->local->is_alive() ? HACKS->predicted_time : HACKS->global_vars->curtime;
	LISTENER_ENTITY->for_each_player([&](c_cs_player* player)
	{
		THREAD_POOL->add_task(threaded_update_tick, std::ref(LAGCOMP), player, std::ref(fresh_tick));
	});

	THREAD_POOL->wait_all();
}