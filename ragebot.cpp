#include "globals.hpp"
#include "engine_prediction.hpp"
#include "animations.hpp"
#include "lagcomp.hpp"
#include "movement.hpp"
#include "game_movement.hpp"
#include "entlistener.hpp"
#include "exploits.hpp"
#include "event_logs.hpp"
#include "chams.hpp"
#include "anti_aim.hpp"
#include "penetration.hpp"
#include "resolver.hpp"
#include "ragebot.hpp"

namespace
{
	std::mutex multipoint_points_mutex;

	void append_unique_multipoint(rage_player_t* rage, const rage_point_t& point)
	{
		const std::lock_guard<std::mutex> lock(multipoint_points_mutex);
		for (auto& existing : rage->points_to_scan)
		{
			if (existing.aim_point == point.aim_point)
				return;
		}

		rage->points_to_scan.emplace_back(point);
	}

	bool shot_debug_enabled()
	{
		return (g_cfg.visuals.eventlog.logs & 4) != 0;
	}

	const char* shot_matrix_name(const anim_record_t& record)
	{
#ifndef LEGACY
		if (record.visible_flick)
			return "zero/flick";
#endif
		return "original";
	}

	float shot_matrix_desync(const anim_record_t& record)
	{
#ifndef LEGACY
		if (record.visible_flick)
			return record.zero_desync;
#endif
		return record.aim_desync;
	}

	const char* shot_side_name(int side)
	{
		switch (side)
		{
		case side_left:
			return "left";
		case side_zero:
			return "zero";
		case side_right:
			return "right";
		case side_original:
			return "original";
		default:
			return "unknown";
		}
	}

	const char* shot_record_resolver_mode(const anim_record_t& record)
	{
		switch (record.resolver_mode)
		{
		case resolver_mode_no_fake:
			return "no fake";
		case resolver_mode_freestanding:
			return "freestanding";
		case resolver_mode_static:
			return "static";
		case resolver_mode_layers:
			return "layers";
		case resolver_mode_brute:
			return "brute";
		case resolver_mode_unknown:
			return "unknown";
		default:
			return "none";
		}
	}

	const char* shot_hitchance_source_name(shot_hitchance_source_t source)
	{
		switch (source)
		{
		case hitchance_source_nospread:
			return "nospread";
		case hitchance_source_disabled:
			return "disabled";
		case hitchance_source_low_spread:
			return "low_spread";
		default:
			return "seeds";
		}
	}

	const char* shot_resolver_status_name(resolver_debug_status_t status)
	{
		switch (status)
		{
		case resolver_status_resolved:
			return "resolved";
		case resolver_status_no_fake:
			return "no fake";
		case resolver_status_disabled:
			return "disabled";
		case resolver_status_bot:
			return "bot";
		case resolver_status_no_weapon_info:
			return "no weapon info";
		case resolver_status_no_local:
			return "no local";
		case resolver_status_local_dead:
			return "local dead";
		case resolver_status_no_animstate:
			return "no animstate";
		case resolver_status_no_hdr:
			return "no hdr";
		case resolver_status_unresolved:
			return "unresolved";
		default:
			return "none";
		}
	}

	bool shot_hitgroup_matches(const aim_shot_record_t& shot)
	{
		switch (shot.hitbox)
		{
		case HITBOX_HEAD:
			return shot.hitgroup == HITGROUP_HEAD;
		case HITBOX_NECK:
			return shot.hitgroup == HITGROUP_NECK || shot.hitgroup == HITGROUP_HEAD;
		case HITBOX_PELVIS:
		case HITBOX_STOMACH:
			return shot.hitgroup == HITGROUP_STOMACH;
		case HITBOX_THORAX:
		case HITBOX_CHEST:
		case HITBOX_UPPER_CHEST:
			return shot.hitgroup == HITGROUP_CHEST;
		case HITBOX_LEFT_THIGH:
		case HITBOX_LEFT_CALF:
		case HITBOX_LEFT_FOOT:
			return shot.hitgroup == HITGROUP_LEFTLEG;
		case HITBOX_RIGHT_THIGH:
		case HITBOX_RIGHT_CALF:
		case HITBOX_RIGHT_FOOT:
			return shot.hitgroup == HITGROUP_RIGHTLEG;
		case HITBOX_LEFT_HAND:
		case HITBOX_LEFT_UPPER_ARM:
		case HITBOX_LEFT_FOREARM:
			return shot.hitgroup == HITGROUP_LEFTARM;
		case HITBOX_RIGHT_HAND:
		case HITBOX_RIGHT_UPPER_ARM:
		case HITBOX_RIGHT_FOREARM:
			return shot.hitgroup == HITGROUP_RIGHTARM;
		default:
			return true;
		}
	}

	std::string shot_flick_state(const aim_shot_record_t& shot)
	{
		if (shot.record.visible_flick)
			return "selected";
		if (shot.backtrack.flick_records > 0)
			return "observed";
		return "none";
	}

	std::string shot_safest_state(const aim_shot_record_t& shot)
	{
		const auto& debug = shot.backtrack;
		if (debug.safest_history < 0)
			return "none";

		const auto safest_bt = std::abs(TIME_TO_TICKS(shot.target_sim_time - debug.safest_sim_time));
		const auto hitbox = main_utils::hitbox_to_string(debug.safest_hitbox);
		return tfm::format("h%d/v%d box=%s bt=%dt dmg=%d safe=%d/5",
			debug.safest_history, debug.safest_valid, hitbox.c_str(), safest_bt, debug.safest_damage, debug.safest_safety);
	}

	std::string shot_enemy_state(const anim_record_t& record)
	{
		std::string state = record.on_ground ? "ground" : "air";
		if (record.walking)
			state += ",walk";
		if (record.strafing)
			state += ",strafe";
		if (record.fakewalking)
			state += ",fakewalk";
		if (record.shooting)
			state += ",shoot";
		if (record.dormant)
			state += ",dormant";
		return state;
	}

	void log_shot_fire(const aim_shot_record_t& shot)
	{
		if (!shot_debug_enabled())
			return;

		const auto& record = shot.record;
		const auto& debug = shot.backtrack;
		const auto hitbox = main_utils::hitbox_to_string(shot.hitbox);
		const auto resolver_now = shot.resolver.mode.empty() ? std::string("none") : shot.resolver.mode;
		const auto desync_config = shot.desync_config == 0 ? std::string("auto") : std::to_string(shot.desync_config);
		const auto flick = shot_flick_state(shot);
		const auto safest = shot_safest_state(shot);
		const auto backtrack_selection = tfm::format("selected[dmg=%d total=%d safe=%d/5] safest[%s]",
			debug.selected_damage, debug.selected_total_damage, debug.selected_safety, safest.c_str());
		const auto state = shot_enemy_state(record);

		EVENT_LOGS->push_message(tfm::format(
			"AIM FIRE #%d %s | box=%s point=%s eye=%s | hc=%.1f%%(%s) req=%.1f%% | dmg=%d min=%d hp=%d | safe=%d/5 spread=%.5f inacc=%.5f",
			shot.command_number, shot.target_name.c_str(), hitbox.c_str(),
			shot.center_point ? "center" : "multipoint", shot.predicted_eye ? "predicted" : "current",
			shot.hitchance * 100.f, shot_hitchance_source_name(shot.hitchance_source), shot.required_hitchance * 100.f,
			shot.expected_damage, shot.min_damage, shot.target_health, shot.safety,
			shot.spread, shot.inaccuracy), {}, true);

		EVENT_LOGS->push_message(tfm::format(
			"AIM BT #%d | bt=%dt h%d/v%d | history=%d valid=%d scanned=%d | sim=%.3f target=%.3f record_tick=%d cmd_tick=%d | ping=%.1fms lerp=%.1fms | %s",
			shot.command_number, shot.backtrack_ticks, debug.selected_history, debug.selected_valid,
			debug.history_records, debug.valid_records, debug.scanned_records,
			record.sim_time, shot.target_sim_time, TIME_TO_TICKS(record.sim_time), shot.command_tick,
			shot.latency * 1000.f, shot.lerp * 1000.f, backtrack_selection.c_str()), {}, true);

		// The non-variadic tinyformat overloads used by MSVC accept at most
		// sixteen values. Build the detailed line from smaller format blocks.
		const auto record_debug = tfm::format(
			"matrix=%s desync=%.1f cfg=%s eye=%.1f lby=%.1f lby_delta=%.1f | record_res=%s side=%s(%d) resolved=%d record_status=%s",
			shot_matrix_name(record), shot_matrix_desync(record), desync_config.c_str(),
			record.eye_angles.y, record.lby, math::normalize_yaw(record.eye_angles.y - record.lby),
			shot_record_resolver_mode(record), shot_side_name(record.resolver_side), record.resolver_side,
			record.resolver_resolved ? 1 : 0, shot_resolver_status_name(record.resolver_status));
		const auto resolver_debug = tfm::format(
			"now=%s/%s now_status=%s misses=%d legit/fake=%d/%d brute=%d/%d fs=%d/%d",
			resolver_now.c_str(), shot_side_name(shot.resolver.side), shot_resolver_status_name(shot.resolver.debug_status),
			shot.resolver_misses, shot.resolver.legit_ticks, shot.resolver.fake_ticks,
			shot.resolver.m_was_first_bruteforce ? 1 : 0, shot.resolver.m_was_second_bruteforce ? 1 : 0,
			shot.resolver.freestanding.updated ? 1 : 0, shot.resolver.freestanding.side);
		const auto state_debug = tfm::format(
			"state=%s speed=%.1f duck=%.2f choke=%d | flick=%s shift_filtered=%d shifted=%d break_lc=%d",
			state.c_str(), record.velocity.length_2d(), record.duck_amt, record.choke,
			flick.c_str(), debug.shifted_records, record.tickbase_shift ? 1 : 0, record.break_lc ? 1 : 0);

		EVENT_LOGS->push_message(tfm::format("AIM RES #%d | %s | %s | %s",
			shot.command_number, record_debug.c_str(), resolver_debug.c_str(), state_debug.c_str()), {}, true);
	}

