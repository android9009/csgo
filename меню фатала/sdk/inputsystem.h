#pragma once

#include <game/game.h>

namespace sdk
{
#ifdef FATALITY_GUI
	struct cinput_system
	{
		bool relative_mouse_mode{ false };
		void* sdl_window{ nullptr };

		bool get_relative_mouse_mode() const { return relative_mouse_mode; }
		void set_input(const bool enabled) {}
	};
#else
	struct cinput_system
	{
		MEMBER_CUSTOM(relative_mouse_mode, cinput_system, bool);
		MEMBER_CUSTOM(sdl_window, cinput_system, void*);

		void set_input(const bool enabled)
		{
			if (!get_relative_mouse_mode())
				return;
		}
	};
#endif
}
