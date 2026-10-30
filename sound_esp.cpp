#include "globals.hpp"
#include "sound_esp.hpp"
#include "render.hpp"

void c_sound_esp::on_sound(c_cs_player* player, const vec3_t& origin)
{
	if (!g_cfg.visuals.sound_esp.enable || !HACKS->local || !HACKS->in_game || !player || !player->is_player())
		return;
	if (player == HACKS->local || player->is_teammate() || !player->is_alive())
		return;
	const int index = player->index();
	if (index <= 0 || index >= 65)
		return;

	auto& marker = markers[index];
	const float now = HACKS->system_time();
	// Footstep events can arrive in pairs. Keep the newest position without
	// restarting the same marker several times within a single render frame.
	if (marker.valid && now - marker.time < 0.04f && marker.origin.dist_to(origin) < 8.f)
		return;
	marker.origin = origin;
	marker.time = now;
	marker.valid = true;
}

void c_sound_esp::render()
{
	if (!g_cfg.visuals.sound_esp.enable || !HACKS->local || !HACKS->in_game)
		return;

	const float duration = std::max(0.1f, g_cfg.visuals.sound_esp.duration / 1000.f);
	const float now = HACKS->system_time();
	const auto base = g_cfg.visuals.sound_esp.color.base();
	for (auto& marker : markers)
	{
		if (!marker.valid)
			continue;
		const float age = now - marker.time;
		if (age < 0.f || age >= duration)
		{
			marker.reset();
			continue;
		}
		vec2_t screen{};
		if (!RENDER->world_to_screen(marker.origin, screen))
			continue;
		const float progress = std::clamp(age / duration, 0.f, 1.f);
		const float fade = 1.f - progress;
		const float radius = std::lerp(5.f, (float)g_cfg.visuals.sound_esp.radius, progress);
		const auto color = base.new_alpha((int)(base.a() * fade));
		RENDER->circle(screen.x, screen.y, radius, color, 48, 1.5f);
		RENDER->circle_filled(screen.x, screen.y, 2.f, color, 20);
	}
}