	void log_shot_outcome(const aim_shot_record_t& shot, const char* result, const std::string& reason = {})
	{
		if (!shot_debug_enabled())
			return;

		const auto hit = shot.damage >= 0;
		const auto timeout = std::string(result) == "TIMEOUT";
		const auto actual_damage_value = hit ? shot.damage : 0;
		const auto actual_damage = timeout ? std::string("?") : std::to_string(actual_damage_value);
		const auto damage_delta = timeout ? std::string("?") : std::to_string(actual_damage_value - shot.expected_damage);
		const auto remaining_health = hit ? std::to_string(shot.remaining_health) : std::string("?");
		const auto group_match = hit ? (shot_hitgroup_matches(shot) ? "1" : "0") : "?";
		const auto wanted_hitbox = main_utils::hitbox_to_string(shot.hitbox);
		const auto actual_hitgroup = hit ? main_utils::hitgroup_to_string(shot.hitgroup)
			: timeout ? std::string("unknown") : std::string("none");
		const auto flick = shot_flick_state(shot);
		const auto impact = shot.impact_fire
			? tfm::format("error=%.1fu aim=%.1fu impact=%.1fu", shot.impact.dist_to(shot.point),
				shot.start.dist_to(shot.point), shot.start.dist_to(shot.impact))
			: std::string("none");
		const auto age_ms = (HACKS->predicted_time - shot.time) * 1000.f;

		const auto damage_debug = tfm::format(
			"reason=%s actual=%s expected=%d delta=%s min=%d | wanted=%s hitgroup=%s group_match=%s hp=%d->%s",
			reason.empty() ? "-" : reason.c_str(), actual_damage.c_str(), shot.expected_damage,
			damage_delta.c_str(), shot.min_damage, wanted_hitbox.c_str(), actual_hitgroup.c_str(),
			group_match, shot.target_health, remaining_health.c_str());
		const auto aim_debug = tfm::format(
			"hc=%.1f%%(%s) bt=%dt h%d/v%d | matrix=%s desync=%.1f record_res=%s side=%s now_status=%s flick=%s",
			shot.hitchance * 100.f, shot_hitchance_source_name(shot.hitchance_source), shot.backtrack_ticks,
			shot.backtrack.selected_history, shot.backtrack.selected_valid, shot_matrix_name(shot.record),
			shot_matrix_desync(shot.record), shot_record_resolver_mode(shot.record),
			shot_side_name(shot.record.resolver_side), shot_resolver_status_name(shot.resolver.debug_status), flick.c_str());
		const auto event_debug = tfm::format(
			"impact=%s events=%d/%d age=%.1fms spread=%.5f inacc=%.5f",
			impact.c_str(), shot.fire ? 1 : 0, shot.impact_fire ? 1 : 0, age_ms, shot.spread, shot.inaccuracy);

		EVENT_LOGS->push_message(tfm::format("AIM %s #%d %s | %s | %s | %s",
			result, shot.command_number, shot.target_name.c_str(), damage_debug.c_str(), aim_debug.c_str(), event_debug.c_str()), {}, true);
	}
}

void draw_hitbox__(c_cs_player* player, matrix3x4_t* bones, int idx, int idx2, bool dur = false)
{
	auto studio_model = HACKS->model_info->get_studio_model(player->get_model());
	if (!studio_model)
		return;

	auto hitbox_set = studio_model->hitbox_set(0);
	if (!hitbox_set)
		return;

	for (int i = 0; i < hitbox_set->num_hitboxes; i++)
	{
		auto hitbox = hitbox_set->hitbox(i);
		if (!hitbox)
			continue;

		vec3_t vMin, vMax;
		math::vector_transform(hitbox->min, bones[hitbox->bone], vMin);
		math::vector_transform(hitbox->max, bones[hitbox->bone], vMax);

		if (hitbox->radius != -1.f)
			HACKS->debug_overlay->add_capsule_overlay(vMin, vMax, hitbox->radius, 255, 255 * idx, 255 * idx2, 150, dur ? HACKS->global_vars->interval_per_tick * 2 : 5.f, 0, 1);
	}
}

bool can_hit_hitbox(const vec3_t& start, const vec3_t& end, rage_player_t* rage, int hitbox, matrix3x4_t* matrix, anim_record_t* record)
{
	auto model = rage->player->get_model();
	if (!model)
		return false;

	auto studio_model = HACKS->model_info->get_studio_model(rage->player->get_model());
	auto set = studio_model->hitbox_set(0);

	if (!set)
		return false;

	auto studio_box = set->hitbox(hitbox);
	if (!studio_box)
		return false;

	vec3_t min, max;

	math::vector_transform(studio_box->min, matrix[studio_box->bone], min);
	math::vector_transform(studio_box->max, matrix[studio_box->bone], max);

	if (studio_box->radius != -1.f)
		return math::segment_to_segment(start, end, min, max) < studio_box->radius;

	matrix3x4_t rotation{}, hitbox_matrix{};
	rotation.angle_matrix(studio_box->rotation);
	math::contact_transforms(matrix[studio_box->bone], rotation, hitbox_matrix);

	vec3_t local_start{}, local_end{};
	math::vector_i_transform(start, hitbox_matrix, local_start);
	math::vector_i_transform(end, hitbox_matrix, local_end);

	auto local_delta = local_end - local_start;
	return math::intersect_line_with_bb(local_start, local_delta, studio_box->min, studio_box->max);
}

float get_point_accuracy(rage_player_t* rage, vec3_t& eye_pos, const rage_point_t& point, matrix3x4_t* matrix, anim_record_t* record)
{
	static auto weapon_accuracy_nospread = HACKS->convars.weapon_accuracy_nospread;
	if (weapon_accuracy_nospread && weapon_accuracy_nospread->get_bool())
		return 1.f;

	auto predicted_info = ENGINE_PREDICTION->get_networked_vars(HACKS->cmd->command_number);

	auto hits = 0;

	auto spread = predicted_info->spread + predicted_info->inaccuracy;
	auto angle = math::calc_angle(eye_pos, point.aim_point).normalized_angle();

	vec3_t forward, right, up;
	math::angle_vectors(angle, &forward, &right, &up);

	for (auto i = 1; i <= 6; ++i)
	{
		for (auto j = 0; j < 8; ++j)
		{
			auto current_spread = spread * ((float)i / 6.f);

			float value = (float)j / 8.0f * (M_PI * 2.f);

			auto direction_cos = std::cos(value);
			auto direction_sin = std::sin(value);

			auto spread_x = direction_sin * current_spread;
			auto spread_y = direction_cos * current_spread;

			vec3_t direction{};
			direction.x = forward.x + spread_x * right.x + spread_y * up.x;
			direction.y = forward.y + spread_x * right.y + spread_y * up.y;
			direction.z = forward.z + spread_x * right.z + spread_y * up.z;

			auto end = eye_pos + direction * HACKS->weapon_info->range;

			if (can_hit_hitbox(eye_pos, end, rage, point.hitbox, matrix, record))
				hits++;
		}
	}

	return (float)hits / 48.f;
}

