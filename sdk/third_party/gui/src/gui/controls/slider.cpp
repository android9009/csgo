#include <gui/controls/slider.h>
#include <algorithm>
#include <cmath>
#include <type_traits>
#include <cstring>

GUI_NAMESPACE;

#define MAKE_TEMPLATE_FN_DEF(r, f) template r slider<int>::f ; template r slider<float>::f ; template r slider<double>::f ;

template <typename T>
void slider<T>::on_mouse_down(char key)
{
	if (key == m_left)
	{
		lock_input();
		update_value();
	}
}

template <typename T>
void slider<T>::on_mouse_move(const vec2 &, const vec2 &)
{
	if (is_locked_by_me())
		update_value();
}

template <typename T>
void slider<T>::preserve_hotkey_state()
{
	pres_state.is_engaged = in_hotkey;
	static_assert(sizeof(T)<=sizeof(pres_state.hk_value));
	const T hk = in_hotkey ? *value : T{};
	const T real = in_hotkey ? old_value : *value;
	std::memcpy(&pres_state.hk_value, &hk, sizeof(T));
	std::memcpy(&pres_state.real_value, &real, sizeof(T));

	*value = old_value;
}

template <typename T>
void slider<T>::restore_hotkey_state()
{
	in_hotkey = pres_state.is_engaged;
	T restored{};
	const auto& stored = in_hotkey ? pres_state.hk_value : pres_state.real_value;
	std::memcpy(&restored, &stored, sizeof(T));
	*value = restored;
}

template <typename T>
void slider<T>::on_key_down(uint16_t key)
{
	if (!is_mouse_on_me)
		return;

	const auto o = *value;
	if (key == VK_LEFT || key == VK_DOWN)
		*value = static_cast<T>(*value - step);
	if (key == VK_RIGHT || key == VK_UP)
		*value = static_cast<T>(*value + step);

	*value = std::clamp(*value, low, high);
	if (o != *value)
	{
		if (callback)
			callback(o, *value);

		in_hotkey = false;
		run_universal_callbacks();
		ctx->do_tick_sound();
	}
}

template <typename T>
void slider<T>::on_mouse_wheel(float factor)
{
	// Consume even at a limit: scrolling over a slider must not move its panel.
	input.consume_wheel();
	if constexpr (std::is_integral_v<T>)
	{
		wheel_remainder += factor;
		factor = std::trunc(wheel_remainder);
		wheel_remainder -= factor;
	}
	const auto o = *value;
	const auto next = std::clamp(static_cast<double>(o) + static_cast<double>(step) * factor,
		static_cast<double>(low), static_cast<double>(high));
	*value = static_cast<T>(next);

	if (o != *value)
	{
		if (callback)
			callback(o, *value);

		in_hotkey = false;
		run_universal_callbacks();
		ctx->do_tick_sound();
	}
}

template <typename T>
void slider<T>::on_mouse_up(char key)
{
	if (key == m_left)
	{
		unlock_input();
		ctx->do_tick_sound();
	}
}

template <typename T>
void slider<T>::render()
{
	control::render();

	const auto r = area_abs().floor();
	const auto f = get_fill();

	auto &d = draw.layers[ctx->content_layer];
	d->add_rect_filled_rounded(r, colors.bg_bottom, 2.f);

	d->g.anti_alias = true;
	if (f.x >= 8.f)
		d->add_rect_filled_rounded(r.size(f).shrink(2.f).floor(), colors.accent, 2.f);
	else if (f.x >= 6.f)
		d->add_rect_filled_rounded(r.size(f).shrink(2.f).floor(), colors.accent, 2.f, rnd_l);
	d->g.anti_alias = false;

	const auto m = draw.fonts[GUI_HASH("gui_main")];
	const auto value_s = get_formatted(*value);

	const auto old_clip = d->g.clip_rect;

	const bool show_infinity = infinite_at_min && *value == low;
	d->font = show_infinity ? draw.fonts[GUI_HASH("gui_infinity")] : m;
	const auto draw_value = [&](const color &c) {
		d->add_text(r.center() + vec2{0.f, 1.f}, show_infinity ? "\xE2\x88\x9E" : value_s, c,
			text_params::with_vh(align_center, align_center));
	};
	draw_value(colors.text_dark);
	d->override_clip_rect(r.size(f));
	draw_value(colors.text);
	d->g.clip_rect = old_clip;
}

