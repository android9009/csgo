#include "../visuals.hpp"

namespace menu::group
{
	void visuals_esp(std::shared_ptr<layout> &e)
	{
		//VIRTUALIZER_TIGER_WHITE_START;
		const auto group_sz = window_size_to_groupbox_size(848.f, 588.f) - vec2{0.f, 36.f};

		const auto area = MAKE("visuals>tab>esp", layout, vec2{ 0.f, 36.f }, vec2{ 800.f, 446.f }, s_justify);
		area->make_not_clip();
		area->add(make_groupbox(XOR("visuals>esp>player"), XOR("PLAYER"), group_sz, visuals_esp_player));
		const auto section_size = vec2{group_sz.x, (group_sz.y - 20.f) / 3.f};
		area->add(tools::make_stacked_groupboxes(GUI_HASH("visuals>esp>objects"), group_sz, {
			make_groupbox("visuals>esp>weapon", "WEAPON", section_size, [](auto e) { visuals_esp_world(e, 0); }),
			make_groupbox("visuals>esp>grenades", "GRENADES", section_size, [](auto e) { visuals_esp_world(e, 1); }),
			make_groupbox("visuals>esp>c4", "C4", section_size, [](auto e) { visuals_esp_world(e, 2); })}));
		area->add(
			make_groupbox(
				XOR("visuals>esp>preview"), XOR("PREVIEW"), group_sz, [](auto e)
				{
					{
						const auto c = MAKE("visuals>esp>preview>type", combo_box, cfg.visuals.preview);
						c->legacy_mode = true;
						c->tooltip = "Enemy / Team / Local have separate settings. ESP PLAYER settings follow this Type.";
						c->add(
						{MAKE("visuals>esp>preview>type>enemy", selectable, XOR("Enemy"), 0), MAKE("visuals>esp>preview>type>team", selectable, XOR("Team"), 1),
						 MAKE("visuals>esp>preview>type>local", selectable, XOR("Local"), 2),});

						ADD_INLINE("Type", c, preview_settings("visuals>esp>preview>type"));
					}

					e->add(MAKE("visuals>esp>preview>viewport", model_preview_viewport, vec2{220.f, 380.f}));
				}));

		e->add(area);
		//VIRTUALIZER_TIGER_WHITE_END;
	}

}
