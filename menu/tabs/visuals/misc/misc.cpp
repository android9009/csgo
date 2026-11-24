#include "../visuals.hpp"

namespace menu::group
{
	void visuals_misc(std::shared_ptr<layout> &e)
	{
		//VIRTUALIZER_TIGER_WHITE_START;
		const auto group_sz = window_size_to_groupbox_size(848.f, 588.f) - vec2{0.f, 36.f};

		const auto area = MAKE("visuals>tab>misc", layout, vec2{ 0.f, 36.f }, vec2{ 800.f, 446.f }, s_justify);
		area->is_visible = false;
		area->make_not_clip();
		area->add(make_groupbox(XOR("visuals>misc>local"), XOR("LOCAL"), group_sz, visuals_misc_local));
		area->add(make_groupbox(XOR("visuals>misc>other"), XOR("OTHER"), group_sz, visuals_misc_other));
		area->add(make_groupbox(XOR("visuals>misc>world"), XOR("WORLD"), group_sz, visuals_misc_world));

		e->add(area);
		//VIRTUALIZER_TIGER_WHITE_END;
	}

}