bool c_ragebot::can_fire(bool ignore_revolver)
{
	if (!HACKS->local || !HACKS->weapon)
		return false;

	if (HACKS->cmd->weapon_select != 0)
		return false;

	if (!HACKS->weapon_info)
		return false;

	if (HACKS->local->flags().has(FL_ATCONTROLS))
		return false;

	if (HACKS->local->wait_for_no_attack())
		return false;

	if (HACKS->local->is_defusing())
		return false;

	if (HACKS->weapon_info->weapon_type >= 1 && HACKS->weapon_info->weapon_type <= 6 && HACKS->weapon->clip1() < 1)
		return false;

	if (HACKS->local->player_state() > 0)
		return false;

	auto weapon_index = HACKS->weapon->item_definition_index();
	if ((weapon_index == WEAPON_GLOCK || weapon_index == WEAPON_FAMAS) && HACKS->weapon->burst_shots_remaining() > 0)
		return HACKS->predicted_time >= HACKS->weapon->next_burst_shot();

	// TO-DO: auto revolver detection
	if (weapon_index == WEAPON_REVOLVER && !ignore_revolver)
		return revolver_fire;

	float next_attack = HACKS->local->next_attack();
	float next_primary_attack = HACKS->weapon->next_primary_attack();

	return HACKS->predicted_time >= next_attack && HACKS->predicted_time >= next_primary_attack;
}

bool c_ragebot::is_shooting()
{
	if (!HACKS->weapon)
		return false;

	bool attack2 = HACKS->cmd->buttons.has(IN_ATTACK2);
	bool attack = HACKS->cmd->buttons.has(IN_ATTACK);

	short weapon_index = HACKS->weapon->item_definition_index();

	if (weapon_index == WEAPON_C4)
		return false;

	if ((weapon_index == WEAPON_GLOCK || weapon_index == WEAPON_FAMAS) && HACKS->weapon->burst_shots_remaining() > 0)
		return HACKS->predicted_time >= HACKS->weapon->next_burst_shot();

	if (HACKS->weapon->is_grenade())
		return !HACKS->weapon->pin_pulled() && HACKS->weapon->throw_time() > 0.f && HACKS->weapon->throw_time() < HACKS->predicted_time;

	auto can_fire_now = can_fire();
	if (weapon_index == WEAPON_REVOLVER)
		return attack && can_fire_now;

	if (HACKS->weapon->is_knife())
		return (attack || attack2) && can_fire_now;

	return attack && can_fire_now;
}

void c_ragebot::update_hitboxes()
{
	if (HACKS->weapon->is_taser())
	{
		hitboxes.emplace_back(HITBOX_STOMACH);
		hitboxes.emplace_back(HITBOX_PELVIS);
		return;
	}

	if (rage_config.hitboxes & head)
		hitboxes.emplace_back(HITBOX_HEAD);

	if (rage_config.hitboxes & chest)
		hitboxes.emplace_back(HITBOX_CHEST);

	if (rage_config.hitboxes & stomach)
		hitboxes.emplace_back(HITBOX_STOMACH);

	if (rage_config.hitboxes & pelvis)
		hitboxes.emplace_back(HITBOX_PELVIS);

	if (rage_config.hitboxes & arms_)
	{
		hitboxes.emplace_back(HITBOX_LEFT_UPPER_ARM);
		hitboxes.emplace_back(HITBOX_RIGHT_UPPER_ARM);
	}

	if (rage_config.hitboxes & legs)
	{
		hitboxes.emplace_back(HITBOX_LEFT_FOOT);
		hitboxes.emplace_back(HITBOX_RIGHT_FOOT);
	}
}

multipoints_t c_ragebot::get_points(c_cs_player* player, int hitbox, matrix3x4_t* matrix)
{
	auto local_anims = ANIMFIX->get_local_anims();
	multipoints_t points;

	const model_t* model = player->get_model();
	if (!model)
		return points;

	studiohdr_t* hdr = HACKS->model_info->get_studio_model(model);
	if (!hdr)
		return points;

	mstudiohitboxset_t* set = hdr->hitbox_set(player->hitbox_set());
	if (!set)
		return points;

	mstudiobbox_t* bbox = set->hitbox(hitbox);
	if (!bbox)
		return points;

	if (bbox->radius <= 0.f)
	{
		matrix3x4_t rot_matrix = { };
		rot_matrix.angle_matrix(bbox->rotation);

		matrix3x4_t mat = { };
		math::contact_transforms(matrix[bbox->bone], rot_matrix, mat);

		auto origin = mat.get_origin();
		auto center = (bbox->min + bbox->max) * 0.5f;

		if (hitbox == HITBOX_LEFT_FOOT || hitbox == HITBOX_RIGHT_FOOT)
		{
			float d1 = (bbox->min.z - center.z) * 0.875f;
			if (hitbox == HITBOX_LEFT_FOOT)
				d1 *= -1.f;

			points.emplace_back(vec3_t{ center.x, center.y, center.z + d1 }, false);

			float d2 = (bbox->min.x - center.x) * 0.5f;
			float d3 = (bbox->max.x - center.x) * 0.5f;

			points.emplace_back(vec3_t{ center.x + d2, center.y, center.z }, false);
			points.emplace_back(vec3_t{ center.x + d3, center.y, center.z }, false);
		}
		else
			points.emplace_back(center, true);

		if (points.empty())
			return points;

		for (auto& p : points)
		{
			p.first = { p.first.dot(mat.mat[0]), p.first.dot(mat.mat[1]), p.first.dot(mat.mat[2]) };
			p.first += origin;
		}
	}
	else
	{
		vec3_t max = bbox->max;
		vec3_t min = bbox->min;
		auto center = (bbox->min + bbox->max) / 2.f;

		auto dynamic_scale = get_dynamic_scale(center, bbox->radius);

		auto head_scale = rage_config.scale_head != -1 ? rage_config.scale_head * 0.01f : dynamic_scale;
		auto body_scale = rage_config.scale_body != -1 ? rage_config.scale_body * 0.01f : dynamic_scale;

		auto body_slider = 0.35f * body_scale;
		auto r = bbox->radius * body_slider, r2 = bbox->radius * (body_slider * 2.f - 1.f);

		switch (hitbox)
		{
		case HITBOX_HEAD:
		{
			points.emplace_back(center, true);

			float head_slider = 0.75f * head_scale;

			float r = bbox->radius * head_slider;
			points.emplace_back(vec3_t{ max.x + (MATRIX_HEAD_ROTATION * r), max.y + (-MATRIX_HEAD_ROTATION * r), max.z }, false);

			vec3_t right{ max.x, max.y, max.z + r };
			points.emplace_back(right, false);

			vec3_t left{ max.x, max.y, max.z - r };
			points.emplace_back(left, false);

			points.emplace_back(vec3_t{ max.x, max.y - r, max.z }, false);

			auto state = player->animstate();
			if (state && state->velocity_length_xy <= 0.1f && player->eye_angles().x <= 75.f)
				points.emplace_back(vec3_t{ max.x - r, max.y, max.z }, false);
		}
		break;
		case HITBOX_UPPER_CHEST:
		{
			points.emplace_back(center, true);

			points.emplace_back(vec3_t{ center.x, center.y, max.z + r }, false);
			points.emplace_back(vec3_t{ center.x, center.y, min.z - r }, false);
		}
		break;
		case HITBOX_PELVIS:
		{
			points.emplace_back(center, true);

			points.emplace_back(vec3_t{ center.x, center.y, max.z + r }, false);
			points.emplace_back(vec3_t{ center.x, center.y, min.z - r }, false);
		}
		break;
		case HITBOX_CHEST:
		case HITBOX_THORAX:
		{
			points.emplace_back(center, true);

			points.emplace_back(vec3_t{ center.x, center.y, max.z + r }, false);
			points.emplace_back(vec3_t{ center.x, center.y, min.z - r }, false);
			points.emplace_back(vec3_t{ center.x, max.y - r, center.z }, false);
		}
		break;
		case HITBOX_STOMACH:
		{
			points.emplace_back(center, true);

			points.emplace_back(vec3_t{ center.x, center.y, min.z + r }, false);
			points.emplace_back(vec3_t{ center.x, center.y, max.z - r }, false);
			points.emplace_back(vec3_t{ center.x, max.y - r, center.z }, false);
		}
		break;
		case HITBOX_RIGHT_CALF:
		case HITBOX_LEFT_CALF:
		{
			points.emplace_back(center, true);
			points.emplace_back(vec3_t{ max.x - (bbox->radius / 2.f), max.y, max.z }, false);
		}
		break;
		case HITBOX_RIGHT_UPPER_ARM:
		case HITBOX_LEFT_UPPER_ARM:
		{
			points.emplace_back(center, true);
			points.emplace_back(vec3_t{ max.x + bbox->radius, center.y, center.z }, false);
		}
		break;
		default:
			points.emplace_back(center, true);
			break;
		}

		if (points.empty())
			return points;

		for (auto& p : points)
			math::vector_transform(p.first, matrix[bbox->bone], p.first);
	}

	return points;
}

