#include "visuals.hpp"

namespace menu::group
{
	std::shared_ptr<control> make_model_preview(const char* id, const vec2 &size)
	{
		return std::make_shared<model_preview_viewport>(control_id{hash(id), id}, size, true);
	}

	void update_ragdoll_chams_visibility(bool removed)
	{
		for (const auto id : {GUI_HASH("visuals>chams>enemy>ragdoll>row"), GUI_HASH("visuals>chams>team>ragdoll>row")})
			if (const auto row = ctx->find(id)) row->set_visible(!removed);
	}

	void visuals_tab(std::shared_ptr<layout> &e)
	{
		//VIRTUALIZER_TIGER_WHITE_START;
		const auto tabs = MAKE("visuals>tabs", tabs_layout, vec2{ 0.f, 0.f }, vec2{ 800.f, 22.f }, td_horizontal);
		tabs->add(MAKE("visuals>tabs>esp", child_tab, XOR("ESP"), GUI_HASH("visuals>tab>esp"))->make_selected());
		tabs->add(MAKE("visuals>tabs>chams", child_tab, XOR("Chams"), GUI_HASH("visuals>tab>chams")));
		tabs->add(MAKE("visuals>tabs>misc", child_tab, XOR("Misc"), GUI_HASH("visuals>tab>misc")));

		e->add(tabs);
		visuals_esp(e);
		visuals_models(e);
		visuals_misc(e);
		//VIRTUALIZER_TIGER_WHITE_END;
	}

	std::shared_ptr<settings> preview_settings(const std::string &id)
	{
		auto gear = MAKE_RUNTIME(id + ">settings", settings, nullptr);
		auto disablers = MAKE_RUNTIME(id + ">animation disablers", combo_box, cfg.visuals.disablers);
		disablers->allow_multiple = true;
		disablers->add({
			MAKE_RUNTIME(id + ">model fading", selectable, "Model fading", cfg_t::vis_disable_model_fade),
			MAKE_RUNTIME(id + ">dormancy animation", selectable, "Dormancy animation", cfg_t::vis_disable_dormancy_interp),
			MAKE_RUNTIME(id + ">health bar animation", selectable, "Health bar animation", cfg_t::vis_disable_health_bar_interp),
			MAKE_RUNTIME(id + ">esp fading", selectable, "ESP fading", cfg_t::vis_disable_esp_fade)});
		gear->add(make_control("Animation disablers", disablers));
		return gear;
	}

	void insert_chams(const std::string &id, const std::shared_ptr<combo_box> &c)
	{
        c->add({MAKE_RUNTIME(id+"none",selectable,"None",cfg_t::cham_none),
            MAKE_RUNTIME(id+"textured",selectable,"Textured",cfg_t::cham_textured),
            MAKE_RUNTIME(id+"flat",selectable,"Flat",cfg_t::cham_flat),
            MAKE_RUNTIME(id+"glow",selectable,"Glow",cfg_t::cham_glow),
            MAKE_RUNTIME(id+"sprite",selectable,"Custom Sprite",cfg_t::cham_sprite)});
	}

}