template <typename T>
void slider<T>::update_value()
{
	if (step <= 0.f || size.x <= 0.f) return;
	const auto step_frac = 1.0 / static_cast<double>(step);
	const auto cursor = input.cursor();

	const auto o = *value;

	const double raw = static_cast<double>((cursor.x - pos_abs().x) / size.x) *
		(static_cast<double>(high) - static_cast<double>(low)) + static_cast<double>(low);
	*value = static_cast<T>(std::clamp(std::floor(raw * step_frac + 0.5) / step_frac,
		static_cast<double>(low), static_cast<double>(high)));

	if (o != *value)
	{
		if (callback)
			callback(o, *value);

		in_hotkey = false;
		run_universal_callbacks();
	}
}

template <typename T>
void slider<T>::determine_type()
{
	hk_type = std::is_floating_point<T>() ? st_float : st_int;
}

template <typename T>
void slider<T>::reset()
{
	if (callback)
		callback(value, value);

	hotkeys = value.get_hotkey_table();
	clean_up_hotkeys();
	control::update_hotkey_table();
	run_universal_callbacks();
	notify_nearby_label();

	if (!handling_hotkey && ctx->should_update_old_value)
	{
		old_value = *value;
		in_hotkey = false;
	}
}

template <typename T>
void slider<T>::update_hotkey_table()
{
	clean_up_hotkeys();
	control::update_hotkey_table();
	value.update_hotkeys(hotkeys);
	notify_nearby_label();
}

template <typename T>
void slider<T>::update_hotkey_value(hotkey_update upd, hotkey_info v)
{
	if (upd == hku_click)
	{
		if (v.beh == hkb_toggle)
		{
			if (in_hotkey)
				*value = old_value;
			else
			{
				old_value = *value;
				*value = *(T *)&v.value;
			}

			in_hotkey = !in_hotkey;
		}
		else
		{
			if (!in_hotkey)
			{
				old_value = *value;
				*value = *(T *)&v.value;
			}

			in_hotkey = true;
		}
	}
	else
	{
		if (v.beh == hkb_hold && in_hotkey)
		{
			*value = old_value;
			in_hotkey = false;
		}
	}

	handling_hotkey = true;
	reset();
	handling_hotkey = false;
}

MAKE_TEMPLATE_FN_DEF(void, on_mouse_down(char key));
MAKE_TEMPLATE_FN_DEF(void, on_mouse_move(const ren::vec2 &p, const ren::vec2 &d));
MAKE_TEMPLATE_FN_DEF(void, on_key_down(uint16_t key));
MAKE_TEMPLATE_FN_DEF(void, on_mouse_up(char key));
MAKE_TEMPLATE_FN_DEF(void, on_mouse_wheel(float factor));
MAKE_TEMPLATE_FN_DEF(void, render());
MAKE_TEMPLATE_FN_DEF(void, update_value());
MAKE_TEMPLATE_FN_DEF(void, determine_type());
MAKE_TEMPLATE_FN_DEF(void, reset());
MAKE_TEMPLATE_FN_DEF(void, update_hotkey_table());
MAKE_TEMPLATE_FN_DEF(void, update_hotkey_value(hotkey_update upd, hotkey_info v));
MAKE_TEMPLATE_FN_DEF(void, preserve_hotkey_state());
MAKE_TEMPLATE_FN_DEF(void, restore_hotkey_state());

#undef MAKE_TEMPLATE_FN_DEF
