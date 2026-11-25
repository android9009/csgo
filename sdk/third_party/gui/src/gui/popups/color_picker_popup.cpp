#include <gui/popups/color_picker_popup.h>
#include <gui/controls/hsv_slider.h>
#include <gui/gui.h>
#include <gui/controls/button.h>
#include <gui/controls/text_input.h>
#include <gui/controls/checkbox.h>
#include <gui/controls/label.h>

GUI_NAMESPACE;

void color_picker_popup::on_first_render_call()
{
	popup::on_first_render_call();

	pos = calc_perfect_position(pos + vec2{picker->prefer_left ? -size.x : 0.f, picker->size.y + 4.f});

	// create hsv picker
	const auto p = std::make_shared<hsv_slider>(control_id{GUI_HASH("cpp_hsv")}, picker->value, picker->allow_alpha);
	if (picker->gradient_setting().get())
		p->enable_gradient(picker->second_color());
	p->pos = {8.f, 8.f};
	p->callback = picker->callback;
	p->universal_callbacks = picker->universal_callbacks;
	p->universal_callbacks[UINT32_MAX].push_back(
		[this, weak = std::weak_ptr<hsv_slider>(p)]()
		{
			const auto p = weak.lock();
			if (!p) return;
			const auto shit = find<text_input>(GUI_HASH("cpp_txt"));
			const auto old_cbk = shit->callback;
			shit->callback = {};
			shit->set_value(color_to_string(p->active_value().get()));
			shit->move_caret_to_end();
			shit->callback = old_cbk;
		});

	add(p);

	// copy/paste buttons
	const auto copy = std::make_shared<button>(GUI_HASH("cpp_copy"), (""), ctx->res.icons.copy, vec2{8.f, picker->allow_alpha ? 210.f : 170.f}, vec2{0.f, 16.f});
	copy->tooltip = ("Copy");
	copy->callback = [p]()
	{
		clipboard::set(color_to_string(p->active_value().get()));
	};

	const auto paste = std::make_shared<button>(GUI_HASH("cpp_paste"), (""), ctx->res.icons.paste, vec2{30.f, picker->allow_alpha ? 210.f : 170.f}, vec2{0.f, 16.f});
	paste->tooltip = ("Paste");
	paste->callback = [this, p]()
	{
		const auto c = color_from_string(clipboard::get());
		if (!c)
			return;

		auto &picker_v = p->active_value().get();
		picker_v.value = c->value;
		if (!picker->allow_alpha)
			picker_v.value.a = 1.f;

		find<hsv_slider>(GUI_HASH("cpp_hsv"))->reset();
		find<text_input>(GUI_HASH("cpp_txt"))->set_value(color_to_string(p->active_value().get()));
	};

	const auto txt = std::make_shared<text_input>(
		GUI_HASH("cpp_txt"), vec2{50.f, picker->allow_alpha ? 208.f : 168.f}, vec2{152.f, 20.f}, std::regex(("[#0-9a-f]+")), std::wregex((L"[#0-9a-f]")), 9);
	txt->set_value(color_to_string(p->active_value().get()));
	txt->move_caret_to_end();
	txt->callback = [this, p](std::string &str)
	{
		if (const auto c = color_from_string(str); c)
		{
			p->active_value()->value = c->value;
			if (!picker->allow_alpha)
				p->active_value()->value.a = 1.f;
			find<hsv_slider>(GUI_HASH("cpp_hsv"))->reset();
		}
	};

	add(copy);
	add(paste);
	add(txt);

	const float gradient_y = picker->allow_alpha ? 237.f : 197.f;
	const auto gradient = std::make_shared<checkbox>(control_id{GUI_HASH("cpp_gradient")}, picker->gradient_setting(), vec2{0.f, 0.f});
	gradient->callback = [this, p, txt](bool enabled) {
		if (enabled) p->enable_gradient(picker->second_color());
		else p->disable_gradient();
		const auto old = txt->callback;
		txt->callback = {};
		txt->set_value(color_to_string(p->active_value().get()));
		txt->move_caret_to_end();
		txt->callback = old;
	};
	gradient->margin={};gradient->reset_internal();
	auto gradient_row=std::make_shared<control_container>(control_id{id+80},vec2{8.f,gradient_y},vec2{196.f,16.f});
	gradient_row->add(gradient);
	gradient_row->add(std::make_shared<label>(control_id{GUI_HASH("cpp_gradient_label")},"Gradient",colors.text,false,vec2{18.f,1.f}));
	add(gradient_row);
	process_queues();raise_first_render_call();

	ctx->top_sort = sort;
	ctx->active_popups.emplace_back(as<popup>());
}

void color_picker_popup::render()
{
	if (!is_visible)
		return;

	begin_render();

	const auto area = area_abs();

	auto &d = draw.layers[ctx->content_layer];
	d->add_shadow_rect(area, 12.f, true);
	d->g.anti_alias = true;
	d->add_rect_filled_rounded(area, colors.bg_block, 4.f);
	d->add_rect_rounded(area, colors.outline, 4.f);
	d->g.anti_alias = false;

	popup::render();
}
