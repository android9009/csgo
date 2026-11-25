#ifndef GUI_TESTER_WINDOW_H
#define GUI_TESTER_WINDOW_H

#include <gui/controls/control_container.h>
#include <gui/input.h>
#include <gui/gui.h>

namespace evo::gui
{
	class window : public control_container
	{
	public:
		window(control_id id, const ren::vec2 &p, const ren::vec2 &s) :
			control_container(id, p, s)
		{
			using namespace ren;

			is_window = true;
			priority = ip_window;
			sort = ++ctx->top_sort;
			type = ctrl_window;

		}

		void add(const std::shared_ptr<control> &c) override;

		void render() override;

		void on_mouse_down(char key) override;
		void on_mouse_up(char key) override;
		void on_mouse_move(const ren::vec2 &p, const ren::vec2 &d) override;

		bool allow_move{true};

	};
}

#endif //GUI_TESTER_WINDOW_H
