#include "globals.hpp"
#include "resolver.hpp"
#include "animations.hpp"
#include "server_bones.hpp"
#include "ragebot.hpp"
#include "penetration.hpp"

namespace resolver
{
	inline vec3_t get_point_direction(c_cs_player* player)
	{
		vec3_t fw{}, at_target_fw{};

		math::vector_angles(vec3_t(HACKS->local->origin() - player->origin()), at_target_fw);
		math::angle_vectors(vec3_t(0, at_target_fw.y - 90.f, 0), fw);

		return fw;
	}

	inline void prepare_freestanding(c_cs_player* player)
	{
		auto& info = resolver_info[player->index()];
		auto& freestanding = info.freestanding;

		auto layers = player->animlayers();

		if (!layers || !HACKS->weapon_info || !HACKS->local || !HACKS->local->is_alive() || player->is_bot() || !g_cfg.rage.resolver)
		{
			if (freestanding.updated)
				freestanding.reset();

			return;
		}

		auto weight = layers[6].weight;
		if (weight > 0.75f)
		{
			if (freestanding.updated)
				freestanding.reset();

			return;
		}

		auto& cache = player->bone_cache();
		if (!cache.count() || !cache.base())
			return;

		vec3_t at_target_fw{};

		math::vector_angles(vec3_t(HACKS->local->origin() - player->get_abs_origin()), at_target_fw);

		float at_target = math::normalize_yaw(at_target_fw.y);
		float angle = math::normalize_yaw(player->eye_angles().y);

		const bool sideways_left = std::abs(math::normalize_yaw(angle - math::normalize_yaw(at_target - 90.f))) < 45.f;
		const bool sideways_right = std::abs(math::normalize_yaw(angle - math::normalize_yaw(at_target + 90.f))) < 45.f;

		bool forward = std::abs(math::normalize_yaw(angle - math::normalize_yaw(at_target + 180.f))) < 45.f;
		bool inverse_side = forward && !(sideways_left || sideways_right);

		auto direction = get_point_direction(player);

		matrix3x4_t predicted_matrix[128]{};
		std::memcpy(predicted_matrix, cache.base(), sizeof(predicted_matrix));

		auto store_changed_matrix_data = [&](const vec3_t& new_position, bullet_t& out)
		{
			auto old_abs_origin = player->get_abs_origin();

			math::change_bones_position(predicted_matrix, 128, player->origin(), new_position);
			{
				matrix3x4_t old_cache[128]{};
				player->store_bone_cache(old_cache);
				{
					player->set_abs_origin(new_position);
					player->set_bone_cache(predicted_matrix);

					auto head_pos = cache.base()[8].get_origin();
					out = penetration::simulate(HACKS->local, player, ANIMFIX->get_local_anims()->eye_pos, head_pos);
				}
				player->set_abs_origin(old_abs_origin);
				player->set_bone_cache(old_cache);
			}
			math::change_bones_position(predicted_matrix, 128, new_position, player->origin());
		};

		bullet_t left{}, right{};

		auto left_dir = inverse_side ? (player->origin() + direction * 40.f) : (player->origin() - direction * 40.f);
		store_changed_matrix_data(left_dir, left);

		auto right_dir = inverse_side ? (player->origin() - direction * 40.f) : (player->origin() + direction * 40.f);
		store_changed_matrix_data(right_dir, right);

		if (left.damage > right.damage)
			freestanding.side = side_right;
		else if (left.damage < right.damage)
			freestanding.side = side_left;

		if (freestanding.side)
			freestanding.updated = true;
	}

