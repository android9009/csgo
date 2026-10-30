#include "globals.hpp"
#include "features.hpp"

namespace
{
	constexpr std::array<int, 5> legit_hitboxes = {
		HITBOX_HEAD,
		HITBOX_CHEST,
		HITBOX_UPPER_CHEST,
		HITBOX_STOMACH,
		HITBOX_PELVIS
	};

	INLINE unsigned int sanitize_hitboxes(unsigned int hitboxes)
	{
		return hitboxes ? hitboxes : static_cast<unsigned int>(head | chest | stomach | pelvis);
	}
}

int c_legitbot::get_min_damage(c_cs_player* player) const
{
	if (!player)
		return 1;

	const int configured = std::clamp(g_cfg.legit.min_damage, 1, 120);
	if (configured >= 100)
		return player->health() + configured - 100;

	return std::min(configured, player->health());
}

bool c_legitbot::is_hitbox_enabled(int hitbox, unsigned int mask) const
{
	mask = sanitize_hitboxes(mask);

	switch (hitbox)
	{
	case HITBOX_HEAD:
	case HITBOX_NECK:
		return mask & head;
	case HITBOX_CHEST:
	case HITBOX_UPPER_CHEST:
	case HITBOX_THORAX:
		return mask & chest;
	case HITBOX_STOMACH:
		return mask & stomach;
	case HITBOX_PELVIS:
		return mask & pelvis;
	default:
		return false;
	}
}

bool c_legitbot::passes_smoke_check(const vec3_t& start, const vec3_t& end) const
{
	if (!g_cfg.legit.smoke_check || !offsets::line_goes_through_smoke.pointer)
		return true;

	using line_goes_through_smoke_fn = bool(__cdecl*)(vec3_t, vec3_t);
	return !offsets::line_goes_through_smoke.cast<line_goes_through_smoke_fn>()(start, end);
}

bool c_legitbot::validate_point(const legit_point_t& point)
{
	if (!point.valid() || !point.player->is_alive() || point.player->dormant())
		return false;

	restore_record_t restore;
	if (point.record)
	{
		restore.store(point.player);
		LAGCOMP->set_record(point.player, point.record, point.record->matrix_orig.matrix);
	}

	const auto bullet = penetration::simulate(HACKS->local, point.player, HACKS->local->get_eye_position(), point.position);

	if (point.record)
		restore.restore(point.player);

	if (bullet.traced_target != point.player)
		return false;

	if (!g_cfg.legit.autowall && bullet.penetration_count < 4)
		return false;

	return bullet.damage >= get_min_damage(point.player);
}