void c_ragebot::run_stop()
{
	if (!HACKS->weapon || HACKS->client_state->delta_tick == -1)
		return;

	if (!HACKS->weapon_info || !HACKS->weapon || !trigger_stop)
		return;

	auto force_acc = rage_config.quick_stop_options & force_accuracy;

	auto max_speed = HACKS->local->is_scoped() ? HACKS->weapon_info->max_speed_alt : HACKS->weapon_info->max_speed;
	max_speed *= 0.34f;

	auto velocity = HACKS->local->velocity();
	if (velocity.length_2d() <= 15.f)
		return;

	if (velocity.length_2d() <= max_speed)
	{
		game_movement::modify_move(*HACKS->cmd, velocity, max_speed);

		if (force_acc)
			should_shot = false;

		return;
	}

	game_movement::force_stop();

	if (force_acc)
		should_shot = true;
}

void c_ragebot::auto_pistol()
{
	if (!HACKS->weapon)
		return;

	auto index = HACKS->weapon->item_definition_index();
	if (index == WEAPON_C4
		|| index == WEAPON_HEALTHSHOT
		|| index == WEAPON_REVOLVER
		|| (index == WEAPON_GLOCK || index == WEAPON_FAMAS) && HACKS->weapon->burst_shots_remaining() > 0)
		return;

	if (HACKS->weapon->is_misc_weapon() && !HACKS->weapon->is_knife())
		return;

	auto next_attack = HACKS->local->next_attack();
	auto next_primary_attack = HACKS->weapon->next_primary_attack();
	auto next_secondary_attack = HACKS->weapon->next_secondary_attack();

	if (HACKS->predicted_time < next_attack || HACKS->predicted_time < next_primary_attack)
	{
		if (HACKS->cmd->buttons.has(IN_ATTACK))
			HACKS->cmd->buttons.remove(IN_ATTACK);
	}

	if (HACKS->predicted_time < next_secondary_attack)
	{
		if (HACKS->cmd->buttons.has(IN_ATTACK2))
			HACKS->cmd->buttons.remove(IN_ATTACK2);
	}
}

void c_ragebot::force_scope()
{
	if (!rage_config.auto_scope)
		return;

	bool able_to_zoom = HACKS->predicted_time >= HACKS->weapon->next_secondary_attack();
	if (able_to_zoom && HACKS->weapon->zoom_level() < 1 && HACKS->weapon->is_sniper() && !HACKS->cmd->buttons.has(IN_ATTACK2))
		HACKS->cmd->buttons.force(IN_ATTACK2);
}

bool c_ragebot::should_stop(const rage_point_t& point)
{
	if (HACKS->weapon->is_taser())
		return false;

	auto unpredicted_vars = ENGINE_PREDICTION->get_unpredicted_vars();
	if (!unpredicted_vars)
		return false;

	if (!(rage_config.quick_stop_options & between_shots) && !can_fire())
		return false;

	if (!MOVEMENT->on_ground() && !(rage_config.quick_stop_options & in_air))
		return false;

	return true;
}

void c_ragebot::update_predicted_eye_pos()
{
	auto unpredicted_vars = ENGINE_PREDICTION->get_initial_vars();
	if (!unpredicted_vars)
		return;

	auto anim = ANIMFIX->get_local_anims();
	if (!anim)
		return;

	auto max_speed = HACKS->local->is_scoped() ? HACKS->weapon_info->max_speed_alt : HACKS->weapon_info->max_speed;
	auto velocity = unpredicted_vars->velocity;

	auto speed = std::max<float>(velocity.length_2d(), 1.f);
	auto max_stop_ticks = std::max<int>(((speed / max_speed) * 5) - 1, 0);

	auto max_predict_ticks = std::clamp(max_stop_ticks, 0, 14);
	if (max_predict_ticks == 0)
	{
		predicted_eye_pos = anim->eye_pos;
		return;
	}

	auto last_predicted_velocity = velocity;
	for (int i = 0; i < max_predict_ticks; ++i)
	{
		auto pred_velocity = velocity * TICKS_TO_TIME(i + 1);

		auto origin = anim->eye_pos + pred_velocity;
		auto flags = HACKS->local->flags();

		game_movement::extrapolate(HACKS->local, origin, pred_velocity, flags, flags.has(FL_ONGROUND));

		last_predicted_velocity = pred_velocity;
	}

	predicted_eye_pos = anim->eye_pos + last_predicted_velocity;
}

void c_ragebot::prepare_players_for_scan()
{
	LISTENER_ENTITY->for_each_player([&](c_cs_player* player)
	{
		auto& rage = rage_players[player->index()];

		if (!player->is_alive() || player->dormant() || player->has_gun_game_immunity())
		{
			if (rage.valid)
				rage.reset();

			return;
		}

		if (rage.player != player)
		{
			rage.reset();
			rage.player = player;
			return;
		}

		rage.distance = HACKS->local->origin().dist_to(player->origin());
		rage.valid = true;

		++rage_player_iter;
	});
}

std::vector<rage_point_t> get_hitbox_points(int damage, std::vector<int>& hitboxes, vec3_t& eye_pos, vec3_t& predicted_eye_pos, rage_player_t* rage, anim_record_t* record, bool predicted = false)
{
	if (hitboxes.empty())
		return {};

	std::vector<rage_point_t> out{};
	out.reserve(hitboxes.size());

	auto matrix_to_aim = record->get_aim_matrix();
	LAGCOMP->set_record(rage->player, record, matrix_to_aim);

	auto local_anims = ANIMFIX->get_local_anims();
	auto& start_eye_pos = predicted ? predicted_eye_pos : local_anims->eye_pos;

	for (auto& hitbox : hitboxes)
	{
		auto aim_point = rage->player->get_hitbox_position(hitbox, matrix_to_aim);
		auto bullet = penetration::simulate(HACKS->local, rage->player, start_eye_pos, aim_point);

		if (bullet.damage < damage
			|| bullet.traced_target == nullptr
			|| bullet.traced_target != rage->player
			|| HACKS->weapon->is_taser() && bullet.penetration_count < 4)
			continue;

		rage_point_t point{};
		point.center = true;
		point.hitbox = hitbox;
		point.damage = bullet.damage;
		point.aim_point = aim_point;
		point.predicted_eye_pos = predicted;

#ifndef LEGACY
		point.safety = [&]()
		{
			auto safety = 0;

			matrix3x4_t* matrices[]
			{
				record->matrix_left.matrix,
				record->matrix_left.roll_matrix,
				record->matrix_right.matrix,
				record->matrix_right.roll_matrix,
				record->matrix_zero.matrix,
			};

			for (int i = 0; i < 5; ++i)
			{
				if (can_hit_hitbox(start_eye_pos, aim_point, rage, hitbox, matrices[i], record))
					++safety;
			}

			return safety;
		}();
#endif

		out.emplace_back(point);
	}

	return out;
}

