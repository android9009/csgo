#include <gui/controls/hsv_slider.h>
#include <gui/gui.h>
#include <algorithm>

#undef min
#undef max

GUI_NAMESPACE;

void hsv_slider::render()
{
	control::render();

	const auto saturation = get_sb_rect();
	const auto alpha = get_alpha_rect();

	auto &d = draw.layers[ctx->content_layer];

	// --- saturation/brightness ---
	auto fake_color = *active_value();
	fake_color = fake_color.hsv(fake_hue, 1.f, 1.f, 1.f);

	if (secondary)
	{
		// Sample the same bilinear blend used for picking. Small tiles avoid the
		// diagonal interpolation of one large Direct3D quad.
		constexpr int tiles = 12;
		for (int y = 0; y < tiles; ++y)
			for (int x = 0; x < tiles; ++x)
			{
				const float x0 = static_cast<float>(x) / tiles, x1 = static_cast<float>(x + 1) / tiles;
				const float y0 = static_cast<float>(y) / tiles, y1 = static_cast<float>(y + 1) / tiles;
				const rect tile(saturation.tl() + vec2{x0 * saturation.width(), y0 * saturation.height()},
					saturation.tl() + vec2{x1 * saturation.width(), y1 * saturation.height()});
				d->add_rect_filled_multicolor(tile, {mix_color(x0, y0), mix_color(x1, y0), mix_color(x1, y1), mix_color(x0, y1)});
			}
	}
	else
	{
		d->add_rect_filled_multicolor(saturation, {color::white(), fake_color, fake_color, color::white()});
		d->add_rect_filled_multicolor(saturation, {color::black_transparent(), color::black_transparent(), color::black(), color::black()});
	}

	const auto active_pos = secondary ? mix_positions[secondary_selected ? 1 : 0] : vec2{fake_saturation, 1.f - fake_brightness};
	const auto sb_grip_pos = saturation.tl() + active_pos * vec2{saturation.width(), saturation.height()};
	const auto grip_color = color(220, 220, 220);

	const auto old_clip = d->g.clip_rect;
	d->override_clip_rect(saturation);
	d->g.anti_alias = true;
	d->add_circle(sb_grip_pos, 3.f, grip_color, 36, 1.f, 2.f);
	if (secondary)
	{
		const auto &other = secondary_selected ? value.get() : secondary->get();
		const auto marker = secondary ? mix_positions[secondary_selected ? 0 : 1] : vec2{other.s(), 1.f - other.v()};
		const auto other_pos = saturation.tl() + marker * vec2{saturation.width(), saturation.height()};
		d->add_circle(other_pos, 3.f, grip_color, 36, 1.f, 2.f);
	}
	d->g.clip_rect = old_clip;

	// apply fake rounding
	d->add_fake_rounding(saturation, colors.bg_block, 4.f);
	d->add_rect_rounded(saturation.expand(1.f), colors.outline, 4.f);
	d->g.anti_alias = false;

	// --- hue ---
	const std::vector<color> hue_parts = {color(255, 0, 0), color(255, 255, 0), color(0, 255, 0), color(0, 255, 255), color(0, 0, 255), color(255, 0, 255),
										  color(255, 0, 0)};

	for (int bar = 0; bar < (secondary ? 2 : 1); ++bar)
	{
		const auto strip = get_hue_rect(bar);
		const auto part_height = strip.height() / static_cast<float>(hue_parts.size() - 1);
		for (size_t i = 0; i < hue_parts.size() - 1; ++i)
		{
			const auto &p = hue_parts[i];
			const auto &n = hue_parts[i + 1];
			d->add_rect_filled_multicolor(strip.translate({0.f, static_cast<float>(i) * part_height}).height(part_height), {p, p, n, n});
		}
		d->add_rect(strip, colors.outline);
		const float selected_hue = secondary ? static_cast<float>(bar == 0 ? mix_left_hue : mix_right_hue) : static_cast<float>(fake_hue);
		const auto grip = rect(strip.tl() + vec2{-1.f, std::clamp(selected_hue / 360.f * strip.height(), 0.f, strip.height() - 1.f)}).size({strip.width() + 2.f, 2.f});
		d->add_rect_filled(grip, colors.text_light);
	}

	// --- alpha ---
	if (allow_alpha)
	{
		d->add_shadow_rect(alpha, 4.f);
		d->add_rect_filled_multicolor(alpha, {active_value()->mod_a(0.f), active_value(), active_value(), active_value()->mod_a(0.f)});

		d->g.anti_alias = true;
		d->add_fake_rounding(alpha, colors.bg_block, 4.f);
		d->add_rect_rounded(alpha.expand(1.f), colors.outline, 4.f);
		d->g.anti_alias = false;

		const auto alpha_grip_pos = alpha.tl() + vec2{std::clamp(active_value()->value.a * alpha.width(), 0.f, alpha.width() - 1), -2.f};
		const auto alpha_grip_rect = rect(alpha_grip_pos).size({2.f, alpha.height() + 4.f});

		d->add_shadow_rect(alpha_grip_rect, 4.f);
		d->add_rect_filled(alpha_grip_rect, colors.text_light);
	}
}