	inline void prepare_side(c_cs_player* player, anim_record_t* current, anim_record_t* previous)
	{
		auto& info = resolver_info[player->index()];
		info.debug_status = info.resolved ? resolver_status_resolved : resolver_status_unresolved;
		if (!HACKS->weapon_info || !HACKS->local || !HACKS->local->is_alive() || player->is_bot() || !g_cfg.rage.resolver)
		{
			if (info.resolved)
				info.reset();

			// Diagnostics only: preserve why this resolver path was unavailable.
			if (!HACKS->weapon_info)
				info.debug_status = resolver_status_no_weapon_info;
			else if (!HACKS->local)
				info.debug_status = resolver_status_no_local;
			else if (!HACKS->local->is_alive())
				info.debug_status = resolver_status_local_dead;
			else if (player->is_bot())
				info.debug_status = resolver_status_bot;
			else if (!g_cfg.rage.resolver)
				info.debug_status = resolver_status_disabled;

			return;
		}

		auto state = player->animstate();
		if (!state)
		{
			if (info.resolved)
				info.reset();

			info.debug_status = resolver_status_no_animstate;
			return;
		}

		auto hdr = player->get_studio_hdr();
		if (!hdr)
		{
			info.debug_status = resolver_status_no_hdr;
			return;
		}

		if (current->choke < 2)
			info.add_legit_ticks();
		else
			info.add_fake_ticks();

		if (info.is_legit())
		{
			info.resolved = false;
			info.mode = XOR("no fake");
			info.debug_status = resolver_status_no_fake;
			return;
		}

		prepare_freestanding(player);
		auto& freestanding = info.freestanding;

		if (!info.anim_resolve_ticks || std::abs(HACKS->global_vars->tickcount - info.anim_resolve_ticks) >= current->choke)
		{			
			if (info.freestanding.updated)
			{
				info.side = info.freestanding.side;
				info.mode = XOR("freestanding");
			}
			else
			{
				info.side = side_zero;
				info.mode = XOR("static");
			}

			info.resolved = true;
			info.debug_status = resolver_status_resolved;
		}

		if (previous)
		{
			// matrix_orig is built after prepare_side(); use the freshly stored
			// network layers as the current reference instead of an empty matrix.
			const auto& current_layer = current->layers[ANIMATION_LAYER_MOVEMENT_MOVE];
			const auto& previous_layer = previous->matrix_orig.layers[ANIMATION_LAYER_MOVEMENT_MOVE];
			auto accelerating = current_layer.weight >= previous_layer.weight &&
				current_layer.playback_rate >= previous_layer.playback_rate;

			if (!(current->flags.has(FL_ONGROUND)) || !(previous->flags.has(FL_ONGROUND)))
				accelerating = false;

			if (accelerating)
			{
				auto best_match = std::make_pair(side_original, FLT_MAX);

				for (int i = side_left; i <= side_right; ++i)
				{
					auto current_side = ANIMFIX->get_matrix_side(current, i);

					const auto& side_layer = current_side->layers[ANIMATION_LAYER_MOVEMENT_MOVE];
					if (side_layer.sequence != current_layer.sequence)
						continue;

					const auto delta_weight = fabsf(side_layer.weight - current_layer.weight);
					const auto delta_cycle = fabsf(side_layer.cycle - current_layer.cycle);
					const auto delta_rate = fabsf(side_layer.playback_rate - current_layer.playback_rate);
					const auto delta_total = delta_weight + delta_cycle + delta_rate;

					if (delta_total < best_match.second)
						best_match = { static_cast<simulate_side_t>(i), delta_total };

					if (delta_weight < 0.000001f && delta_cycle < 0.000001f && delta_rate < 0.000001f)
						best_match = { static_cast<simulate_side_t>(i), 0.f };
				}

				if (best_match.second < FLT_MAX)
				{
					info.side = best_match.first;
					info.mode = XOR("layers");
					info.anim_resolve_ticks = HACKS->global_vars->tickcount;
					info.resolved = true;
					info.debug_status = resolver_status_resolved;
				}
			}
		}

		if (RAGEBOT->missed_shots[player->index()])
		{
			switch (RAGEBOT->m_missed_anim_side[player->index()])
			{
			case side_zero:
				info.mode = XOR("brute");

				if (RAGEBOT->m_missed_prev_side[player->index()] == side_right)
					info.side = side_left;
				else if (RAGEBOT->m_missed_prev_side[player->index()] == side_left)
					info.side = side_right;
				else if (RAGEBOT->m_missed_prev_side[player->index()] == side_zero)
					info.side = math::random_int(0, 1) ? side_left : side_right;

				info.m_was_first_bruteforce = false;
				info.m_was_second_bruteforce = false;
				break;
			case side_left:
				info.mode = XOR("brute");
				info.side = info.m_was_second_bruteforce ? side_zero : side_right;
				info.m_was_first_bruteforce = true;
				break;
			case side_right:
				info.mode = XOR("brute");
				info.side = info.m_was_first_bruteforce ? side_zero : side_left;
				info.m_was_second_bruteforce = true;
				break;
			}

			info.resolved = true;
			info.debug_status = resolver_status_resolved;
		}
	}

	inline void apply_side(c_cs_player* player, anim_record_t* current, int choke)
	{
		auto& info = resolver_info[player->index()];
		if (!HACKS->weapon_info || !HACKS->local || !HACKS->local->is_alive() || !info.resolved || info.side == side_original || player->is_teammate(false))
			return;

		auto state = player->animstate();
		if (!state)
			return;

		state->abs_yaw = math::normalize_yaw(player->eye_angles().y + state->get_max_rotation() * info.side);
	}
}