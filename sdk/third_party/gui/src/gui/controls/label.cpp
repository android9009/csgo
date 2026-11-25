#include <gui/controls/label.h>
#include <gui/controls/checkbox.h>
#include <gui/controls/layout.h>
#include <algorithm>

GUI_NAMESPACE;

void label::on_draw_param_changed(char w)
{
	control::on_draw_param_changed(w);
	if (w == dp_text)
		size = fnt->get_text_size(text);
}

void label::on_first_render_call()
{
	// retry, bitch!
	fnt = draw.fonts[bold ? GUI_HASH("gui_bold") : GUI_HASH("gui_main")];
	size = {fnt->get_text_size(text).x, 10.f};
}

void label::on_mouse_down(char key)
{
	// meme thing: let's see if there's a checkbox nearby...
	const auto par = get_parent();
	if (!par)
		return;

	par->as<control_container>()->for_each_control_logical(
		[&](auto f)
		{
			if (f->type == ctrl_checkbox)
			{
				// don't do this shit  :)
				f->on_mouse_down(key);
				f->is_caring_about_hover = f->is_caring_about_parent = true;
				ctx->active = nullptr;
				lock_input();
				return true;
			}

			return false;
		});
}

void label::on_mouse_up(char)
{
	// do the same meme here again! but only if it's locked
	const auto par = get_parent();
	if (!is_locked_by_me() || !par)
		return;

	unlock_input();
}

void label::on_mouse_enter()
{
	const auto par = get_parent();
	if (!par)
		return;

	par->as<control_container>()->for_each_control_logical(
		[&](auto f)
		{
			if (f->type == ctrl_checkbox)
			{
				f->on_mouse_enter();
				return true;
			}

			return false;
		});
}

void label::on_mouse_leave()
{
	const auto par = get_parent();
	if (!par)
		return;

	par->as<control_container>()->for_each_control_logical(
		[&](auto f)
		{
			if (f->type == ctrl_checkbox)
			{
				f->on_mouse_leave();
				return true;
			}

			return false;
		});
}

void label::render()
{
	control::render();
	if (!is_visible)
		return;

	auto &d = draw.layers[ctx->content_layer];

	vec2 offset{};
	if (is_new)
	{
		d->g.anti_alias = true;
		d->add_circle_filled(pos_abs() + vec2{2.f, size.y / 2.f}, 2.f, colors.accent);
		d->g.anti_alias = false;
		offset.x += 8.f;
	}

	d->font = fnt;
	const auto start = pos_abs() + offset;
	float right = start.x + fnt->get_text_size(text).x;
	const auto parent_control = get_parent();
	if (parent_control && parent_control->type == ctrl_layout && parent_control->as<layout>()->direction == s_inline)
	{
		const auto row = parent_control->as<layout>();
		right = row->area_abs().br().x;
		row->for_each_control([&](const std::shared_ptr<control> &other) {
			if (other.get() != this && other->is_visible && other->area_abs().tl().x > start.x)
				right = std::min(right, other->area_abs().tl().x - 4.f);
		});
	}
	right = std::max(right, start.x);
	const float width = right - start.x;
	const auto clip = rect(start, {right, start.y + std::max(size.y, fnt->get_text_size(text).y)});
	const float shift = scroll_text.update(clip.contains(input.cursor()) && is_mouse_on_me,
		fnt->get_text_size(text).x - width, draw.frame_time);
	const auto old_clip = d->g.clip_rect;
	d->override_clip_rect(clip);
	d->add_text(start - vec2{shift, 0.f}, text, col);
	d->g.clip_rect = old_clip;
}
