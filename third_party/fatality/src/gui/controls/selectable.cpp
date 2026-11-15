#include <gui/controls/selectable.h>
#include <gui/controls/color_picker.h>

GUI_NAMESPACE;

void selectable::on_mouse_enter()
{
	if (is_selected)
		return;

	using namespace ren;
	a->direct(u_fc(0.f, custom_color.value_or(colors.text_light)));
}

void selectable::on_mouse_leave()
{
	if (is_selected)
		return;

	using namespace ren;
	a->direct(u_fc(0.f, custom_color.value_or(colors.text_mid)));
}

void selectable::position_swatch()
{
	if (!swatch) return;
	swatch->parent = parent;
	swatch->margin = {};
	swatch->pos = pos + margin.mins + vec2{4.f, (size.y - 8.f) * .5f};
	swatch->size = {16.f, 8.f};
}

void selectable::on_mouse_down(char key)
{
	if (key == m_left && swatch)
	{
		position_swatch();
		if (swatch->area_abs().contains(input.cursor()))
		{
			swatch->on_mouse_down(key);
			return;
		}
	}
	const auto p = get_parent()->as<list>();
	if (!p || (is_selected && !p->allow_multiple) || key != m_left)
		return;

	ctx->do_tick_sound();
	p->update_selected(shared_from_this());
	lock_input();
}

void selectable::on_mouse_up(char)
{
	unlock_input();
}

void selectable::render()
{
	control::render();
	if (!is_visible)
		return;

	a->animate();

	const auto r = area_abs();

	auto &l = draw.layers[ctx->content_layer];

	vec2 off{};
	if (swatch)
	{
		position_swatch();
		swatch->render();
		off.x += 20.f;
	}
	if (show_checkbox)
	{
		const auto box = rect(r.tl() + vec2{4.f, 2.f}).size({12.f, 12.f});
		l->add_rect_filled_rounded(box, colors.bg_block, 2.f);
		l->add_rect_rounded(box, colors.outline, 2.f);
		if (is_selected)
		{
			l->add_line(box.tl() + vec2{2.f, 6.f}, box.tl() + vec2{5.f, 9.f}, colors.accent, 2.f);
			l->add_line(box.tl() + vec2{4.f, 9.f}, box.tl() + vec2{10.f, 3.f}, colors.accent, 2.f);
		}
		off.x += 16.f;
	}
	if (is_loaded)
	{
		l->g.anti_alias = true;
		l->add_circle_filled(r.tl() + vec2{7.f, r.height() * .5f + 1.f}, 2.f, colors.accent);
		l->g.anti_alias = false;

		off.x += 10.f;
	}

	if (is_selected && custom_color)
		off.x += 5.f;

	l->font = is_highlighted || (is_selected && custom_color) ? draw.fonts[GUI_HASH("gui_bold")] : draw.fonts[GUI_HASH("gui_main")];
	const auto start = r.tl() + vec2(4.f, 4.f) + off;
	const auto text_area = rect(start, {std::max(start.x, r.br().x - 4.f), r.br().y});
	const auto shift = scroll_text.update(is_mouse_on_me, l->font->get_text_size(text).x - text_area.width(), draw.frame_time);
	const auto old_clip = l->g.clip_rect;
	l->override_clip_rect(text_area);
	l->add_text(start - vec2{shift, 0.f}, text, a->value.c);
	l->g.clip_rect = old_clip;
}

void selectable::select()
{
	is_selected = true;
	a->direct(u_fc(4.f, custom_color.value_or(colors.accent)));
}

void selectable::unselect()
{
	is_selected = false;
	a->direct(u_fc(0.f, custom_color.value_or(is_mouse_on_me ? colors.text_light : colors.text_mid)));
}