void pre_cache_centers(int damage, std::vector<int>& hitboxes, vec3_t& predicted_eye_pos, rage_player_t* rage)
{
	rage->reset_hitscan();
	auto anim = ANIMFIX->get_local_anims();

	auto lagcomp = ANIMFIX->get_anims(rage->player->index());
	if (!lagcomp || lagcomp->records.empty())
		return;

	auto scan_record = [&](anim_record_t* record)
	{
		auto points = get_hitbox_points(damage, hitboxes, predicted_eye_pos, predicted_eye_pos, rage, record, true);
		if (points.empty())
			points = get_hitbox_points(damage, hitboxes, anim->eye_pos, predicted_eye_pos, rage, record, false);

		return points;
	};

	rage->restore.store(rage->player);

	anim_record_t* best = nullptr;
	std::vector<rage_point_t> best_points{};
	auto best_damage = -1;
	auto best_total_damage = -1;

	auto consider_record = [&](anim_record_t* record, int history_index, int valid_index)
	{
		auto points = scan_record(record);
		if (points.empty())
			return;

		auto highest_damage = 0;
		auto total_damage = 0;
		auto record_safety = -1;
		auto record_safety_damage = -1;
		auto record_safety_hitbox = 0;
		for (const auto& point : points)
		{
			highest_damage = std::max(highest_damage, point.damage);
			total_damage += point.damage;
			if (point.damage > record_safety_damage
				|| (point.damage == record_safety_damage && point.safety > record_safety))
			{
				record_safety = point.safety;
				record_safety_damage = point.damage;
				record_safety_hitbox = point.hitbox;
			}
		}

		auto& debug = rage->backtrack;
		if (record_safety > debug.safest_safety
			|| (record_safety == debug.safest_safety && record_safety_damage > debug.safest_damage))
		{
			debug.safest_history = history_index;
			debug.safest_valid = valid_index;
			debug.safest_damage = record_safety_damage;
			debug.safest_safety = record_safety;
			debug.safest_hitbox = record_safety_hitbox;
			debug.safest_sim_time = record->sim_time;
		}

		// Records are visited newest to oldest, so equal scores keep fresher data.
		if (highest_damage > best_damage || (highest_damage == best_damage && total_damage > best_total_damage))
		{
			best = record;
			best_damage = highest_damage;
			best_total_damage = total_damage;
			best_points = std::move(points);

			debug.selected_history = history_index;
			debug.selected_valid = valid_index;
			debug.selected_damage = highest_damage;
			debug.selected_total_damage = total_damage;
			debug.selected_safety = record_safety;
		}
	};

	std::vector<anim_record_t*> valid_records{};
	std::vector<int> valid_history_indices{};
	valid_records.reserve(lagcomp->records.size());
	valid_history_indices.reserve(lagcomp->records.size());

	rage->backtrack.history_records = static_cast<int>(lagcomp->records.size());
	auto history_index = 0;
	for (auto& record : lagcomp->records)
	{
		if (record.tickbase_shift)
			++rage->backtrack.shifted_records;
		if (record.visible_flick)
			++rage->backtrack.flick_records;

		if (record.filled && !record.dormant && !record.tickbase_shift
			&& LAGCOMP->is_tick_valid(record.break_lc, record.sim_time))
		{
			valid_records.emplace_back(&record);
			valid_history_indices.emplace_back(history_index);
		}
		++history_index;
	}
	rage->backtrack.valid_records = static_cast<int>(valid_records.size());

	constexpr size_t max_record_scans = 4;
	std::vector<size_t> candidate_indices{};
	candidate_indices.reserve(max_record_scans + 1);

	if (valid_records.size() <= max_record_scans)
	{
		for (size_t i = 0; i < valid_records.size(); ++i)
			candidate_indices.emplace_back(i);
	}
	else
	{
		candidate_indices = {
			0,
			valid_records.size() / 3,
			valid_records.size() * 2 / 3,
			valid_records.size() - 1
		};

#ifndef LEGACY
		// A measured flick can sit just behind the front record. Always include
		// the newest one instead of losing it to the backtrack sampling stride.
		for (size_t i = 0; i < valid_records.size(); ++i)
		{
			if (valid_records[i]->visible_flick)
			{
				candidate_indices.emplace_back(i);
				break;
			}
		}
#endif

		std::sort(candidate_indices.begin(), candidate_indices.end());
		candidate_indices.erase(std::unique(candidate_indices.begin(), candidate_indices.end()), candidate_indices.end());
	}

	rage->backtrack.scanned_records = static_cast<int>(candidate_indices.size());
	for (const auto index : candidate_indices)
		consider_record(valid_records[index], valid_history_indices[index], static_cast<int>(index));

	rage->restore.restore(rage->player);

	if (best && !best_points.empty())
	{
		rage->points_to_scan = std::move(best_points);
		rage->points_to_scan.reserve(MAX_SCANNED_POINTS);
		rage->start_scans = true;
		rage->hitscan_record = best;
	}
}

void get_result(bool& out, const vec3_t& start, const vec3_t& end, rage_player_t* rage, int hitbox, matrix3x4_t* matrix, anim_record_t* record)
{
	out = can_hit_hitbox(start, end, rage, hitbox, matrix, record);
}

bool hitchance(vec3_t eye_pos, rage_player_t& rage, const rage_point_t& point, anim_record_t* record, const float& chance, matrix3x4_t* matrix, float* hitchance_out = nullptr, shot_hitchance_source_t* source = nullptr)
{
	if (hitchance_out)
		*hitchance_out = 0.f;
	if (source)
		*source = hitchance_source_seeds;

	static auto weapon_accuracy_nospread = HACKS->convars.weapon_accuracy_nospread;
	if (weapon_accuracy_nospread && weapon_accuracy_nospread->get_bool())
	{
		if (hitchance_out)
			*hitchance_out = 1.f;
		if (source)
			*source = hitchance_source_nospread;

		return true;
	}

	if (chance <= 0.f)
	{
		if (hitchance_out)
			*hitchance_out = 1.f;
		if (source)
			*source = hitchance_source_disabled;

		return true;
	}

	auto networked_vars = ENGINE_PREDICTION->get_networked_vars(HACKS->cmd->command_number);
	if (!networked_vars || precomputed_seeds.size() < MAX_SEEDS)
		return false;

	auto matrix_to_aim = record->get_aim_matrix();
	auto current_bones = matrix ? matrix : matrix_to_aim;

	if ((HACKS->ideal_inaccuracy + 0.0005f) >= networked_vars->inaccuracy && networked_vars->spread <= 0.00001f)
	{
		if (hitchance_out)
			*hitchance_out = 1.f;
		if (source)
			*source = hitchance_source_low_spread;

		return true;
	}

	rage.restore.store(rage.player);
	LAGCOMP->set_record(rage.player, record, current_bones);

	const auto start = eye_pos;
	const auto aim_angle = math::calc_angle(start, point.aim_point);

	vec3_t forward, right, up;
	math::angle_vectors(aim_angle, &forward, &right, &up);

	const auto weapon_index = HACKS->weapon->item_definition_index();
	const auto revolver_secondary = weapon_index == WEAPON_REVOLVER && HACKS->cmd->buttons.has(IN_ATTACK2);
	const auto required_hits = std::max(1, static_cast<int>(std::ceil(std::clamp(chance, 0.f, 1.f) * MAX_SEEDS)));

	auto current = 0;
	auto passed = false;
	for (auto i = 0; i < MAX_SEEDS; i++)
	{
		const auto& seed = precomputed_seeds[i];
		auto inaccuracy_radius = seed.inaccuracy_radius;
		auto spread_radius = seed.spread_radius;

		if (revolver_secondary)
		{
			inaccuracy_radius = 1.f - inaccuracy_radius * inaccuracy_radius;
			spread_radius = 1.f - spread_radius * spread_radius;
		}
		else if (weapon_index == WEAPON_NEGEV && networked_vars->recoil_index < 3.f)
		{
			for (auto j = 3; j > networked_vars->recoil_index; --j)
			{
				inaccuracy_radius *= inaccuracy_radius;
				spread_radius *= spread_radius;
			}

			inaccuracy_radius = 1.f - inaccuracy_radius;
			spread_radius = 1.f - spread_radius;
		}

		const auto inaccuracy = inaccuracy_radius * networked_vars->inaccuracy;
		const auto spread = spread_radius * networked_vars->spread;
		const auto spread_x = seed.inaccuracy_cos * inaccuracy + seed.spread_cos * spread;
		const auto spread_y = seed.inaccuracy_sin * inaccuracy + seed.spread_sin * spread;
		const auto direction = (forward + right * spread_x + up * spread_y).normalized();
		const auto end = start + direction * HACKS->weapon_info->range;

		if (can_hit_hitbox(start, end, &rage, point.hitbox, current_bones, record))
			++current;

		const auto sampled = i + 1;
		if (hitchance_out)
			*hitchance_out = static_cast<float>(current) / static_cast<float>(sampled);

		if (current >= required_hits)
		{
			passed = true;

			// A requested percentage needs the full sample, not an early partial estimate.
			if (!hitchance_out)
				break;
		}

		if (current + MAX_SEEDS - sampled < required_hits)
			break;
	}

	rage.restore.restore(rage.player);
	return passed;
}

void collect_damage_from_multipoints(int damage, vec3_t& predicted_eye_pos, rage_player_t* rage, rage_point_t& points, anim_record_t* record, matrix3x4_t* matrix_to_aim, bool predicted)
{
	auto multipoints = RAGEBOT->get_points(rage->player, points.hitbox, matrix_to_aim);
	if (multipoints.empty())
		return;

	auto local_anims = ANIMFIX->get_local_anims();
	auto& start_eye_pos = predicted ? predicted_eye_pos : local_anims->eye_pos;

	for (auto& multipoint : multipoints)
	{
		if (multipoint.second)
			continue;

		auto bullet = penetration::simulate(HACKS->local, rage->player, start_eye_pos, multipoint.first);
		if (bullet.damage < damage
			|| bullet.traced_target == nullptr
			|| bullet.traced_target != rage->player
			|| HACKS->weapon->is_taser() && bullet.penetration_count < 4)
			continue;

		rage_point_t point{};
		point.center = false;
		point.hitbox = points.hitbox;
		point.damage = bullet.damage;
		point.aim_point = multipoint.first;
		point.predicted_eye_pos = points.predicted_eye_pos;

#ifndef LEGACY
		point.safety = [&]()
		{
			auto safety = 0;

			matrix3x4_t* matrices[]
			{
				record->matrix_left.matrix,
				record->matrix_left.roll_matrix,
				record->matrix_right.matrix,
				record->matrix_right.roll_matrix,
				record->matrix_zero.matrix,
			};

			for (int i = 0; i < 5; ++i)
			{
				if (can_hit_hitbox(start_eye_pos, multipoint.first, rage, points.hitbox, matrices[i], record))
					++safety;
			}

			return safety;
		}();
#endif
		append_unique_multipoint(rage, point);
	}
}

