#ifndef COLOR_PICKER_FC7A748F3EEE4C20945E339F7E805589_H
#define COLOR_PICKER_FC7A748F3EEE4C20945E339F7E805589_H

#include <gui/control.h>
#include <gui/values.h>

#include <functional>

namespace evo::gui
{
	class color_picker : public control
	{
	public:
		color_picker(control_id id, value_param<ren::color> &v, bool alpha = true, const ren::vec2 &p = {}, bool left = false) :
			control(id, p, {24.f, 8.f}), allow_alpha(alpha), prefer_left(left), value(v), gradient_secondary(v.get())
		{
			margin = {2.f, 8.f, 2.f, 8.f};
			type = ctrl_color_picker;
		}

		void on_mouse_down(char key) override;
		void render() override;

		bool allow_alpha{true};
		bool prefer_left{};
		value_param<ren::color> &value;
		value_param<ren::color> *gradient_color{};
		value_param<bool> gradient_enabled{};
		value_param<bool>* gradient_state{};
		value_param<bool>& gradient_setting(){return gradient_state ? *gradient_state : gradient_enabled;}
		value_param<ren::color> gradient_secondary;
		value_param<ren::color> &second_color() { return gradient_color ? *gradient_color : gradient_secondary; }

		std::function<void(ren::color)> callback{};
	};
}

#endif //COLOR_PICKER_FC7A748F3EEE4C20945E339F7E805589_H
