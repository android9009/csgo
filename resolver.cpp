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

		math::vector_angles(at_target_fw, vec3_t(HACKS->local->origin() - player->origin()));
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

		math::vector_angles(at_target_fw, vec3_t(HACKS->local->origin() - player->get_abs_origin()));

		float at_target = math::normalize_yaw(at_target_fw.y);
		float angle = math::normalize_yaw(player->eye_angles().y);

		const bool sideways_left = std::abs(math::normalize_yaw(angle - math::normalize_yaw(at_target - 90.f))) < 45.f;
		const bool sideways_right = std::abs(math::normalize_yaw(angle - math::normalize_yaw(at_target + 90.f))) < 45.f;

		bool forward = std::abs(math::normalize_yaw(angle - math::normalize_yaw(at_target + 180.f))) < 45.f;
		bool inverse_side = forward && !(sideways_left || sideways_right);

		auto direction = get_point_direction(player);

		static matrix3x4_t predicted_matrix[128]{};
		std::memcpy(predicted_matrix, cache.base(), sizeof(predicted_matrix));

		auto store_changed_matrix_data = [&](const vec3_t& new_position, bullet_t& out)
		{
			auto old_abs_origin = player->get_abs_origin();

			math::change_bones_position(predicted_matrix, 128, player->origin(), new_position);
			{
				static matrix3x4_t old_cache[128]{};
				player->store_bone_cache(old_cache);
				{
					player->set_abs_origin(new_position);
					player->set_bone_cache(predicted_matrix);

					auto head_pos = cache.base()[8].get_origin();
					out = penetration::simulate(HACKS->local, player, ANIMFIX->get_local_anims()->eye_pos, head_pos);
				}
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
		if (!HACKS->weapon_info || !HACKS->local || !HACKS->local->is_alive() || player->is_bot() || !g_cfg.rage.resolver)
		{
			if (info.resolved)
				info.reset();

			return;
		}

		auto state = player->animstate();
		if (!state)
		{
			if (info.resolved)
				info.reset();

			return;
		}

		auto hdr = player->get_studio_hdr();
		if (!hdr)
			return;

		if (current->choke < 2)
			info.add_legit_ticks();
		else
			info.add_fake_ticks();

		if (info.is_legit())
		{
			info.resolved = false;
			info.mode = XOR("no fake");
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
		}

		if (previous)
		{
			auto accelerating = current->matrix_orig.layers[6].weight >= previous->matrix_orig.layers[6].weight &&
				current->matrix_orig.layers[6].playback_rate >= previous->matrix_orig.layers[6].playback_rate;

			if (!(current->flags.has(FL_ONGROUND)) || !(previous->flags.has(FL_ONGROUND)))
				accelerating = false;

			if (accelerating)
			{
				auto best_match = std::make_pair(side_original, FLT_MAX);

				for (int i = side_left; i <= side_right; ++i)
				{
					auto current_side = ANIMFIX->get_matrix_side(current, i);

					if (current_side->layers[6].sequence != current->matrix_orig.layers[6].sequence)
						continue;

					const auto delta_weight = fabsf(current_side->layers[6].weight - current->matrix_orig.layers[6].weight);
					const auto delta_cycle = fabsf(current_side->layers[6].cycle - current->matrix_orig.layers[6].cycle);
					const auto delta_rate = fabsf(current_side->layers[6].playback_rate - current->matrix_orig.layers[6].playback_rate);
					const auto delta_total = delta_weight + delta_cycle + delta_rate;

					if (delta_total < best_match.second)
						best_match = { static_cast<simulate_side_t>(i), delta_total };

					if (delta_weight < 0.000001f || delta_cycle < 0.000001f || delta_rate < 0.000001f)
						best_match = { static_cast<simulate_side_t>(i), 0.f };
				}

				if (best_match.second < FLT_MAX)
				{
					info.side = best_match.first;
					info.mode = XOR("layers");
					info.anim_resolve_ticks = HACKS->global_vars->tickcount;
					info.resolved = true;
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