void c_ragebot::do_hitscan(rage_player_t* rage)
{
	if (rage->points_to_scan.empty())
		return;

	if (!rage->start_scans)
		return;

	if (!rage->hitscan_record)
		return;

	rage->restore.store(rage->player);
	{
		auto matrix_to_aim = rage->hitscan_record->get_aim_matrix();
		LAGCOMP->set_record(rage->player, rage->hitscan_record, matrix_to_aim);
	
		int threads_count = 0;

		// Workers append to points_to_scan. Keep their input in a stable copy so
		// a vector reallocation cannot invalidate task arguments or this loop.
		auto center_points = rage->points_to_scan;
		for (auto& points : center_points)
		{
			++threads_count;

			auto dmg = get_min_damage(rage->player);
			THREAD_POOL->add_task(collect_damage_from_multipoints,
				dmg,
				std::ref(predicted_eye_pos),
				rage,
				std::ref(points),
				rage->hitscan_record,
				matrix_to_aim,
				points.predicted_eye_pos);
		}

		if (threads_count > 0)
			THREAD_POOL->wait_all();
	}
	rage->restore.restore(rage->player);
}

void c_ragebot::scan_players()
{
	int threads_count = 0;

	LISTENER_ENTITY->for_each_player([&](c_cs_player* player)
	{
		if (!player->is_alive() || player->dormant() || player->has_gun_game_immunity())
			return;

		auto rage = &rage_players[player->index()];
		if (!rage || !rage->player || rage->player != player)
			return;

		++threads_count;

		auto dmg = get_min_damage(rage->player);
		THREAD_POOL->add_task(pre_cache_centers, dmg, std::ref(hitboxes), std::ref(predicted_eye_pos), rage);
	});

	if (threads_count < 1)
		return;

	THREAD_POOL->wait_all();

	LISTENER_ENTITY->for_each_player([&](c_cs_player* player)
	{
		if (!player->is_alive() || player->dormant() || player->has_gun_game_immunity())
			return;

		auto rage = &rage_players[player->index()];
		if (!rage || !rage->player || rage->player != player)
			return;

		do_hitscan(rage);
	});
}

void c_ragebot::choose_best_point()
{
	auto prefer_baim_on_dt = EXPLOITS->enabled() && EXPLOITS->get_exploit_mode() == EXPLOITS_DT
		&& (HACKS->weapon->is_auto_sniper() || HACKS->weapon->is_heavy_pistols());

	LISTENER_ENTITY->for_each_player([&](c_cs_player* player)
	{
		if (!player->is_alive() || player->dormant() || player->has_gun_game_immunity())
			return;

		auto rage = &rage_players[player->index()];
		if (!rage || !rage->player || rage->player != player)
			return;

		auto damage = get_min_damage(rage->player);
		auto get_best_aim_point = [&]() -> rage_point_t
		{
			rage_point_t best{};
			std::sort(rage->points_to_scan.begin(), rage->points_to_scan.end(), [](const rage_point_t& a, const rage_point_t& b) {
				return a.damage > b.damage;
			});

			for (auto& point : rage->points_to_scan)
			{
				auto is_body = point.hitbox == HITBOX_PELVIS || point.hitbox == HITBOX_STOMACH;

				if (point.damage < damage)
					continue;
				
				if (g_cfg.binds[force_body_b].toggled && !is_body)
					continue;

				if (g_cfg.binds[force_sp_b].toggled && point.safety < 5)
					continue;

				if (point.safety == 5 && rage_config.prefer_safe)
				{
					point.found = true;
					return point;
				}
				else if (is_body && (point.damage >= player->health() || prefer_baim_on_dt || rage_config.prefer_body))
				{
					point.found = true;
					return point;
				}
				else
				{
					if (point.damage > best.damage)
					{
						best = point;
						best.found = true;
					}
				}
			}

			return best;
		};

		auto best_point = get_best_aim_point();
		if (best_point.found)
		{
			rage->best_point = best_point;
			rage->best_record = rage->hitscan_record;
			rage->best_point.found = true;
		}
	});
}

void c_ragebot::auto_revolver()
{
	if (!HACKS->local || !HACKS->weapon || !HACKS->weapon_info)
		return;

	auto next_secondary_attack = HACKS->weapon->next_secondary_attack();

	if (!g_cfg.rage.enable || EXPLOITS->recharge.start && !EXPLOITS->recharge.finish || HACKS->weapon->item_definition_index() != WEAPON_REVOLVER || HACKS->weapon->clip1() <= 0)
	{
		last_checked = 0;
		tick_cocked = 0;
		tick_strip = 0;
		next_secondary_attack = 0.f;

		revolver_fire = false;
		return;
	}

	auto time = TICKS_TO_TIME(HACKS->tickbase - EXPLOITS->tickbase_offset());
	const auto max_ticks = TIME_TO_TICKS(.25f) - 1;
	const auto tick_base = TIME_TO_TICKS(time);

	if (HACKS->local->next_attack() > time)
		return;

	if (HACKS->local->spawn_time() != last_spawn_time)
	{
		tick_cocked = tick_base;
		tick_strip = tick_base - max_ticks - 1;
		last_spawn_time = HACKS->local->spawn_time();
	}

	if (HACKS->weapon->next_primary_attack() > time)
	{
		HACKS->cmd->buttons.remove(IN_ATTACK);
		revolver_fire = false;
		return;
	}

	if (last_checked == tick_base)
		return;

	last_checked = tick_base;
	revolver_fire = false;

	if (tick_base - tick_strip > 2 && tick_base - tick_strip < 14)
		revolver_fire = true;

	if (HACKS->cmd->buttons.has(IN_ATTACK) && revolver_fire)
		return;

	HACKS->cmd->buttons.force(IN_ATTACK);

	if (next_secondary_attack >= time)
		HACKS->cmd->buttons.force(IN_ATTACK2);

	if (tick_base - tick_cocked > max_ticks * 2 + 1)
	{
		tick_cocked = tick_base;
		tick_strip = tick_base - max_ticks - 1;
	}

	const auto cock_limit = tick_base - tick_cocked >= max_ticks;
	const auto after_strip = tick_base - tick_strip <= max_ticks;

	if (cock_limit || after_strip)
	{
		tick_cocked = tick_base;
		HACKS->cmd->buttons.remove(IN_ATTACK);

		if (cock_limit)
			tick_strip = tick_base;
	}
}

bool c_ragebot::knife_is_behind(c_cs_player* player, anim_record_t* record)
{
	auto origin = record ? record->origin : player->get_abs_origin();
	auto abs_angles = record ? record->abs_angles : player->get_abs_angles();

	auto anim = ANIMFIX->get_local_anims();

	vec3_t delta{ origin - anim->eye_pos };
	delta.z = 0.f;
	delta = delta.normalized();

	vec3_t target;
	math::angle_vectors(abs_angles, target);
	target.z = 0.f;

	return delta.dot(target) > 0.475f;
}

bool c_ragebot::knife_trace(vec3_t dir, bool stab, c_game_trace* trace)
{
	float range = stab ? 32.f : 48.f;

	auto anim = ANIMFIX->get_local_anims();

	vec3_t start = anim->eye_pos;
	vec3_t end = start + (dir * range);

	c_trace_filter filter{};
	filter.skip = HACKS->local;
	HACKS->engine_trace->trace_ray(ray_t(start, end), MASK_SOLID, &filter, trace);

	if (trace->fraction >= 1.f)
	{
		HACKS->engine_trace->trace_ray(ray_t(start, end, { -16.f, -16.f, -18.f }, { 16.f, 16.f, 18.f }), MASK_SOLID, &filter, trace);
		return trace->fraction < 1.f;
	}

	return true;
}

bool c_ragebot::can_knife(c_cs_player* player, anim_record_t* record, vec3_t angle, bool& stab)
{
	vec3_t forward{};
	math::angle_vectors(angle, forward);

	c_game_trace trace{};
	knife_trace(forward, false, &trace);

	if (!trace.entity || trace.entity != player)
		return false;

	bool armor = player->armor_value() > 0;
	bool first = HACKS->weapon->next_primary_attack() + 0.4f < HACKS->predicted_time;
	bool back = knife_is_behind(player, record);

	int stab_dmg = knife_dmg.stab[armor][back];
	int slash_dmg = knife_dmg.swing[first][armor][back];
	int swing_dmg = knife_dmg.swing[false][armor][back];

	int health = player->health();
	if (health <= slash_dmg)
		stab = false;
	else if (health <= stab_dmg)
		stab = true;
	else if (health > (slash_dmg + swing_dmg + stab_dmg))
		stab = true;
	else
		stab = false;

	if (stab && !knife_trace(forward, true, &trace))
		return false;

	return true;
}

