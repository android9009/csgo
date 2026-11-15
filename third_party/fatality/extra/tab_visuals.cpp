#include "menu.h"

#include <gui/controls/image.h>
#include <array>

#include "../cfg.h"
#include "macros.h"

using namespace evo::gui;
using namespace evo::gui::helpers;
using namespace evo::ren;

namespace menu::group
{
	constexpr int removal_ragdolls = 7;

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

	static std::shared_ptr<settings> preview_settings(const std::string &id)
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

	void insert_chams(const std::string &id, const std::shared_ptr<combo_box> &c);

	void visuals_esp_world(std::shared_ptr<layout> &e, int category);

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
						c->add(
						{MAKE("visuals>esp>preview>type>enemy", selectable, XOR("Enemy"), 0), MAKE("visuals>esp>preview>type>team", selectable, XOR("Team"), 1),
						 MAKE("visuals>esp>preview>type>local", selectable, XOR("Local"), 2),});

						ADD_INLINE("Type", c, preview_settings("visuals>esp>preview>type"));
					}

					/*const auto img = MAKE("visuals>chams>preview>image2", image, entity_rendering::is_setup ? (entity_rendering::preview_texture->pTextureHandles[0]->lpRawTexture) : nullptr, vec2{ 235.f, 370.f });
					img->rnd = [](const std::shared_ptr<control>& c)
					{
						entity_rendering::preview_pos = { c->pos_abs().x, c->pos_abs().y };
						visuals::queue_custom_player_esp();
						visuals::render_custom_player_esp();
					};
		
					ADD_LINE("preview img 2", img);*/
				}));

		e->add(area);
		//VIRTUALIZER_TIGER_WHITE_END;
	}

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
						c->add(
						{MAKE("visuals>chams>preview>type>enemy", selectable, XOR("Enemy"), 0), MAKE("visuals>chams>preview>type>team", selectable, XOR("Team"), 1),
						 MAKE("visuals>chams>preview>type>local", selectable, XOR("Local"), 2),});

						ADD_INLINE("Type", c, preview_settings("visuals>chams>preview>type"));
					}

					/*const auto img = MAKE("visuals>chams>preview>image", image, entity_rendering::is_setup ? (entity_rendering::preview_texture->pTextureHandles[0]->lpRawTexture) : nullptr, vec2{ 235.f, 370.f });
					img->rnd = [](const std::shared_ptr<control>& c)
					{
						entity_rendering::preview_pos = { c->pos_abs().x, c->pos_abs().y };
						visuals::queue_custom_player_esp();
						visuals::render_custom_player_esp();
					};
		
					ADD_LINE("preview img", img);*/
				}));

		e->add(area);
		//VIRTUALIZER_TIGER_WHITE_END;
	}

	void visuals_misc(std::shared_ptr<layout> &e)
	{
		//VIRTUALIZER_TIGER_WHITE_START;
		const auto group_sz = window_size_to_groupbox_size(848.f, 588.f) - vec2{0.f, 36.f};

		const auto area = MAKE("visuals>tab>misc", layout, vec2{ 0.f, 36.f }, vec2{ 800.f, 446.f }, s_justify);
		area->is_visible = false;
		area->make_not_clip();
		area->add(make_groupbox(XOR("visuals>misc>local"), XOR("LOCAL"), group_sz, visuals_misc_local));
		area->add(make_groupbox(XOR("visuals>misc>various"), XOR("OTHER"), group_sz, visuals_misc_various));
		area->add(make_groupbox(XOR("visuals>misc>beams"), XOR("BEAMS"), group_sz, visuals_misc_beams));

		e->add(area);
		//VIRTUALIZER_TIGER_WHITE_END;
	}

	void visuals_esp_player(std::shared_ptr<layout> &e)
	{


		//VIRTUALIZER_TIGER_WHITE_START;
		ADD("Esp", "visuals>esp>player>esp", checkbox, cfg.visuals.esp.enabled);
		ADD_INLINE(
			"Box", MAKE("visuals>esp>player>box", checkbox, cfg.visuals.esp.box), MAKE("visuals>esp>player>box>color", color_picker, cfg.visuals.esp.box_color, false));
		ADD_INLINE(
			"Skeleton", MAKE("visuals>esp>player>skeleton", checkbox, cfg.visuals.esp.skeleton),
			MAKE("visuals>esp>player>skeleton>color", color_picker, cfg.visuals.esp.skeleton_color, false));
		ADD_INLINE(
			"History skeleton", MAKE("visuals>esp>player>history skeleton", checkbox, cfg.visuals.esp.history_skeleton),
			MAKE("visuals>esp>player>history skeleton>color", color_picker, cfg.visuals.esp. history_skeleton_color, false));
		ADD_INLINE(
			"Name", MAKE("visuals>esp>player>name", checkbox, cfg.visuals.esp.name),
			MAKE("visuals>esp>player>name>color", color_picker, cfg.visuals.esp.name_color, false));
		ADD_INLINE(
			"Weapon", MAKE("visuals>esp>player>weapon", checkbox, cfg.visuals.esp.weapon),
			MAKE("visuals>esp>player>weapon>color", color_picker, cfg.visuals.esp.weapon_color, false));
		ADD_INLINE(
			"Weapon icons", MAKE("visuals>esp>player>weapon icons", checkbox, cfg.visuals.esp.weapon_icon),
			MAKE("visuals>esp>player>weapon icons>color", color_picker, cfg.visuals.esp.weapon_icon_color, false ));
		{
			const auto picker = MAKE("visuals>esp>player>health>color 1", color_picker, cfg.visuals.esp.health_color_1, false);
			picker->gradient_color = &cfg.visuals.esp.health_color_2;
			ADD_INLINE("Health", MAKE("visuals>esp>player>health", checkbox, cfg.visuals.esp.health), picker);
		}


		ADD_INLINE(
			"Armor", MAKE("visuals>esp>player>armor", checkbox, cfg.visuals.esp.armor),
			MAKE("visuals>esp>player>armor>color", color_picker, cfg.visuals.esp.armor_color, false));
		ADD_INLINE(
			"Ammo", MAKE("visuals>esp>player>ammo", checkbox, cfg.visuals.esp.ammo),
			MAKE("visuals>esp>player>ammo>color", color_picker, cfg.visuals.esp.ammo_color, false));



		ADD_INLINE(
			"Sound", MAKE("visuals>esp>player>sound", checkbox, cfg.visuals.esp.sound_esp),
			MAKE("visuals>esp>player>sound>color", color_picker, cfg.visuals.esp.sound_esp_color));



		ADD("Shared ESP", "visuals>esp>player>shared esp", checkbox, cfg.misc.shared_esp);
		ADD("Share with enemies", "visuals>esp>player>share with enemies", checkbox, cfg.misc.shared_esp_with_enemy);


		{
			const auto c = MAKE("visuals>esp>player>info", combo_box, cfg.visuals.esp.flags);
			c->allow_multiple = true;
			c->add(
			{MAKE("visuals>esp>player>info>scoped", selectable, XOR("Scoped"), cfg_t::esp_flag_scoped), MAKE(
				 "visuals>esp>player>info>bomb", selectable, XOR("Bomb"), cfg_t::esp_flag_bomb),
			 MAKE("visuals>esp>player>info>lc", selectable, XOR("Lagcomp break"), cfg_t::esp_flag_lc),
			 MAKE("visuals>esp>player>info>fd", selectable, XOR("Fake duck"), cfg_t::esp_flag_fd),
			 MAKE("visuals>esp>player>info>xp", selectable, XOR("Exploit charge"), cfg_t::esp_flag_xp),
			 MAKE("visuals>esp>player>info>armor", selectable, XOR("Armor"), cfg_t::esp_flag_armor),
			 MAKE("visuals>esp>player>info>hit", selectable, XOR("Hit"), cfg_t::esp_flag_hit),
			 MAKE("visuals>esp>player>info>money", selectable, XOR("Money"), cfg_t::esp_flag_money),
			 MAKE("visuals>esp>player>info>defuser", selectable, XOR("Defuse kit"), cfg_t::esp_flag_defuser), MAKE(
				 "visuals>esp>player>info>taser", selectable, XOR("Taser"), cfg_t::esp_flag_taser), MAKE(
				 "visuals>esp>player>info>immunity", selectable, XOR("Immunity"), cfg_t::esp_flag_immune),});

			ADD_INLINE("Info", c);
		}
		//VIRTUALIZER_TIGER_WHITE_END;
	}

	void visuals_esp_world(std::shared_ptr<layout> &e, int category)
	{
		auto &world = cfg.visuals.esp.world;
		const std::string key = category == 0 ? "visuals>esp>weapon>" : category == 1 ? "visuals>esp>grenades>" : "visuals>esp>c4>";
		auto &enabled = category == 0 ? world.weapon_enabled : category == 1 ? world.grenade_enabled : world.c4_enabled;
		auto &icon_color = category == 0 ? world.weapon_color : world.grenade_color;
		auto &icons = category == 0 ? world.weapon_icons : world.grenade_icons;
		auto &visual = category == 0 ? world.weapon_visuals : category == 1 ? world.grenade_visuals : world.c4_visuals;
		const char *icon_label = category == 0 ? "Weapon icons" : category == 1 ? "Grenade icons" : "C4 icons";
		auto add_inline = [&](const std::string &suffix, const char *label_text, const std::vector<std::shared_ptr<control>> &controls) {
			e->add(make_inlined(hash(key + suffix + ">row"), [&](std::shared_ptr<layout> &row) {
				row->add(std::make_shared<label>(control_id{hash(key + suffix + ">label")}, label_text));
				for (const auto &c : controls) row->add(c);
			}));
		};
		add_inline("enable", "Enable", {MAKE_RUNTIME(key + "enable", checkbox, enabled)});
		if (category != 2)
			add_inline("icons", icon_label, {MAKE_RUNTIME(key + "icons", checkbox, icons), MAKE_RUNTIME(key + "icons>color", color_picker, icon_color)});
		else {
			auto options = MAKE_RUNTIME(key + "options", combo_box, world.c4_options, vec2{}, vec2{80.f, 20.f});
			options->allow_multiple = true;
			options->add({MAKE_RUNTIME(key + "options>timer", selectable, "Timer", 0),
				MAKE_RUNTIME(key + "options>damage", selectable, "Damage", 1),
				MAKE_RUNTIME(key + "options>defuse", selectable, "Defusing", 2),
				MAKE_RUNTIME(key + "options>icons", selectable, "C4 icons", 3)});
			add_inline("options", "Options", {options, MAKE_RUNTIME(key + "timer>color", color_picker, world.c4_timer_color)});
		}
		auto material = MAKE_RUNTIME(key + "chams", combo_box, visual.material, vec2{}, vec2{80.f, 20.f});
		insert_chams(key + "chams>", material);
		e->add(make_control("Chams", material));
		auto glow = MAKE_RUNTIME(key + "glow", combo_box, visual.glow_mode, vec2{}, vec2{80.f, 20.f});
		glow->add({MAKE_RUNTIME(key + "glow>off", selectable, "Off", 0), MAKE_RUNTIME(key + "glow>on", selectable, "On", 1)});
		const auto glow_color = MAKE_RUNTIME(key + "glow>color", color_picker, visual.glow_color);
		glow_color->is_visible = visual.glow_mode->test(1);
		glow->callback = [weak = std::weak_ptr<color_picker>(glow_color)](bits &mode) {
			if (const auto picker = weak.lock()) picker->set_visible(mode.test(1));
		};
		add_inline("glow", "Glow", {glow, glow_color});
	}

	void visuals_models_general(std::shared_ptr<layout> &e)
	{
		//VIRTUALIZER_TIGER_WHITE_START;
		ADD("Enabled", "visuals>chams>general>enabled", checkbox, cfg.visuals.chams.enable);
		ADD("Skip occlusion", "visuals>chams>general>skip occlusion", checkbox, cfg.visuals.chams.check_occlusion);
		ADD_INLINE(
			"Glow", MAKE("visuals>esp>player>glow", checkbox, cfg.visuals.esp.glow.enabled),
			MAKE("visuals>esp>player>glow>settings", settings, [](auto e) { ADD_INLINE("Team", MAKE("visuals>esp>player>glow>settings>team", checkbox, cfg.visuals.esp.
				glow. team), MAKE("visuals>esp>player>glow>settings>team>color", color_picker, cfg.visuals.esp.glow. team_color)); ADD_INLINE("Enemy", MAKE(
				"visuals>esp>player>glow>settings>enemy", checkbox, cfg.visuals.esp.glow .enemy), MAKE("visuals>esp>player>glow>settings>enemy>color", color_picker, cfg.
				visuals.esp.glow. enemy_color)); ADD_INLINE("Local", MAKE("visuals>esp>player>glow>settings>local", checkbox, cfg.visuals.esp.glow .local), MAKE(
				"visuals>esp>player>glow>settings>local>color", color_picker, cfg.visuals.esp.glow. local_color)); }));

		//VIRTUALIZER_TIGER_WHITE_END;
	}

	void insert_chams(const std::string &id, const std::shared_ptr<combo_box> &c)
	{
		c->add(
		{MAKE_RUNTIME(id + XOR("none"), selectable, XOR("None"), cfg_t::cham_none), MAKE_RUNTIME(id + XOR("normal"), selectable, XOR("Normal"), cfg_t::cham_normal),
		 MAKE_RUNTIME(id + XOR("flat"), selectable, XOR("Flat"), cfg_t::cham_flat), MAKE_RUNTIME(id + XOR("rim glow"), selectable, XOR("Rim Glow"), cfg_t::cham_rim_glow),
		 MAKE_RUNTIME(id + XOR("contour"), selectable, XOR("Contour"), cfg_t::cham_contour), MAKE_RUNTIME(id + XOR("flow"), selectable, XOR("Flow"), cfg_t::cham_flow),
		 MAKE_RUNTIME(id + XOR("acrylic"), selectable, XOR("Acrylic"), cfg_t::cham_acrylic),
		 MAKE_RUNTIME(id + XOR("legacy"), selectable, XOR("Legacy"), cfg_t::cham_legacy),});
	}

	void visuals_models_local(std::shared_ptr<layout> &e)
	{
		//VIRTUALIZER_TIGER_WHITE_START;
		{
			const auto c = MAKE("visuals>chams>local>type", combo_box, cfg.visuals.chams.local_pl.mat);
			insert_chams(XOR("visuals>chams>local>type>"), c);

			c->callback = [](bits &v)
			{
				FIND_SAFE(sc, "visuals>chams>local>type>settings>secondary color");
				FIND_SAFE(t, "visuals>chams>local>type>settings");

				t->set_visible(!v.test(cfg_t::cham_none));
				sc->get_parent()->set_visible(
					!(v.test(cfg_t::cham_normal) || v.test(cfg_t::cham_flat) || v.test(cfg_t::cham_flow) || v.test(cfg_t::cham_acrylic) || v.test(cfg_t::cham_legacy)));
			};

			ADD_INLINE(
				"Type", c,
				MAKE("visuals>chams>local>type>settings", settings, [](auto e) { ADD("Primary color", "visuals>chams>local>type>settings>primary color", color_picker, cfg
					. visuals.chams.local_pl.col1); ADD("Secondary color", "visuals>chams>local>type>settings>secondary color", color_picker, cfg .visuals.chams.local_pl.
					col2); })->make_invisible());
		}

		{
			const auto c = MAKE("visuals>chams>local>fake type", combo_box, cfg.visuals.chams.local_fake.mat);
			insert_chams(XOR("visuals>chams>local>fake type>"), c);

			c->callback = [](bits &v)
			{
				FIND_SAFE(sc, "visuals>chams>local>fake type>settings>secondary color");
				FIND_SAFE(t, "visuals>chams>local>fake type>settings");

				t->set_visible(!v.test(cfg_t::cham_none));
				sc->get_parent()->set_visible(
					!(v.test(cfg_t::cham_normal) || v.test(cfg_t::cham_flat) || v.test(cfg_t::cham_flow) || v.test(cfg_t::cham_acrylic) || v.test(cfg_t::cham_legacy)));
			};

			ADD_INLINE(
				"Fake type", c,
				MAKE("visuals>chams>local>fake type>settings", settings, [](auto e) { ADD("Primary color", "visuals>chams>local>fake type>settings>primary color",
						color_picker, cfg.visuals.chams.local_fake.col1); ADD("Secondary color", "visuals>chams>local>fake type>settings>secondary color", color_picker ,
						cfg.
						visuals.chams.local_fake.col2); ADD("Interpolate", "visuals>chams>local>fake type>settings>interpolate", checkbox, cfg. visuals.chams.local_interp
					);
					ADD("Pulsate", "visuals>chams>local>fake type>settings>pulsate", checkbox, cfg.visuals.chams. local_pulsate); })->make_invisible());
		}

		{
			const auto c = MAKE("visuals>chams>local>arms", combo_box, cfg.visuals.chams.local_arms.mat);
			insert_chams(XOR("visuals>chams>local>arms>"), c);

			c->callback = [](bits &v)
			{
				FIND_SAFE(sc, "visuals>chams>local>arms>settings>secondary color");
				FIND_SAFE(t, "visuals>chams>local>arms>settings>primary color");

				t->get_parent()->set_visible(!v.test(cfg_t::cham_none));
				sc->get_parent()->set_visible(
					!(v.test(cfg_t::cham_none) || v.test(cfg_t::cham_normal) || v.test(cfg_t::cham_flat) || v.test(cfg_t::cham_flow) || v.test(cfg_t::cham_acrylic) || v.
						test(cfg_t::cham_legacy)));
			};

			ADD_INLINE(
				"Arms", c,
				MAKE("visuals>chams>local>arms>settings", settings, [](auto e) { ADD_INV("Primary color", "visuals>chams>local>arms>settings>primary color", color_picker,
					cfg .visuals.chams.local_arms.col1); ADD_INV("Secondary color", "visuals>chams>local>arms>settings>secondary color", color_picker, cfg.visuals.chams.
					local_arms.col2); ADD("Hide sleeves", "visuals>chams>local>arms>settings>hide sleeves", checkbox, cfg.visuals. chams.local_nosleeve); }));
		}

		{
			const auto c = MAKE("visuals>chams>local>attachments", combo_box, cfg.visuals.chams.local_att.mat);
			insert_chams(XOR("visuals>chams>local>attachments>"), c);

			c->callback = [](bits &v)
			{
				FIND_SAFE(sc, "visuals>chams>local>attachments>settings>secondary color");
				FIND_SAFE(t, "visuals>chams>local>attachments>settings");

				t->set_visible(!v.test(cfg_t::cham_none));
				sc->get_parent()->set_visible(
					!(v.test(cfg_t::cham_normal) || v.test(cfg_t::cham_flat) || v.test(cfg_t::cham_flow) || v.test(cfg_t::cham_acrylic) || v.test(cfg_t::cham_legacy)));
			};

			ADD_INLINE(
				"Attachments", c,
				MAKE("visuals>chams>local>attachments>settings", settings, [](auto e) { ADD("Primary color", "visuals>chams>local>attachments>settings>primary color",
					color_picker, cfg.visuals.chams.local_att.col1); ADD("Secondary color", "visuals>chams>local>attachments>settings>secondary color", color_picker, cfg.
					visuals.chams.local_att.col2); })->make_invisible());
		}
		//VIRTUALIZER_TIGER_WHITE_END;
	}

	void visuals_models_enemy(std::shared_ptr<layout> &e)
	{
		//VIRTUALIZER_TIGER_WHITE_START;
		{
			const auto c = MAKE("visuals>chams>enemy>player", combo_box, cfg.visuals.chams.enemy_pl.mat);
			insert_chams(XOR("visuals>chams>enemy>player>"), c);

			c->callback = [](bits &v)
			{
				FIND_SAFE(sc, "visuals>chams>enemy>player>settings>secondary color");
				FIND_SAFE(t, "visuals>chams>enemy>player>settings");

				t->set_visible(!v.test(cfg_t::cham_none));
				sc->get_parent()->set_visible(
					!(v.test(cfg_t::cham_normal) || v.test(cfg_t::cham_flat) || v.test(cfg_t::cham_flow) || v.test(cfg_t::cham_acrylic) || v.test(cfg_t::cham_legacy)));
			};

			ADD_INLINE(
				"Player", c,
				MAKE("visuals>chams>enemy>player>settings", settings, [](auto e) { ADD("Primary color", "visuals>chams>enemy>player>settings>primary color", color_picker,
					cfg. visuals.chams.enemy_pl.col1, true, vec2{}, true); ADD("Secondary color", "visuals>chams>enemy>player>settings>secondary color", color_picker, cfg
					.visuals.chams.enemy_pl.col2, true, vec2{}, true); })->make_invisible());
		}



		{
			const auto c = MAKE("visuals>chams>enemy>history", combo_box, cfg.visuals.chams.enemy_backtrack.mat);
			insert_chams("visuals>chams>enemy>history>", c);
			c->callback = [](bits &v) {
				FIND_SAFE(gear, "visuals>chams>enemy>history>settings");
				const bool enabled = !v.none() && !v.test(cfg_t::cham_none);
				gear->set_visible(enabled);
				FIND_SAFE(secondary, "visuals>chams>enemy>history>settings>secondary color");
				secondary->get_parent()->set_visible(enabled && !(v.test(cfg_t::cham_normal) || v.test(cfg_t::cham_flat) || v.test(cfg_t::cham_flow) || v.test(cfg_t::cham_acrylic) || v.test(cfg_t::cham_legacy)));
			};
			ADD_INLINE("Backtrack", c, MAKE("visuals>chams>enemy>history>settings", settings, [](auto e) {
				ADD("Primary color", "visuals>chams>enemy>history>settings>primary color", color_picker, cfg.visuals.chams.enemy_backtrack.col1, true, vec2{}, true);
				const auto secondary = make_control("Secondary color", MAKE("visuals>chams>enemy>history>settings>secondary color", color_picker, cfg.visuals.chams.enemy_backtrack.col2, true, vec2{}, true));
				const auto &v = *cfg.visuals.chams.enemy_backtrack.mat;
				secondary->set_visible(!v.none() && !v.test(cfg_t::cham_none) && !(v.test(cfg_t::cham_normal) || v.test(cfg_t::cham_flat) || v.test(cfg_t::cham_flow) || v.test(cfg_t::cham_acrylic) || v.test(cfg_t::cham_legacy)));
				e->add(secondary);
			})->make_invisible());
		}

		{
			const auto c = MAKE("visuals>chams>enemy>shot record", combo_box, cfg.visuals.chams.enemy_shot.mat);
			insert_chams(XOR("visuals>chams>enemy>shot record>"), c);

			c->callback = [](bits &v)
			{
				FIND_SAFE(sc, "visuals>chams>enemy>shot record>settings>secondary color");
				FIND_SAFE(t, "visuals>chams>enemy>shot record>settings");

				t->set_visible(!v.test(cfg_t::cham_none));
				sc->get_parent()->set_visible(
					!(v.test(cfg_t::cham_normal) || v.test(cfg_t::cham_flat) || v.test(cfg_t::cham_flow) || v.test(cfg_t::cham_acrylic) || v.test(cfg_t::cham_legacy)));
			};

			ADD_INLINE(
				"On shot", c,
				MAKE("visuals>chams>enemy>shot record>settings", settings, [](auto e) { ADD("Primary color", "visuals>chams>enemy>shot record>settings>primary color",
					color_picker, cfg.visuals.chams.enemy_shot.col1, true, vec2{}, true); ADD("Secondary color",
					"visuals>chams>enemy>shot record>settings>secondary color", color_picker, cfg.visuals.chams.enemy_shot.col2, true, vec2{}, true); })->make_invisible(
				));
		}

		{
			const auto c = MAKE("visuals>chams>enemy>ragdoll", combo_box, cfg.visuals.chams.enemy_rag.mat);
			insert_chams(XOR("visuals>chams>enemy>ragdoll>"), c);

			c->callback = [](bits &v)
			{
				FIND_SAFE(sc, "visuals>chams>enemy>ragdoll>settings>secondary color");
				FIND_SAFE(t, "visuals>chams>enemy>ragdoll>settings");

				t->set_visible(!v.test(cfg_t::cham_none));
				sc->get_parent()->set_visible(
					!(v.test(cfg_t::cham_normal) || v.test(cfg_t::cham_flat) || v.test(cfg_t::cham_flow) || v.test(cfg_t::cham_acrylic) || v.test(cfg_t::cham_legacy)));
			};

			const auto gear = MAKE("visuals>chams>enemy>ragdoll>settings", settings, [](auto e) {
				ADD("Primary color", "visuals>chams>enemy>ragdoll>settings>primary color", color_picker, cfg.visuals.chams.enemy_rag.col1, true, vec2{}, true);
				ADD("Secondary color", "visuals>chams>enemy>ragdoll>settings>secondary color", color_picker, cfg.visuals.chams.enemy_rag.col2, true, vec2{}, true);
			})->make_invisible();
			const auto row = make_inlined(GUI_HASH("visuals>chams>enemy>ragdoll>row"), [&](std::shared_ptr<layout> &r) {
				r->add(std::make_shared<label>(control_id{GUI_HASH("visuals>chams>enemy>ragdoll>label")}, "Ragdoll"));
				r->add(c);
				r->add(gear);
			});
			row->is_visible = !cfg.misc.removals->test(removal_ragdolls);
			e->add(row);
		}

		{
			const auto c = MAKE("visuals>chams>enemy>attachments", combo_box, cfg.visuals.chams.enemy_att.mat);
			insert_chams(XOR("visuals>chams>enemy>attachments>"), c);

			c->callback = [](bits &v)
			{
				FIND_SAFE(sc, "visuals>chams>enemy>attachments>settings>secondary color");
				FIND_SAFE(t, "visuals>chams>enemy>attachments>settings");

				t->set_visible(!v.test(cfg_t::cham_none));
				sc->get_parent()->set_visible(
					!(v.test(cfg_t::cham_normal) || v.test(cfg_t::cham_flat) || v.test(cfg_t::cham_flow) || v.test(cfg_t::cham_acrylic) || v.test(cfg_t::cham_legacy)));
			};

			ADD_INLINE(
				"Attachments", c,
				MAKE("visuals>chams>enemy>attachments>settings", settings, [](auto e) { ADD("Primary color", "visuals>chams>enemy>attachments>settings>primary color",
					color_picker, cfg.visuals.chams.enemy_att.col1, true, vec2{}, true); ADD("Secondary color", "visuals>chams>enemy>attachments>settings>secondary color"
					, color_picker, cfg.visuals.chams.enemy_att.col2, true, vec2{}, true); ADD("Ignore Z", "visuals>chams>enemy>attachments>settings>ignore z", checkbox,
					cfg.visuals. chams.enemy_att.ignorez); })->make_invisible());
		}

		ADD_INLINE(
			"Ignore Z", MAKE("visuals>chams>enemy>player>settings>ignore z", checkbox, cfg.visuals.chams.enemy_pl.ignorez),
			MAKE("visuals>chams>enemy>player>settings>hidden color", color_picker, cfg.visuals.chams.enemy_pl. col_z, true, vec2{}, true));
		//VIRTUALIZER_TIGER_WHITE_END;
	}

	void visuals_models_team(std::shared_ptr<layout> &e)
	{
		//VIRTUALIZER_TIGER_WHITE_START;
		{
			const auto c = MAKE("visuals>chams>team>player", combo_box, cfg.visuals.chams.team_pl.mat);
			insert_chams(XOR("visuals>chams>team>player>"), c);

			c->callback = [](bits &v)
			{
				FIND_SAFE(sc, "visuals>chams>team>player>settings>secondary color");
				FIND_SAFE(t, "visuals>chams>team>player>settings");

				t->set_visible(!v.test(cfg_t::cham_none));
				sc->get_parent()->set_visible(
					!(v.test(cfg_t::cham_normal) || v.test(cfg_t::cham_flat) || v.test(cfg_t::cham_flow) || v.test(cfg_t::cham_acrylic) || v.test(cfg_t::cham_legacy)));
			};

			ADD_INLINE(
				"Player", c,
				MAKE("visuals>chams>team>player>settings", settings, [](auto e) { ADD("Primary color", "visuals>chams>team>player>settings>primary color", color_picker,
					cfg. visuals.chams.team_pl.col1, true, vec2{}, true); ADD("Secondary color", "visuals>chams>team>player>settings>secondary color", color_picker, cfg.
					visuals.chams.team_pl.col2, true, vec2{}, true); })->make_invisible());
		}



		{
			const auto c = MAKE("visuals>chams>team>ragdoll", combo_box, cfg.visuals.chams.team_rag.mat);
			insert_chams(XOR("visuals>chams>team>ragdoll>"), c);

			c->callback = [](bits &v)
			{
				FIND_SAFE(sc, "visuals>chams>team>ragdoll>settings>secondary color");
				FIND_SAFE(t, "visuals>chams>team>ragdoll>settings");

				t->set_visible(!v.test(cfg_t::cham_none));
				sc->get_parent()->set_visible(
					!(v.test(cfg_t::cham_normal) || v.test(cfg_t::cham_flat) || v.test(cfg_t::cham_flow) || v.test(cfg_t::cham_acrylic) || v.test(cfg_t::cham_legacy)));
			};

			const auto gear = MAKE("visuals>chams>team>ragdoll>settings", settings, [](auto e) {
				ADD("Primary color", "visuals>chams>team>ragdoll>settings>primary color", color_picker, cfg.visuals.chams.team_rag.col1, true, vec2{}, true);
				ADD("Secondary color", "visuals>chams>team>ragdoll>settings>secondary color", color_picker, cfg.visuals.chams.team_rag.col2, true, vec2{}, true);
			})->make_invisible();
			const auto row = make_inlined(GUI_HASH("visuals>chams>team>ragdoll>row"), [&](std::shared_ptr<layout> &r) {
				r->add(std::make_shared<label>(control_id{GUI_HASH("visuals>chams>team>ragdoll>label")}, "Ragdoll"));
				r->add(c);
				r->add(gear);
			});
			row->is_visible = !cfg.misc.removals->test(removal_ragdolls);
			e->add(row);
		}

		{
			const auto c = MAKE("visuals>chams>team>attachments", combo_box, cfg.visuals.chams.team_att.mat);
			insert_chams(XOR("visuals>chams>team>attachments>"), c);

			c->callback = [](bits &v)
			{
				FIND_SAFE(sc, "visuals>chams>team>attachments>settings>secondary color");
				FIND_SAFE(t, "visuals>chams>team>attachments>settings");

				t->set_visible(!v.test(cfg_t::cham_none));
				sc->get_parent()->set_visible(
					!(v.test(cfg_t::cham_normal) || v.test(cfg_t::cham_flat) || v.test(cfg_t::cham_flow) || v.test(cfg_t::cham_acrylic) || v.test(cfg_t::cham_legacy)));
			};

			ADD_INLINE(
				"Attachments", c,
				MAKE("visuals>chams>team>attachments>settings", settings, [](auto e) { ADD("Primary color", "visuals>chams>team>attachments>settings>primary color",
					color_picker, cfg.visuals.chams.team_att.col1, true, vec2{}, true); ADD("Secondary color", "visuals>chams>team>attachments>settings>secondary color",
					color_picker, cfg.visuals.chams.team_att.col2, true, vec2{}, true); ADD("Ignore Z", "visuals>chams>team>attachments>settings>ignore z", checkbox, cfg.
					visuals. chams.team_att.ignorez); })->make_invisible());
		}

		ADD_INLINE(
			"Ignore Z", MAKE("visuals>chams>team>player>settings>ignore z", checkbox, cfg.visuals.chams.team_pl.ignorez),
			MAKE("visuals>chams>team>player>settings>hidden color", color_picker, cfg.visuals.chams.team_pl.col_z , true, vec2{}, true));
		//VIRTUALIZER_TIGER_WHITE_END;
	}

	void visuals_misc_local(std::shared_ptr<layout> &e)
	{
		//VIRTUALIZER_TIGER_WHITE_START;
		ADD_INLINE(
			"Thirdperson", MAKE("visuals>misc>local>thirdperson", checkbox, cfg.misc.thirdperson),
			MAKE("visuals>misc>local>thirdperson>settings", settings, [](auto e) { ADD("Force when dead", "visuals>misc>local>thirdperson>settings>force when dead",
					checkbox, cfg. misc.thirdperson_dead); ADD("Disable on grenade", "visuals>misc>local>thirdperson>settings>disable on grenade", checkbox, cfg.misc.
					thirdperson_grenade); ADD("Disable animation", "visuals>misc>local>thirdperson>settings>disable animation", checkbox, cfg.misc.thirdperson_no_interp);
				ADD
				("Distance", "visuals>misc>local>thirdperson>settings>distance", slider<float>, 40.f, 180.f, cfg.misc.thirdperson_dist, XOR("%.0fu")); ADD("Transparency",
					"visuals>misc>local>thirdperson>settings>transparency", slider<float>, 0.f, 100.f, cfg.misc.transparency_in_scope, XOR("%.0f%%")); ADD("+ with nades",
					"visuals>misc>local>thirdperson>settings>+ with nades", slider<float>, 0.f, 100.f, cfg.misc.transparency_nade, XOR("%.0f%%")); }));

		ADD_INLINE(
			"Fov changer", MAKE("visuals>misc>local>fov changer", checkbox, cfg.misc.fov_enabled),
			MAKE("visuals>misc>local>fov changer>settings", settings, [](auto e) { ADD("Fov", "visuals>misc>local>fov changer>settings>fov", slider<float>, 60.f, 140.f,
				cfg.misc. fov, XOR("%.0f")); ADD("Remove zoom", "visuals>misc>local>fov changer>settings>remove zoom", slider<float>, 0.f, 100.f, cfg.misc.fov_scope, XOR("%.0f%%"));
				ADD("Force second zoom", "visuals>misc>local>fov>second zoom", checkbox, cfg.misc.force_second_zoom);
				ADD("Force viewmodel in scope", "visuals>misc>local>fov>viewmodel scope", checkbox, cfg.misc.force_viewmodel_scope);
				ADD("Aspect ratio", "visuals>misc>local>fov>aspect", slider<float>, 0.f, 200.f, cfg.misc.aspect, "%.0f%%"); }));

		ADD_INLINE("Viewmodel changer", MAKE("visuals>misc>local>viewmodel", checkbox, cfg.misc.vm_fov_enabled),
			MAKE("visuals>misc>local>viewmodel>settings", settings, [](auto e) {
				ADD("FOV", "visuals>misc>local>viewmodel>fov", slider<float>, 60.f, 140.f, cfg.misc.vm_fov, "%.0f");
				ADD("X", "visuals>misc>local>viewmodel>x", slider<float>, -30.f, 30.f, cfg.misc.viewmodel_x, "%.1f", .1f);
				ADD("Y", "visuals>misc>local>viewmodel>y", slider<float>, -30.f, 30.f, cfg.misc.viewmodel_y, "%.1f", .1f);
				ADD("Z", "visuals>misc>local>viewmodel>z", slider<float>, -30.f, 30.f, cfg.misc.viewmodel_z, "%.1f", .1f);
				ADD("Roll", "visuals>misc>local>viewmodel>roll", slider<float>, -180.f, 180.f, cfg.misc.viewmodel_roll, "%.0f deg");
				ADD("Yaw", "visuals>misc>local>viewmodel>yaw", slider<float>, -180.f, 180.f, cfg.misc.viewmodel_yaw, "%.0f deg");
				ADD("Pitch", "visuals>misc>local>viewmodel>pitch", slider<float>, -180.f, 180.f, cfg.misc.viewmodel_pitch, "%.0f deg");
				ADD("Disable landing bob", "visuals>misc>local>viewmodel>landing bob", checkbox, cfg.misc.disable_landing_bob);
			}));

		ADD_INLINE(
			"Grenade prediction", MAKE("visuals>misc>local>grenade prediction", checkbox, cfg.misc.grenade_tracer),
			MAKE("visuals>misc>local>grenade prediction>hit color", color_picker, cfg.misc.tracer_color)-> make_tooltip(XOR("Hit color")),
			MAKE("visuals>misc>local>grenade prediction>miss color", color_picker, cfg.misc.box_color)-> make_tooltip(XOR("Miss color")));
		ADD_INLINE("Grenade proximity warning", MAKE("visuals>misc>local>grenade proximity warning", checkbox, cfg.misc.grenade_helper),
			MAKE("visuals>misc>local>grenade proximity warning>color", color_picker, cfg.misc.grenade_warning_color));

		{
			const auto c = MAKE("visuals>misc>local>remove scope", combo_box, cfg.misc.noscope);
			c->add({MAKE("visuals>misc>local>remove scope>none", selectable, "None", cfg_t::noscope_none),
				MAKE("visuals>misc>local>remove scope>static", selectable, "Static", cfg_t::noscope_static),
				MAKE("visuals>misc>local>remove scope>dynamic", selectable, "Dynamic", cfg_t::noscope_dynamic)});
			ADD_INLINE("Remove scope", c, MAKE("visuals>misc>local>remove scope>settings", settings, [](auto e) {
				ADD("Line color", "visuals>misc>local>remove scope>settings>line color", color_picker, cfg.misc.scope_line_color);
				ADD("Line size", "visuals>misc>local>remove scope>settings>line size", slider<float>, 5.f, 100.f, cfg.misc.scope_line_size, "%.0f%%");
				ADD("Invert", "visuals>misc>local>remove scope>settings>invert", checkbox, cfg.misc.scope_invert);
			}));
		}
		ADD("Penetration crosshair", "visuals>misc>local>penetration crosshair", checkbox, cfg.visuals.autowall_crosshair);
		ADD("Force crosshair", "visuals>misc>local>force crosshair", checkbox, cfg.misc.force_crosshair);
		{
			const auto spread = MAKE("visuals>misc>local>spread", checkbox, cfg.visuals.force_spread);
			const auto picker = MAKE("visuals>misc>local>spread>color", color_picker, cfg.visuals.spread_circle_color1);
			picker->gradient_color = &cfg.visuals.spread_circle_color2;
			ADD_INLINE("Spread", spread, picker);
		}
		//VIRTUALIZER_TIGER_WHITE_END;
	}

	void visuals_misc_various(std::shared_ptr<layout> &e)
	{
		ADD("Texture transparency", "visuals>misc>other>texture transparency", slider<float>, 0.f, 100.f, cfg.misc.texture_transparency, "%.0f%%");
		ADD("Prop transparency", "visuals>misc>other>prop transparency", slider<float>, 0.f, 100.f, cfg.visuals.prop_transparency, "%.0f%%");
		{
			auto tracer = MAKE("visuals>misc>other>tracers", combo_box, cfg.misc.tracer_targets, vec2{}, vec2{80.f, 20.f});
			tracer->allow_multiple = true;
			tracer->expand_popup = true;
			std::vector<std::shared_ptr<layout>> rows;
			const std::array<const char *, 3> names{"Local", "Enemy", "Teammates"};
			for (int i = 0; i < 3; ++i) {
				const auto key = std::string("visuals>misc>other>tracers>") + std::to_string(i);
				tracer->add(MAKE_RUNTIME(key, selectable, names[i], i));
				auto style = MAKE_RUNTIME(key + ">style", combo_box, cfg.misc.tracer_style[i], vec2{}, vec2{70.f, 20.f});
				style->add({MAKE_RUNTIME(key + ">laser", selectable, "Laser", 0), MAKE_RUNTIME(key + ">line", selectable, "Line", 1)});
				auto color = MAKE_RUNTIME(key + ">color", color_picker, cfg.misc.tracer_colors[i]);
				auto row = make_inlined(hash(key + ">row"), [&](std::shared_ptr<layout> &r) { r->add(std::make_shared<label>(control_id{hash(key + ">label")}, "Style")); r->add(style); r->add(color); });
				row->is_visible = cfg.misc.tracer_targets->test(i);
				rows.push_back(row);
				tracer->add(row);
			}
			tracer->callback = [rows](bits &v) { for (int i = 0; i < 3; ++i) rows[i]->set_visible(v.test(i)); };
			ADD_INLINE("Bullet tracers", tracer);
		}
		{
			const auto c = MAKE("visuals>misc>other>hitmarker", combo_box, cfg.misc.hitmarker);
			c->allow_multiple = true;
			c->add({MAKE("visuals>misc>other>hitmarker>world", selectable, "World", cfg_t::hitmarker_world), MAKE("visuals>misc>other>hitmarker>crosshair", selectable, "Crosshair", cfg_t::hitmarker_cross)});
			ADD_INLINE("Hitmarker", c);
		}
		ADD_INLINE("Show impacts", MAKE("visuals>misc>other>impacts", checkbox, cfg.misc.impacts),
			MAKE("visuals>misc>other>impacts>color1", color_picker, cfg.misc.impacts_color), MAKE("visuals>misc>other>impacts>color2", color_picker, cfg.misc.impacts_color2));
		ADD_INLINE("Out of FOV", MAKE("visuals>misc>other>out of fov", checkbox, cfg.visuals.out_of_fov),
			MAKE("visuals>misc>other>out of fov>settings", settings, [](auto e) {
				ADD("Color", "visuals>misc>other>out of fov>color", color_picker, cfg.visuals.out_of_fov_col);
				ADD("Distance", "visuals>misc>other>out of fov>distance", slider<float>, 1.f, 100.f, cfg.visuals.out_of_fov_dst, "%.0f%%");
				ADD("Size", "visuals>misc>other>out of fov>size", slider<float>, 1.f, 50.f, cfg.visuals.out_of_fov_size, "%.0fpx");
			}));
		{
			const auto c = MAKE("visuals>misc>other>removals", combo_box, cfg.misc.removals);
			c->allow_multiple = true;
			c->callback = [](bits &v) { update_ragdoll_chams_visibility(v.test(removal_ragdolls)); };
			const std::array<const char *, 10> names{"Flashbang Effect", "Smoke Rendering", "Particle", "Post Processing", "Water Effects", "Blood Effects", "Decals", "Ragdolls", "Visual punch", "Shadows"};
			for (int i = 0; i < static_cast<int>(names.size()); ++i) {
				const auto item = MAKE_RUNTIME(std::string("visuals>misc>other>removals>") + std::to_string(i), selectable, names[i], i);
				item->show_checkbox = true;
				c->add(item);
			}
			ADD_INLINE("Removals", c);
		}
	}

	void visuals_misc_beams(std::shared_ptr<layout> &e)
	{
		//VIRTUALIZER_TIGER_WHITE_START;
		ADD("Enabled", "visuals>misc>beams>enabled", checkbox, cfg.visuals.beams.enabled);
		ADD("Width", "visuals>misc>beams>width", slider<float>, 1.f, 20.f, cfg.visuals.beams.width, XOR("%.0f"));

		ADD_INLINE(
			"Team", MAKE("visuals>misc>beams>team", checkbox, cfg.visuals.beams.team_enabled),
			MAKE("visuals>misc>beams>team>color", color_picker, cfg.visuals.beams.team_color));
		ADD_INLINE(
			"Enemy", MAKE("visuals>misc>beams>enemy", checkbox, cfg.visuals.beams.enemy_enabled),
			MAKE("visuals>misc>beams>enemy>color", color_picker, cfg.visuals.beams.enemy_color));
		ADD_INLINE(
			"Local", MAKE("visuals>misc>beams>local", checkbox, cfg.visuals.beams.local_enabled),
			MAKE("visuals>misc>beams>local>color", color_picker, cfg.visuals.beams.local_color));
		//VIRTUALIZER_TIGER_WHITE_END;
	}
}