void hsv_slider::on_mouse_down(char key)
{
	if (key == m_left)
	{
		lock_input();

		const auto cur = input.cursor();
		if (get_sb_rect().contains(cur))
		{
			if (secondary)
			{
				const auto sb = get_sb_rect();
				const auto marker = [&](int index, const ren::color &c) {
					const auto p = secondary ? mix_positions[index] : vec2{c.s(), 1.f - c.v()};
					return sb.tl() + p * vec2{sb.width(), sb.height()};
				};
				const auto d1 = (cur - marker(0, value.get())).len_sqr();
				const auto d2 = (cur - marker(1, secondary->get())).len_sqr();
				if (d1 < 100.f || d2 < 100.f)
				{
					secondary_selected = d1 == d2 ? !secondary_selected : d2 < d1;
					run_universal_callbacks();
				}
			}
			current_edit = e_sb;
		}
		else if (get_hue_rect(0).contains(cur))
			current_edit = e_hue;
		else if (secondary && get_hue_rect(1).contains(cur))
			current_edit = e_mix_right;
		else if (get_alpha_rect().contains(cur))
			current_edit = e_alpha;
		else
			current_edit = e_none;

		on_mouse_move(input.cursor(), input.cursor_delta());
	}
}

void hsv_slider::on_mouse_move(const vec2 &p, const vec2 &)
{
	if (!is_locked_by_me() || current_edit == e_none || (current_edit == e_alpha && !allow_alpha))
		return;

	const auto sb = get_sb_rect();
	const auto alpha = get_alpha_rect();

	switch (current_edit)
	{
	case e_sb:
		fake_saturation = std::clamp((p.x - sb.tl().x) / sb.width(), 0.f, 1.f);
		fake_brightness = std::clamp(1.f - (p.y - sb.tl().y) / sb.height(), 0.f, 1.f);
		if (secondary) mix_positions[secondary_selected ? 1 : 0] = {fake_saturation, 1.f - fake_brightness};
		break;
	case e_hue:
	case e_mix_right:
	{
		const auto strip = get_hue_rect(current_edit == e_hue ? 0 : 1);
		const auto h = static_cast<uint16_t>(std::clamp((p.y - strip.tl().y) / strip.height() * 360.f, 0.f, 360.f));
		if (secondary)
			(current_edit == e_hue ? mix_left_hue : mix_right_hue) = h;
		else
			fake_hue = h;
		break;
	}
	case e_alpha:
		*active_value() = active_value()->a(std::clamp((p.x - alpha.tl().x) / alpha.width(), 0.f, 1.f));
		break;
	}

	// Hue handles are palette controls, not derived from the selected circle.
	if (secondary && (current_edit == e_hue || current_edit == e_mix_right))
	{
		// Keep both gradient stops at their positions as the palette changes.
		const auto first = mix_positions[0], second = mix_positions[1];
		value = mix_color(first.x, first.y).a(value->value.a);
		*secondary = mix_color(second.x, second.y).a((*secondary)->value.a);
	}
	else if (secondary && current_edit == e_sb)
	{
		const auto point = mix_positions[secondary_selected ? 1 : 0];
		*active_value() = mix_color(point.x, point.y).a(active_value()->value.a);
	}
	else if (!secondary && current_edit != e_alpha)
		*active_value() = active_value()->hsv(fake_hue, fake_saturation, fake_brightness);

	if (callback)
		callback(*active_value());

	run_universal_callbacks();
}

void hsv_slider::on_mouse_up(char key)
{
	if (key == m_left)
		unlock_input();
}

ren::rect hsv_slider::get_sb_rect()
{
	const auto r = area_abs();
	return r.translate({4.f, 4.f}).size(size - vec2{40.f, allow_alpha ? 40.f : 8.f});
}

ren::rect hsv_slider::get_hue_rect(int index)
{
	const auto r = area_abs();
	const float width = secondary ? 9.f : 20.f;
	return r.translate({r.width() - 28.f + static_cast<float>(index) * 11.f, 4.f}).size({width, r.height() - (allow_alpha ? 40.f : 8.f)});
}

ren::color hsv_slider::mix_color(float x, float y) const
{
	const auto left = color::white().hsv(mix_left_hue, 1.f, 1.f);
	const auto right = color::white().hsv(mix_right_hue, 1.f, 1.f);
	return color::interpolate(color::interpolate(left, right, x), color::interpolate(color::white(), color::black(), x), y);
}

ren::vec2 hsv_slider::find_mix_position(const ren::color &c) const
{
	vec2 best{};
	float best_error = 1e9f;
	for (int y = 0; y <= 30; ++y)
		for (int x = 0; x <= 30; ++x)
		{
			const vec2 p{static_cast<float>(x) / 30.f, static_cast<float>(y) / 30.f};
			const auto candidate = mix_color(p.x, p.y);
			const float r = c.value.r - candidate.value.r, g = c.value.g - candidate.value.g, b = c.value.b - candidate.value.b;
			const float error = r * r + g * g + b * b;
			if (error < best_error) { best_error = error; best = p; }
		}
	return best;
}

ren::rect hsv_slider::get_alpha_rect()
{
	const auto r = area_abs();
	return r.translate({4.f, r.height() - 24.f}).size({r.width() - 12.f, 12.f});
}

void hsv_slider::enable_gradient(value_param<ren::color> &second)
{
	secondary = &second;
	mix_left_hue = value->h();
	mix_right_hue = second->h();
	mix_positions[0] = find_mix_position(value.get());
	mix_positions[1] = find_mix_position(second.get());
}

void hsv_slider::disable_gradient()
{
	secondary = nullptr;
	secondary_selected = false;
	fake_hue = value->h();
	fake_saturation = value->s();
	fake_brightness = value->v();
}

void hsv_slider::reset()
{
	// Explicit edits (hex/paste) update the circle, never the gradient handles.
	if (secondary)
		mix_positions[secondary_selected ? 1 : 0] = find_mix_position(active_value().get());
	else
	{
		fake_hue = active_value()->h();
		fake_saturation = active_value()->s();
		fake_brightness = active_value()->v();
	}

	if (callback)
		callback(*active_value());
}
