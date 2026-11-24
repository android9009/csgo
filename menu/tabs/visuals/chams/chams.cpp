#include "../visuals.hpp"

namespace menu::group
{
	void visuals_models(std::shared_ptr<layout> &e)
	{
		//VIRTUALIZER_TIGER_WHITE_START;
		const auto group_sz = window_size_to_groupbox_size(848.f, 588.f) - vec2{0.f, 36.f};
		const auto group_half_sz = group_sz * vec2{1.f, .5f} - vec2{0.f, 5.f};

		const auto area = MAKE("visuals>tab>chams", layout, vec2{ 0.f, 36.f }, vec2{ 800.f, 446.f }, s_justify);
		area->is_visible = false;
		area->make_not_clip();
		area->add(
			tools::make_stacked_groupboxes(
				GUI_HASH("visuals>chams>general,local"), group_sz,
				{make_groupbox(XOR("visuals>chams>general"), XOR("GENERAL"), group_half_sz, visuals_models_general),
				 make_groupbox(XOR("visuals>chams>local"), XOR("LOCAL"), group_half_sz, visuals_models_local)}));
		area->add(
			tools::make_stacked_groupboxes(
				GUI_HASH("visuals>chams>enemy,team"), group_sz, {make_groupbox(XOR("visuals>chams>enemy"), XOR("ENEMY"), group_half_sz, visuals_models_enemy),
																 make_groupbox(XOR("visuals>chams>team"), XOR("TEAM"), group_half_sz, visuals_models_team)}));
		area->add(
			make_groupbox(
				XOR("visuals>chams>preview"), XOR("PREVIEW"), group_sz, [](auto e)
				{
					{
						const auto c = MAKE("visuals>chams>preview>type", combo_box, cfg.visuals.preview);
						c->legacy_mode = true;
						c->tooltip = "Enemy / Team / Local have separate settings. ESP PLAYER settings follow this Type.";
						c->add(
						{MAKE("visuals>chams>preview>type>enemy", selectable, XOR("Enemy"), 0), MAKE("visuals>chams>preview>type>team", selectable, XOR("Team"), 1),
						 MAKE("visuals>chams>preview>type>local", selectable, XOR("Local"), 2),});

						ADD_INLINE("Type", c, preview_settings("visuals>chams>preview>type"));
					}

					e->add(MAKE("visuals>chams>preview>viewport", model_preview_viewport, vec2{220.f, 380.f}, false, true));
				}));

		e->add(area);
		//VIRTUALIZER_TIGER_WHITE_END;
	}

}
