#pragma once
#include "animations.hpp"
#include "anti_aim.hpp"

struct restore_record_t
{
	vec3_t origin{};
	vec3_t abs_origin{};
	vec3_t eye_angles{};

	vec3_t mins{};
	vec3_t maxs{};

	matrix3x4_t bone_cache[128]{};

	INLINE void store(c_cs_player* player)
	{
		origin = player->origin();
		abs_origin = player->get_abs_origin();
		eye_angles = player->eye_angles();

		mins = player->bb_mins();
		maxs = player->bb_maxs();

		player->store_bone_cache(bone_cache);
	}

	INLINE void restore(c_cs_player* player)
	{
		player->origin() = origin;
		player->set_abs_origin(abs_origin);
		player->eye_angles() = eye_angles;

		player->bb_maxs() = maxs;
		player->bb_mins() = mins;

		player->invalidate_bone_cache();
		player->set_bone_cache(bone_cache);
	}

	INLINE void reset()
	{
		origin.reset();
		abs_origin.reset();

		mins.reset();
		maxs.reset();

		std::memset(bone_cache, 0, sizeof(bone_cache));
	}
};

class c_lag_comp
{
private:

public:
	INLINE bool is_tick_valid(bool break_lc, float sim_time)
	{
		auto netchan = HACKS->engine->get_net_channel();
		if (!netchan)
			return false;

		if (HACKS->cl_lagcomp0)
			return true;

		if (break_lc)
			return false;

		// correct is the amount of time we have to correct game time
		float correct = 0.f;

		// add network latency
		correct += HACKS->ping;

		// NOTE:  do these computations in float time, not ticks, to avoid big roundoff error accumulations in the math
		// add view interpolation latency see C_BaseEntity::GetInterpolationAmount()
		correct += HACKS->lerp_time;

		// check bounds [0, sv_maxunlag]
		correct = std::clamp(correct, 0.f, HACKS->max_unlag);

		// use prediction curtime for this
		float curtime = HACKS->local->is_alive() ? HACKS->predicted_time : HACKS->global_vars->curtime;

		// calculate difference between tick sent by player and our latency based tick
		float delta_time = correct - (curtime - sim_time);

		// difference between cmd time and latency is too big > 200ms, use time correction based on latency
		if (fabs(delta_time) > 0.2f)
			return false;

		// get server tickcount with local latancy
		int server_tick = HACKS->global_vars->tickcount + TIME_TO_TICKS(HACKS->ping);

		// correct fakeduck
		if (ANTI_AIM->is_fake_ducking())
			server_tick += 14 - HACKS->client_state->choked_commands;

		// remove all records before that time
		int dead_time = TICKS_TO_TIME(server_tick) - HACKS->max_unlag;

		// if tail is within limits, stop
		if (sim_time < dead_time)
			return false;

		return true;
	}

	INLINE float get_lerp_time()
	{
		return std::max(HACKS->convars.cl_interp->get_float(), HACKS->convars.cl_interp_ratio->get_float() / HACKS->convars.cl_updaterate->get_float());
	}

	INLINE void set_record(c_cs_player* player, anim_record_t* record, matrix3x4_t* matrix)
	{
		auto anim = ANIMFIX->get_anims(player->index());
		if (!anim)
			return;

		player->origin() = record->origin;
		player->set_abs_origin(record->origin);

		player->bb_maxs() = record->maxs;
		player->bb_mins() = record->mins;

		player->invalidate_bone_cache();
		player->set_bone_cache(matrix);
	}

	void build_roll_matrix(c_cs_player* player, matrix_t* side, int side_index, float& fresh_tick, vec3_t& fresh_angles, clamp_bones_info_t& clamp_info);
	void clamp_matrix(c_cs_player* player, matrix_t* side, float& fresh_tick, vec3_t& fresh_angles, clamp_bones_info_t& clamp_info);
	void update_tick_validation();
};

inline auto LAGCOMP = std::make_unique<c_lag_comp>();