void c_ragebot::knife_bot()
{
	if (!g_cfg.rage.enable)
		return;

	if (HACKS->predicted_time < HACKS->weapon->next_primary_attack() || HACKS->predicted_time < HACKS->weapon->next_secondary_attack())
		return;

	bool supress_doubletap_choke = true;
	if (EXPLOITS->enabled() && EXPLOITS->get_exploit_mode() == EXPLOITS_DT)
		supress_doubletap_choke = EXPLOITS->defensive.tickbase_choke > 2;

	bool best_stab{};
	knife_point_t best{};

	LISTENER_ENTITY->for_each_player([&](c_cs_player* player)
	{
		if (!player->is_alive() || player->dormant() || player->has_gun_game_immunity())
			return;

		auto anims = ANIMFIX->get_anims(player->index());
		if (!anims || anims->records.empty())
			return;

		auto first_find = std::find_if(anims->records.begin(), anims->records.end(), [&](anim_record_t& record) 
			{
			return !record.tickbase_shift && LAGCOMP->is_tick_valid(record.break_lc, record.sim_time);
			});

		anim_record_t* first = nullptr;
		if (first_find != anims->records.end())
			first = &*first_find;

		restore_record_t backup{};
		backup.store(player);

		if (!first)
		{
			backup.restore(player);
			return;
		}
	
		LAGCOMP->set_record(player, first, first->matrix_orig.matrix);

		for (auto& a : knife_ang)
		{
			if (!can_knife(player, first, a, best_stab))
				continue;

			best.point = a;
			best.record = first;
			break;
		}
			
		auto last_find = std::find_if(anims->records.rbegin(), anims->records.rend(), [&](anim_record_t& record) 
		{
			return !record.tickbase_shift && LAGCOMP->is_tick_valid(record.break_lc, record.sim_time);
		});

		anim_record_t* last = nullptr;
		if (last_find != anims->records.rend())
			last = &*last_find;

		if (!last || last == first)
		{
			backup.restore(player);
			return;
		}

		LAGCOMP->set_record(player, last, last->matrix_orig.matrix);

		for (auto& a : knife_ang)
		{
			if (!can_knife(player, last, a, best_stab))
				continue;

			best.point = a;
			best.record = last;
			break;
		}		
		
		backup.restore(player);

		if (best.record)
		{
			backup.restore(player);
			return;
		}
	});

	if (supress_doubletap_choke && best.record)
	{
		HACKS->cmd->viewangles = best.point.normalized_angle();

		if (best.record && !HACKS->cl_lagcomp0)
			HACKS->cmd->tickcount = TIME_TO_TICKS(best.record->sim_time + HACKS->lerp_time);

		HACKS->cmd->buttons.force(best_stab ? IN_ATTACK2 : IN_ATTACK);
	}
}

void c_ragebot::run()
{
	if (!HACKS->weapon || !HACKS->weapon_info || HACKS->client_state->delta_tick == -1)
		return;

	auto_pistol();

	if (EXPLOITS->cl_move.trigger && EXPLOITS->cl_move.shifting)
		return;

	hitboxes.clear();
	hitboxes.reserve(HITBOX_MAX);

	rage_config = main_utils::get_weapon_config();
	update_hitboxes();

	trigger_stop = false;
	should_shot = true;
	reset_rage_hitscan = false;
	firing = false;
	working = false;
	rage_player_iter = 0;
	predicted_eye_pos.reset();
	best_rage_player.reset();

	if (!g_cfg.rage.enable || HACKS->weapon->is_misc_weapon() && !HACKS->weapon->is_taser() && !HACKS->weapon->is_knife())
	{
		reset_rage_players();
		return;
	}

	if (HACKS->weapon->is_knife())
	{
		knife_bot();
		return;
	}

	update_predicted_eye_pos();
	prepare_players_for_scan();

	if (rage_player_iter < 1)
	{
		reset_rage_players();
		return;
	}

	reset_rage_hitscan = true;

	scan_players();
	choose_best_point();

	float lowest_distance = FLT_MAX;

	firing = false;
	working = false;
	best_rage_player.reset();

	LISTENER_ENTITY->for_each_player([&](c_cs_player* player)
	{
		if (!player->is_alive() || player->dormant() || player->has_gun_game_immunity())
			return;

		auto rage = &rage_players[player->index()];
		if (!rage || !rage->player || rage->player != player)
			return;

		if (!rage->start_scans)
			return;

		if (!rage->best_point.found)
			return;

		if (lowest_distance > rage->distance)
		{
			lowest_distance = rage->distance;
			best_rage_player = *rage;
		}
	});

	const auto& best_point = best_rage_player.best_point;
	if (best_rage_player.player && best_point.found && best_rage_player.start_scans)
	{
		working = true;

		auto local_anims = ANIMFIX->get_local_anims();

		// also, with predictive scans we automatically achieved early auto stop without useless conditions & heavy code (@opai)
		// and auto scope too...

		auto damage = get_min_damage(best_rage_player.player);
		bool already_stooped = false;
		if (best_point.predicted_eye_pos && best_point.damage >= damage && (rage_config.quick_stop_options & early))
		{
			force_scope();

			if (rage_config.quick_stop && should_stop(best_point))
			{
				already_stooped = true;
				trigger_stop = true;
			}
		}

		auto aim_angle = math::calc_angle(local_anims->eye_pos, best_point.aim_point).normalized_angle();
		auto ideal_start = ANIMFIX->get_eye_position(aim_angle.x);

		auto best_record = best_rage_player.best_record;

		// when we have pre-scanned points we won't shoot at it's position from pred eye pos
		// because it's wrong
		// wait untill you will actually see the point and can shoot to it 
		{
			best_rage_player.restore.store(best_rage_player.player);

			auto matrix_to_aim = best_record->get_aim_matrix();
			LAGCOMP->set_record(best_rage_player.player, best_record, matrix_to_aim);

			auto final_bullet = penetration::simulate(HACKS->local, best_rage_player.player, ideal_start, best_point.aim_point);
			best_rage_player.restore.restore(best_rage_player.player);

			if (final_bullet.damage < damage || HACKS->weapon->is_taser() && final_bullet.penetration_count < 4)
				return;
		}

		if (!should_shot)
			return;

		if (!already_stooped || !(rage_config.quick_stop_options & early))
		{
			force_scope();

			if (rage_config.quick_stop && should_stop(best_point))
				trigger_stop = true;
		}

		bool supress_doubletap_choke = true;
		if (EXPLOITS->enabled() && EXPLOITS->get_exploit_mode() == EXPLOITS_DT)
			supress_doubletap_choke = EXPLOITS->defensive.tickbase_choke > 2;

		if (!supress_doubletap_choke)
			return;

		if (!can_fire())
			return;

		float out_chance = 0.f;
		auto max_hitchance = rage_config.hitchance * 0.01f;
		auto hitchance_source = hitchance_source_seeds;

		if (!hitchance(ideal_start, best_rage_player, best_point, best_record, max_hitchance, nullptr, &out_chance, &hitchance_source))
			return;

		if (g_cfg.rage.auto_fire)
			HACKS->cmd->buttons.force(IN_ATTACK);

		if (HACKS->cmd->buttons.has(IN_ATTACK))
		{
			firing = true;

			auto record_time = best_record->sim_time;

			HACKS->cmd->tickcount = TIME_TO_TICKS(record_time + HACKS->lerp_time);

			if (g_cfg.visuals.chams[c_onshot].enable)
				CHAMS->add_shot_record(best_rage_player.player, best_record->matrix_orig.matrix);

			HACKS->cmd->viewangles = math::calc_angle(ideal_start, best_point.aim_point).normalized_angle();
			HACKS->cmd->viewangles -= HACKS->local->aim_punch_angle() * (HACKS->convars.weapon_recoil_scale->get_float());
			HACKS->cmd->viewangles = HACKS->cmd->viewangles.normalized_angle();

			add_shot_record(best_rage_player.player, best_point, best_record, ideal_start,
				out_chance, max_hitchance, damage, best_rage_player.backtrack);
			shots.back().hitchance_source = hitchance_source;
			log_shot_fire(shots.back());

			if ((g_cfg.binds[hs_b].toggled || !ANTI_AIM->is_fake_ducking()) && !*HACKS->send_packet)
				*HACKS->send_packet = true;
		}
	}

	best_rage_player.reset();
}