void c_legitbot::scan_player(c_cs_player* player, const vec3_t& eye_pos, std::vector<legit_point_t>& points)
{
	if (!player || !player->is_alive() || player->dormant() || player->has_gun_game_immunity())
		return;

	const float max_fov = static_cast<float>(std::clamp(weapon_config.fov > 0 ? weapon_config.fov : 5, 1, 60));
	const unsigned int hitbox_mask = sanitize_hitboxes(weapon_config.hitboxes);
	const int min_damage = get_min_damage(player);

	auto scan_matrix = [&](anim_record_t* record, matrix3x4_t* matrix)
	{
		if (!matrix)
			return;

		for (const int hitbox : legit_hitboxes)
		{
			if (!is_hitbox_enabled(hitbox, hitbox_mask))
				continue;

			const auto position = player->get_hitbox_position(hitbox, matrix);
			if (position.length_sqr() < 1.f)
				continue;

			auto delta = (math::calc_angle(eye_pos, position) - HACKS->cmd->viewangles).normalized_angle();
			const float fov = std::sqrt(delta.x * delta.x + delta.y * delta.y);
			if (fov > max_fov)
				continue;

			if (!passes_smoke_check(eye_pos, position))
				continue;

			if (g_cfg.legit.autowall)
			{
				const auto bullet = penetration::simulate(HACKS->local, player, eye_pos, position);
				if (bullet.traced_target != player || bullet.damage < min_damage)
					continue;
			}
			else
			{
				c_game_trace trace;
				trace.clear();
				c_trace_filter_simple filter(HACKS->local);
				HACKS->engine_trace->trace_ray(ray_t(eye_pos, position), MASK_SHOT_PLAYER, (i_trace_filter*)&filter, &trace);

				if (trace.entity != player)
					continue;
			}

			const float distance = eye_pos.dist_to(position);
			float score = -fov;

			switch (g_cfg.legit.target_selection)
			{
			case 1: // Distance
				score = -distance * 0.01f;
				break;
			case 2: // Health
				score = -static_cast<float>(player->health());
				break;
			case 3: // Hybrid
				score = -fov * 2.f - distance * 0.002f - static_cast<float>(player->health()) * 0.005f;
				if (hitbox == HITBOX_HEAD)
					score += 0.35f;
				break;
			default: // Crosshair
				break;
			}

			legit_point_t point;
			point.player = player;
			point.record = record;
			point.hitbox = hitbox;
			point.fov = fov;
			point.distance = distance;
			point.score = score;
			point.position = position;
			points.emplace_back(point);
		}
	};

	auto& cache = player->bone_cache();
	if (cache.base() && cache.count() > 0)
		scan_matrix(nullptr, cache.base());

	if (!weapon_config.backtrack || HACKS->cl_lagcomp0)
		return;

	auto anims = ANIMFIX->get_anims(player->index());
	if (!anims || anims->ptr != player || anims->records.empty())
		return;

	restore_record_t restore;
	restore.store(player);

	int scanned_records = 0;
	for (auto& record : anims->records)
	{
		if (scanned_records >= 12)
			break;

		if (!record.filled || record.dormant || record.tickbase_shift || record.sim_time <= 0.f)
			continue;

		if (record.sim_time == player->sim_time())
			continue;

		if (!LAGCOMP->is_tick_valid(record.break_lc, record.sim_time))
			continue;

		LAGCOMP->set_record(player, &record, record.matrix_orig.matrix);
		scan_matrix(&record, record.matrix_orig.matrix);
		++scanned_records;
	}

	restore.restore(player);
}

legit_point_t c_legitbot::find_target(const vec3_t& eye_pos)
{
	std::vector<legit_point_t> points;
	points.reserve(64);

	LISTENER_ENTITY->for_each_player([&](c_cs_player* player)
	{
		scan_player(player, eye_pos, points);
	});

	if (points.empty())
		return {};

	std::sort(points.begin(), points.end(), [](const legit_point_t& a, const legit_point_t& b)
	{
		if (a.score == b.score)
			return a.fov < b.fov;
		return a.score > b.score;
	});

	if (g_cfg.legit.target_lock && target_index >= 0)
	{
		for (const auto& point : points)
		{
			if (point.player->index() == target_index && validate_point(point))
				return point;
		}
	}

	for (const auto& point : points)
	{
		if (validate_point(point))
			return point;
	}

	return {};
}

void c_legitbot::run_aim(const vec3_t& eye_pos)
{
	if (!weapon_config.enable || !g_cfg.binds[toggle_legit_b].toggled)
	{
		target_index = -1;
		return;
	}

	if (g_cfg.legit.flash_check && HACKS->local->flash_duration() > 0.f)
	{
		target_index = -1;
		return;
	}

	const auto best = find_target(eye_pos);
	if (!best.valid())
	{
		target_index = -1;
		return;
	}

	const int index = best.player->index();
	const float time = now();
	if (target_index != index)
	{
		target_index = index;
		target_since = time;
	}

	target = best.player;
	target_record = best.record;
	target_point = best.position;
	target_hitbox = best.hitbox;

	const float delay = static_cast<float>(std::clamp(weapon_config.aim_delay, 0, 1000)) * 0.001f;
	if (time - target_since < delay)
		return;

	auto desired = math::calc_angle(eye_pos, best.position).normalized_angle();
	const float recoil_scale = HACKS->convars.weapon_recoil_scale->get_float();
	const float recoil_amount = g_cfg.legit.rcs.enable
		? static_cast<float>(std::clamp(g_cfg.legit.rcs.amount, 0, 100)) * 0.01f
		: 1.f;

	desired -= HACKS->local->aim_punch_angle() * (recoil_scale * recoil_amount);

	auto delta = (desired - HACKS->cmd->viewangles).normalized_angle();
	const int smooth = std::clamp(weapon_config.smooth > 0 ? weapon_config.smooth : 8, 1, 100);
	float step = 1.f;
	if (smooth > 1)
	{
		const float response = 2.f + static_cast<float>(100 - smooth) * 0.35f;
		step = 1.f - std::exp(-response * HACKS->global_vars->interval_per_tick);
		step = std::clamp(step, 0.001f, 1.f);
	}

	HACKS->cmd->viewangles += delta * step;
	HACKS->cmd->viewangles.x = std::clamp(HACKS->cmd->viewangles.x, -89.f, 89.f);
	HACKS->cmd->viewangles.y = math::normalize_yaw(HACKS->cmd->viewangles.y);
	HACKS->cmd->viewangles.z = 0.f;

	if (best.record && HACKS->cmd->buttons.has(IN_ATTACK))
		HACKS->cmd->tickcount = TIME_TO_TICKS(best.record->sim_time + HACKS->lerp_time);

	if (weapon_config.quick_stop && HACKS->local->flags().has(FL_ONGROUND) && HACKS->local->velocity().length_2d() > 20.f)
		MOVEMENT->instant_stop();

	aiming = true;
	view_changed = true;
}

