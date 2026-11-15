#include <gui/controls/color_picker.h>
#include <gui/gui.h>
#include <gui/popups/color_picker_popup.h>

GUI_NAMESPACE;

void color_picker::render()
{
	control::render();

	const auto r = area_abs();

	auto &d = draw.layers[ctx->content_layer];
	d->g.anti_alias = true;
	if (*gradient_enabled)
	{
		const auto first = value.get();
		const auto second = second_color().get();
		d->add_rect_filled_multicolor(r, {first, second, second, first});
		d->add_fake_rounding(r, colors.bg_block, 2.f);
	}
	else
		d->add_rect_filled_rounded(r, *value, 2.f);
	d->g.anti_alias = {};
}

void color_picker::on_mouse_down(char key)
{
	if (key == m_left)
	{
		const auto picker_popup = std::make_shared<color_picker_popup>(control_id{id + 1}, shared_from_this()->as<color_picker>());
		picker_popup->open();
	}
}