void c_ragebot::add_shot_record(c_cs_player* player, const rage_point_t& best, anim_record_t* record, vec3_t eye_pos,
	float hitchance, float required_hitchance, int min_damage, const backtrack_debug_t& backtrack)
{
	auto& new_shot = shots.emplace_back();
	new_shot.time = HACKS->predicted_time;
	new_shot.init_time = 0.f;
	new_shot.impact_fire = false;
	new_shot.fire = false;
	new_shot.damage = -1;
	new_shot.safety = best.safety;
	new_shot.start = eye_pos;
	new_shot.hitgroup = -1;
	new_shot.hitchance = hitchance;
	new_shot.required_hitchance = required_hitchance;
	new_shot.expected_damage = best.damage;
	new_shot.min_damage = min_damage;
	new_shot.target_health = player->health();
	new_shot.predicted_eye = best.predicted_eye_pos;
	new_shot.center_point = best.center;
	new_shot.hitbox = best.hitbox;
	new_shot.pointer = player;
	new_shot.target_name = player->get_name();
	new_shot.record = *record;
	new_shot.index = player->index();
	new_shot.resolver = resolver_info[new_shot.index];
	new_shot.resolver_misses = missed_shots[new_shot.index];
	new_shot.point = best.aim_point;
	new_shot.backtrack = backtrack;
	new_shot.target_sim_time = player->sim_time();
	new_shot.backtrack_ticks = std::abs(TIME_TO_TICKS(new_shot.target_sim_time - record->sim_time));
	new_shot.command_tick = HACKS->cmd->tickcount;
	new_shot.command_number = HACKS->cmd->command_number;
	new_shot.desync_config = std::clamp(g_cfg.rage.desync_degree, -58, 58);
	new_shot.latency = HACKS->ping;
	new_shot.lerp = HACKS->lerp_time;

	const auto predicted = ENGINE_PREDICTION->get_networked_vars(new_shot.command_number);
	new_shot.inaccuracy = predicted->inaccuracy;
	new_shot.spread = predicted->spread;
}

void c_ragebot::weapon_fire(c_game_event* event)
{
	if (shots.empty())
		return;

	if (HACKS->engine->get_player_for_user_id(event->get_int(CXOR("userid"))) != HACKS->engine->get_local_player())
		return;

	auto& shot = shots.front();
	if (!shot.fire)
		shot.fire = true;
}

void c_ragebot::bullet_impact(c_game_event* event)
{
	if (shots.empty())
		return;

	auto& shot = shots.front();

	if (HACKS->engine->get_player_for_user_id(event->get_int(CXOR("userid"))) != HACKS->engine->get_local_player())
		return;

	const auto vec_impact = vec3_t{ event->get_float(CXOR("x")), event->get_float(CXOR("y")), event->get_float(CXOR("z")) };

	bool check = false;
	if (shot.impact_fire)
	{
		if (shot.start.dist_to(vec_impact) > shot.start.dist_to(shot.impact))
			check = true;
	}
	else
		check = true;

	if (!check)
		return;

	shot.impact_fire = true;
	shot.init_time = HACKS->predicted_time;
	shot.impact = vec_impact;
}

void c_ragebot::player_hurt(c_game_event* event)
{
	if (HACKS->engine->get_player_for_user_id(event->get_int(CXOR("attacker"))) != HACKS->engine->get_local_player())
		return;

	if (shots.empty())
		return;

	const auto victim = HACKS->engine->get_player_for_user_id(event->get_int(CXOR("userid")));
	const auto shot = std::find_if(shots.begin(), shots.end(), [&](const aim_shot_record_t& current)
	{
		return current.fire && current.index == victim;
	});

	if (shot == shots.end())
		return;

	// A confirmed hit validates the current resolve. Do not keep forcing the
	// bruteforce branch on every later record after a successful shot.
	missed_shots[victim] = 0;
	m_missed_anim_side[victim] = 0;
	m_missed_prev_side[victim] = 0;

	shot->damage = event->get_int(CXOR("dmg_health"));
	shot->hitgroup = event->get_int(CXOR("hitgroup"));
	shot->remaining_health = event->get_int(CXOR("health"));
	const auto group_match = shot_hitgroup_matches(*shot);
	log_shot_outcome(*shot, "HIT", group_match ? "confirmed" : "confirmed wrong_hitgroup");
	shots.erase(shot);
}

void c_ragebot::round_start(c_game_event* event)
{
	for (auto& i : missed_shots)
		i = 0;

	for (auto& i : m_missed_anim_side)
		i = 0;

	for (auto& i : m_missed_prev_side)
		i = 0;

	shots.clear();
}

void c_ragebot::on_game_events(c_game_event* event)
{
	auto name = CONST_HASH(event->get_name());

	switch (name)
	{
	case HASH("weapon_fire"):
		weapon_fire(event);
		break;
	case HASH("bullet_impact"):
		bullet_impact(event);
		break;
	case HASH("player_hurt"):
		player_hurt(event);
		break;
	case HASH("round_start"):
		round_start(event);
		break;
	}
}

void c_ragebot::proceed_misses()
{
	if (shots.empty())
		return;

	auto& shot = shots.front();
	if (std::abs(HACKS->predicted_time - shot.time) > 1.f)
	{
		const auto reason = !shot.fire ? "timeout/no weapon_fire"
			: !shot.impact_fire ? "timeout/no bullet_impact" : "timeout/no result";
		log_shot_outcome(shot, "TIMEOUT", reason);
		shots.erase(shots.begin());
		return;
	}

	if (shot.init_time != -1.f && shot.index && shot.damage == -1 && shot.fire && shot.impact_fire)
	{
		std::string miss_reason = "invalid target";
		auto new_player = (c_cs_player*)HACKS->entity_list->get_client_entity(shot.index);
		if (new_player && new_player->is_player() && shot.pointer == new_player)
		{
			const auto studio_model = HACKS->model_info->get_studio_model(new_player->get_model());
			miss_reason = studio_model ? "unclassified" : "no studio model";

			if (studio_model)
			{
				auto& resolver_info = shot.resolver;
				const auto end = shot.impact;

				auto matrix_to_aim = shot.record.get_aim_matrix();

				rage_player_t rage_player{};
				rage_player.player = new_player;
				rage_player.restore.store(new_player);

				LAGCOMP->set_record(new_player, &shot.record, matrix_to_aim);

				if (!can_hit_hitbox(shot.start, end, &rage_player, shot.hitbox, matrix_to_aim, &shot.record))
				{
					float dist = shot.start.dist_to(shot.impact);
					float dist2 = shot.start.dist_to(shot.point);

					if (dist2 > dist)
					{
						miss_reason = "occlusion";
						EVENT_LOGS->push_message(XOR("Missed shot due to occlusion"));
					}
					else
					{
						miss_reason = "spread";
						EVENT_LOGS->push_message(XOR("Missed shot due to spread"));
					}
				}
				else
				{
					if (new_player->is_alive())
					{
						if (resolver_info.resolved)
						{
							auto every_side_intersects = false;
#ifndef LEGACY
							every_side_intersects = true;
							matrix3x4_t* alternative_matrices[]
							{
								shot.record.matrix_left.matrix,
								shot.record.matrix_left.roll_matrix,
								shot.record.matrix_right.matrix,
								shot.record.matrix_right.roll_matrix,
								shot.record.matrix_zero.matrix
							};

							for (auto alternative : alternative_matrices)
							{
								if (!can_hit_hitbox(shot.start, end, &rage_player, shot.hitbox, alternative, &shot.record))
								{
									every_side_intersects = false;
									break;
								}
							}
#endif
							if (every_side_intersects)
							{
								miss_reason = "prediction";
								EVENT_LOGS->push_message(XOR("Missed shot due to prediction"));
							}
							else
							{
								missed_shots[shot.index]++;
								m_missed_prev_side[shot.index] = m_missed_anim_side[shot.index];
								m_missed_anim_side[shot.index] = resolver_info.side;
								miss_reason = "resolver";
								EVENT_LOGS->push_message(XOR("Missed shot due to resolver"));
							}
						}
						else
						{
							miss_reason = std::string("unresolved/") + shot_resolver_status_name(shot.resolver.debug_status);
							EVENT_LOGS->push_message(std::string("Missed shot due to ") + miss_reason);
						}
					}
					else
					{
						miss_reason = "death";
						EVENT_LOGS->push_message(XOR("Missed shot due to death"));
					}
				}

				rage_player.restore.restore(new_player);
			}
		}

		log_shot_outcome(shot, "MISS", miss_reason);
		shots.erase(shots.begin());
	}
}