void c_legitbot::run_trigger(const vec3_t& eye_pos)
{
	if (!g_cfg.legit.trigger.enable || !g_cfg.binds[toggle_trigger_b].toggled || HACKS->weapon->clip1() < 1)
	{
		trigger_index = -1;
		return;
	}

	if (g_cfg.legit.flash_check && HACKS->local->flash_duration() > 0.f)
	{
		trigger_index = -1;
		return;
	}

	auto fire_angles = HACKS->cmd->viewangles + HACKS->local->aim_punch_angle() * HACKS->convars.weapon_recoil_scale->get_float();
	vec3_t forward;
	math::angle_vectors(fire_angles.normalized_angle(), forward);
	const auto end = eye_pos + forward * HACKS->weapon_info->range;

	if (!passes_smoke_check(eye_pos, end))
	{
		trigger_index = -1;
		return;
	}

	c_cs_player* player = nullptr;
	int hitbox = -1;
	int hitgroup = HITGROUP_GENERIC;
	int damage = 0;

	if (g_cfg.legit.autowall)
	{
		const auto bullet = penetration::simulate(HACKS->local, nullptr, eye_pos, end, false, true);
		player = bullet.traced_target;
		hitbox = bullet.hitbox;
		hitgroup = bullet.hitgroup;
		damage = bullet.damage;
	}
	else
	{
		c_game_trace trace;
		trace.clear();
		c_trace_filter_simple filter(HACKS->local);
		HACKS->engine_trace->trace_ray(ray_t(eye_pos, end), MASK_SHOT_PLAYER, (i_trace_filter*)&filter, &trace);
		if (trace.entity && trace.entity->is_player())
			player = (c_cs_player*)trace.entity;
		hitbox = trace.hitbox;
		hitgroup = trace.hitgroup;
		damage = player ? get_min_damage(player) : 0;
	}

	if (!player || !player->is_alive() || player->dormant() || player->is_teammate() || player->has_gun_game_immunity())
	{
		trigger_index = -1;
		return;
	}

	bool allowed_hitbox = is_hitbox_enabled(hitbox, weapon_config.hitboxes);
	if (!allowed_hitbox)
	{
		switch (hitgroup)
		{
		case HITGROUP_HEAD:
		case HITGROUP_NECK:
			allowed_hitbox = is_hitbox_enabled(HITBOX_HEAD, weapon_config.hitboxes);
			break;
		case HITGROUP_CHEST:
			allowed_hitbox = is_hitbox_enabled(HITBOX_CHEST, weapon_config.hitboxes);
			break;
		case HITGROUP_STOMACH:
			allowed_hitbox = is_hitbox_enabled(HITBOX_STOMACH, weapon_config.hitboxes);
			break;
		default:
			break;
		}
	}

	if (!allowed_hitbox || damage < get_min_damage(player))
	{
		trigger_index = -1;
		return;
	}

	const float time = now();
	const int index = player->index();
	if (trigger_index != index)
	{
		trigger_index = index;
		trigger_since = time;
	}

	const float delay = static_cast<float>(std::clamp(g_cfg.legit.trigger.delay, 0, 500)) * 0.001f;
	if (time - trigger_since >= delay && RAGEBOT->can_fire(true))
		HACKS->cmd->buttons.force(IN_ATTACK);
}

void c_legitbot::apply_standalone_rcs()
{
	if (aiming || !g_cfg.legit.rcs.enable || !HACKS->cmd->buttons.has(IN_ATTACK))
		return;

	const float scale = HACKS->convars.weapon_recoil_scale->get_float();
	const float amount = static_cast<float>(std::clamp(g_cfg.legit.rcs.amount, 0, 100)) * 0.01f;
	const auto punch = HACKS->local->aim_punch_angle() * scale;
	const auto delta = (punch - last_punch) * amount;

	HACKS->cmd->viewangles -= delta;
	HACKS->cmd->viewangles.x = std::clamp(HACKS->cmd->viewangles.x, -89.f, 89.f);
	HACKS->cmd->viewangles.y = math::normalize_yaw(HACKS->cmd->viewangles.y);
	HACKS->cmd->viewangles.z = 0.f;
	view_changed = true;
}

void c_legitbot::run()
{
	aiming = false;
	view_changed = false;
	target = nullptr;
	target_record = nullptr;
	target_hitbox = -1;

	if (!HACKS->local || !HACKS->local->is_alive() || !HACKS->weapon || !HACKS->weapon_info || !HACKS->cmd
		|| !g_cfg.legit.enable || g_cfg.rage.enable || HACKS->weapon->is_misc_weapon() || HACKS->weapon->is_grenade()
		|| HACKS->cmd->weapon_select != 0 || HACKS->local->is_defusing())
	{
		reset();
		return;
	}

	weapon_config = main_utils::get_legit_weapon_config();
	const auto eye_pos = HACKS->local->get_eye_position();

	if (!punch_seeded)
	{
		last_punch = HACKS->local->aim_punch_angle() * HACKS->convars.weapon_recoil_scale->get_float();
		punch_seeded = true;
	}

	run_aim(eye_pos);
	apply_standalone_rcs();
	run_trigger(eye_pos);

	last_punch = HACKS->local->aim_punch_angle() * HACKS->convars.weapon_recoil_scale->get_float();

	if (view_changed)
		HACKS->engine->set_view_angles(HACKS->cmd->viewangles);
}

void c_legitbot::render()
{
	if (!HACKS->local || !HACKS->local->is_alive() || !HACKS->weapon || !g_cfg.legit.enable || g_cfg.rage.enable)
		return;

	const auto config = main_utils::get_legit_weapon_config();
	if (!config.enable || !config.fov_cicle || RENDER->screen.x <= 0.f || RENDER->screen.y <= 0.f)
		return;

	int game_fov = HACKS->local->fov();
	if (game_fov <= 0)
		game_fov = HACKS->local->default_fov();
	game_fov = std::clamp(game_fov > 0 ? game_fov : 90, 10, 120);

	const float aim_fov = static_cast<float>(std::clamp(config.fov > 0 ? config.fov : 5, 1, 60));
	const float tangent = std::tan(DEG2RAD(aim_fov)) / std::tan(DEG2RAD(static_cast<float>(game_fov) * 0.5f));
	const float radius = tangent * (RENDER->screen.x * 0.5f);
	if (radius <= 0.f || radius > RENDER->screen.y * 2.f)
		return;

	auto color = config.circle_color.base();
	RENDER->circle(RENDER->screen.x * 0.5f, RENDER->screen.y * 0.5f, radius, color, 96, 1.f);

	if (aiming && target)
	{
		vec2_t screen;
		if (RENDER->world_to_screen(target_point, screen))
			RENDER->circle(screen.x, screen.y, 3.f, color, 16, 1.f);
	}
}

void c_legitbot::reset()
{
	target = nullptr;
	target_record = nullptr;
	target_index = -1;
	target_hitbox = -1;
	trigger_index = -1;
	target_since = 0.f;
	trigger_since = 0.f;
	target_point.reset();
	last_punch.reset();
	aiming = false;
	view_changed = false;
	punch_seeded = false;
}
