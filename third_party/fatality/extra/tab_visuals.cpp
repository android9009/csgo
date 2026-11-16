#include "menu.h"

#include <gui/controls/image.h>
#include <array>
#include <algorithm>
#include <cmath>

#include "../cfg.h"
#include "../preview_bridge.hpp"
#include "macros.h"

using namespace evo::gui;
using namespace evo::gui::helpers;
using namespace evo::ren;

namespace menu::group
{

	class model_preview_viewport final : public control
	{
	public:
		model_preview_viewport(control_id id, const vec2 &size, bool skins = false) : control(id, {}, size), skins_preview(skins) { }

		void render() override
		{
			control::render();
			if (!is_visible) return;
			auto &layer = draw.layers[ctx->content_layer];
			FatalityPreviewBridge::render_ct.store(!skins_preview || FatalityPreviewBridge::skins_team_ct.load(std::memory_order_relaxed), std::memory_order_relaxed);
			FatalityPreviewBridge::render_skins.store(skins_preview, std::memory_order_relaxed);
			if (!skins_preview)
				FatalityPreviewBridge::visuals_subject.store(static_cast<int>(cfg.visuals.preview.get().first_set_bit().value_or(0)), std::memory_order_relaxed);
			if (skins_preview && size.y > 0.f)
				FatalityPreviewBridge::skins_aspect.store(size.x / size.y, std::memory_order_relaxed);
			FatalityPreviewBridge::visible.store(true, std::memory_order_relaxed);
			if (auto texture = FatalityPreviewBridge::texture.load(std::memory_order_acquire)) {
				layer->g.texture = texture;
				layer->add_rect_filled(area_abs(), color::white());
				layer->g.texture = {};
			} else {
				layer->font = draw.fonts[GUI_HASH("gui_main")];
				layer->add_text(area_abs().center(), "Model preview unavailable", colors.text_dark,
					text_params::with_vh(align_center, align_center));
			}
		}

		void on_mouse_down(char key) override
		{
			if (key == m_left) lock_input();
		}

		void on_mouse_up(char key) override
		{
			if (key == m_left && is_locked_by_me()) unlock_input();
		}

		void on_mouse_move(const vec2 &, const vec2 &delta) override
		{
			if (is_locked_by_me()) {
				const auto yaw = FatalityPreviewBridge::yaw.load(std::memory_order_relaxed);
				FatalityPreviewBridge::yaw.store(std::remainder(yaw + delta.x * 0.5f, 360.f), std::memory_order_relaxed);
			}
		}

		void on_mouse_wheel(float) override
		{
			// Preview cameras have a fixed distance; the wheel must never zoom them.
			input.consume_wheel();
		}

	private:
		bool skins_preview{};
	};

	std::shared_ptr<control> make_model_preview(const char* id, const vec2 &size)
	{
		return std::make_shared<model_preview_viewport>(control_id{hash(id), id}, size, true);
	}

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

					e->add(MAKE("visuals>esp>preview>viewport", model_preview_viewport, vec2{220.f, 380.f}));
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

					e->add(MAKE("visuals>chams>preview>viewport", model_preview_viewport, vec2{220.f, 380.f}));
				}));

		e->add(area);
		//VIRTUALIZER_TIGER_WHITE_END;
	}

	void visuals_misc_world(std::shared_ptr<layout> &e);

	void visuals_misc(std::shared_ptr<layout> &e)
	{
		//VIRTUALIZER_TIGER_WHITE_START;
		const auto group_sz = window_size_to_groupbox_size(848.f, 588.f) - vec2{0.f, 36.f};

		const auto area = MAKE("visuals>tab>misc", layout, vec2{ 0.f, 36.f }, vec2{ 800.f, 446.f }, s_justify);
		area->is_visible = false;
		area->make_not_clip();
		area->add(make_groupbox(XOR("visuals>misc>local"), XOR("LOCAL"), group_sz, visuals_misc_local));
		area->add(make_groupbox(XOR("visuals>misc>various"), XOR("OTHER"), group_sz, visuals_misc_various));
		area->add(make_groupbox(XOR("visuals>misc>world"), XOR("WORLD"), group_sz, visuals_misc_world));

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
			MAKE("visuals>misc>local>fov changer>settings", settings, [](auto e) {
				ADD("Aspect ratio", "visuals>misc>local>fov>aspect", slider<float>, 0.f, 200.f, cfg.misc.aspect, "%.0f%%");
				ADD("Fov", "visuals>misc>local>fov changer>settings>fov", slider<float>, 60.f, 140.f, cfg.misc.fov, XOR("%.0f"));
				ADD("Remove zoom", "visuals>misc>local>fov changer>settings>remove zoom", slider<float>, 0.f, 100.f, cfg.misc.fov_scope, XOR("%.0f%%"));
				ADD("Force second zoom", "visuals>misc>local>fov>second zoom", checkbox, cfg.misc.force_second_zoom);
				ADD("Force viewmodel in scope", "visuals>misc>local>fov>viewmodel scope", checkbox, cfg.misc.force_viewmodel_scope); }));

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
			c->add(
			{MAKE("visuals>misc>local>remove scope>none", selectable, XOR("None"), cfg_t::noscope_none), MAKE(
				 "visuals>misc>local>remove scope>static", selectable, XOR("Static"), cfg_t::noscope_static),
			 MAKE("visuals>misc>local>remove scope>dynamic", selectable, XOR("Dynamic"), cfg_t::noscope_dynamic),
			 MAKE("visuals>misc>local>remove scope>gradient static", selectable, XOR("Gradient static"), cfg_t::noscope_gradient_static), MAKE(
				 "visuals>misc>local>remove scope>gradient dynamic", selectable, XOR("Gradient dynamic"), cfg_t::noscope_gradient_dynamic),});

			c->callback = [](bits &v)
			{
				FIND_SAFE(rss, "visuals>misc>local>remove scope>settings");
				rss->set_visible(v.test(cfg_t::noscope_gradient_static) || v.test(cfg_t::noscope_gradient_dynamic));
			};

			ADD_INLINE(
				"Remove scope", c,
				MAKE("visuals>misc>local>remove scope>settings", settings, [](auto e) { ADD("Line color", "visuals>misc>local>remove scope>settings>line color",
					color_picker, cfg. misc.scope_line_color); ADD("Line size", "visuals>misc>local>remove scope>settings>line size", slider<float>, 5.f, 100.f, cfg.misc.
					scope_line_size, XOR("%.0f%%")); ADD("Invert", "visuals>misc>local>remove scope>settings>invert", checkbox, cfg.misc. scope_invert); })->
				make_invisible());
		}

		ADD("Penetration crosshair", "visuals>misc>local>penetration crosshair", checkbox, cfg.visuals.autowall_crosshair);
		ADD("Force crosshair", "visuals>misc>local>force crosshair", checkbox, cfg.misc.force_crosshair);

		{
			const auto c = MAKE("visuals>misc>local>spread", combo_box, cfg.visuals.spread_circle_type);
			c->add(
			{MAKE("visuals>misc>local>spread>none", selectable, XOR("None"), cfg_t::spread_circle_off), MAKE(
				 "visuals>misc>local>spread>gradient", selectable, XOR("Gradient"), cfg_t::spread_circle_gradient),});

			ADD_INLINE(
				"Spread", c, MAKE("visuals>misc>local>spread>color 1", color_picker, cfg.visuals.spread_circle_color1),
				MAKE("visuals>misc>local>spread>color 2", color_picker, cfg.visuals.spread_circle_color2));
		}
		//VIRTUALIZER_TIGER_WHITE_END;
	}

	void visuals_misc_various(std::shared_ptr<layout> &e)
	{
		ADD("Texture transparency", "visuals>misc>other>texture transparency", slider<float>, 0.f, 100.f, cfg.misc.texture_transparency, "%.0f%%");
		ADD("Prop transparency", "visuals>misc>other>prop transparency", slider<float>, 0.f, 100.f, cfg.visuals.prop_transparency, "%.0f%%");
		{
			const auto tracer = MAKE("visuals>misc>other>tracers", combo_box, cfg.misc.tracer_targets);
			tracer->allow_multiple = true;
			const auto local = MAKE("visuals>misc>other>tracers>local", selectable, "Local", 0);
			const auto enemy = MAKE("visuals>misc>other>tracers>enemy", selectable, "Enemy", 1);
			const auto team = MAKE("visuals>misc>other>tracers>team", selectable, "Teammates", 2);
			local->swatch = MAKE("visuals>misc>other>tracers>local>color", color_picker, cfg.misc.tracer_colors[0]);
			enemy->swatch = MAKE("visuals>misc>other>tracers>enemy>color", color_picker, cfg.misc.tracer_colors[1]);
			team->swatch = MAKE("visuals>misc>other>tracers>team>color", color_picker, cfg.misc.tracer_colors[2]);
			tracer->add({local, enemy, team});
			const auto gear = MAKE("visuals>misc>other>tracers>settings", settings, [](auto e) {
				const std::array<const char *, 3> names{"Local", "Enemy", "Teammates"};
				for (int i = 0; i < 3; ++i) {
					const auto key = std::string("visuals>misc>other>tracers>settings>") + std::to_string(i);
					auto style = MAKE_RUNTIME(key + ">style", combo_box, cfg.misc.tracer_style[i], vec2{}, vec2{70.f, 20.f});
					style->add({MAKE_RUNTIME(key + ">laser", selectable, "Laser", 0), MAKE_RUNTIME(key + ">line", selectable, "Line", 1)});
					const auto row = make_inlined(hash(key + ">row"), [&](std::shared_ptr<layout> &r) {
						r->add(std::make_shared<label>(control_id{hash(key + ">label")}, names[i]));
						r->add(style);
					});
					row->is_visible = cfg.misc.tracer_targets->test(i);
					e->add(row);
				}
			});
			gear->is_visible = cfg.misc.tracer_targets->test(0) || cfg.misc.tracer_targets->test(1) || cfg.misc.tracer_targets->test(2);
			tracer->callback = [weak = std::weak_ptr<settings>(gear)](bits &v) {
				if (const auto g = weak.lock()) {
					const bool show = v.test(0) || v.test(1) || v.test(2);
					if (g->is_visible != show) g->set_visible(show);
				}
				for (int i = 0; i < 3; ++i) {
					const auto id = hash(std::string("visuals>misc>other>tracers>settings>") + std::to_string(i) + ">row");
					if (const auto row = ctx->find(id); row && row->is_visible != v.test(i)) row->set_visible(v.test(i));
				}
			};
			ADD_INLINE("Bullet tracers", tracer, gear);
		}
		{
			const auto c = MAKE("visuals>misc>other>hitmarker", combo_box, cfg.misc.hitmarker);
			c->allow_multiple = true;
			const auto world = MAKE("visuals>misc>other>hitmarker>world", selectable, "World", cfg_t::hitmarker_world);
			const auto screen = MAKE("visuals>misc>other>hitmarker>screen", selectable, "Screen", cfg_t::hitmarker_cross);
			const auto kibit = MAKE("visuals>misc>other>hitmarker>kibit", selectable, "Kibit", cfg_t::hitmarker_kibit);
			world->swatch = MAKE("visuals>misc>other>hitmarker>world>color", color_picker, cfg.misc.hitmarker_world_color);
			screen->swatch = MAKE("visuals>misc>other>hitmarker>screen>color", color_picker, cfg.misc.hitmarker_screen_color);
			kibit->swatch = MAKE("visuals>misc>other>hitmarker>kibit>color", color_picker, cfg.misc.hitmarker_kibit_color);
			c->add({world, screen, kibit});
			const auto gear = MAKE("visuals>misc>other>hitmarker>settings", settings, [](auto e) {
				const std::array<const char *, 3> names{"World", "Screen", "Kibit"};
				const std::array<int, 3> indices{cfg_t::hitmarker_world, cfg_t::hitmarker_cross, cfg_t::hitmarker_kibit};
				for (int i = 0; i < 3; ++i) {
					const auto key = std::string("visuals>misc>other>hitmarker>settings>") + std::to_string(indices[i]);
					const auto row = make_control(names[i], MAKE_RUNTIME(key + ">duration", slider<float>, 0.f, 10.f, cfg.misc.hitmarker_duration[indices[i]], "%.1f s", .1f));
					row->is_visible = cfg.misc.hitmarker->test(indices[i]);
					e->add(row);
				}
			});
			gear->is_visible = cfg.misc.hitmarker->test(cfg_t::hitmarker_world) || cfg.misc.hitmarker->test(cfg_t::hitmarker_cross) || cfg.misc.hitmarker->test(cfg_t::hitmarker_kibit);
			c->callback = [weak = std::weak_ptr<settings>(gear)](bits &v) {
				const std::array<int, 3> indices{cfg_t::hitmarker_world, cfg_t::hitmarker_cross, cfg_t::hitmarker_kibit};
				const bool show = v.test(indices[0]) || v.test(indices[1]) || v.test(indices[2]);
				if (const auto g = weak.lock(); g && g->is_visible != show) g->set_visible(show);
				for (const auto i : indices) {
					const auto id = hash(std::string("visuals>misc>other>hitmarker>settings>") + std::to_string(i) + ">duration");
					if (const auto slider = ctx->find(id))
						if (const auto row = slider->get_parent(); row && row->is_visible != v.test(i)) row->set_visible(v.test(i));
				}
			};
			ADD_INLINE("Hitmarker", c, gear);
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
				c->add(item);
			}
			ADD_INLINE("Removals", c);
		}
	}

	void visuals_misc_world(std::shared_ptr<layout> &e)
	{
		auto &w = cfg.visuals.world_effects;
		ADD_INLINE("Nightmode", MAKE("visuals>world>nightmode", checkbox, w.nightmode), MAKE("visuals>world>nightmode>color", color_picker, w.nightmode_color));
		ADD_INLINE("Fullbright", MAKE("visuals>world>fullbright", checkbox, w.fullbright), MAKE("visuals>world>fullbright>color", color_picker, w.fullbright_color));
		ADD_INLINE("Sunlight", MAKE("visuals>world>sunlight", checkbox, w.sunlight), MAKE("visuals>world>sunlight>color", color_picker, w.sunlight_color));
		ADD_INLINE("Sky", MAKE("visuals>world>sky", checkbox, w.sky), MAKE("visuals>world>sky>color", color_picker, w.sky_color));
		ADD_INLINE("Fog", MAKE("visuals>world>fog", checkbox, w.fog), MAKE("visuals>world>fog>color", color_picker, w.fog_color));
		ADD_INLINE("Vignette", MAKE("visuals>world>vignette", checkbox, w.vignette), MAKE("visuals>world>vignette>color", color_picker, w.vignette_color));
		ADD_INLINE("Exposure", MAKE("visuals>world>exposure", checkbox, w.exposure_enabled), MAKE("visuals>world>exposure>settings", settings, [](auto e) {
			ADD("Exposure", "visuals>world>exposure>amount", slider<float>, 0.f, 100.f, cfg.visuals.world_effects.exposure, "%.0f%%");
		}));
		ADD_INLINE("Bloom", MAKE("visuals>world>bloom", checkbox, w.bloom), MAKE("visuals>world>bloom>settings", settings, [](auto e) {
			auto &b = cfg.visuals.world_effects;
			ADD("Strength", "visuals>world>bloom>strength", slider<float>, 0.f, 100.f, b.bloom_strength, "%.0f%%");
			ADD("Screen strength", "visuals>world>bloom>screen", slider<float>, 0.f, 100.f, b.bloom_screen_strength, "%.0f%%");
			ADD("Blur strength", "visuals>world>bloom>blur", slider<float>, 0.f, 100.f, b.bloom_blur_strength, "%.0f%%");
			ADD("Threshold", "visuals>world>bloom>threshold", slider<float>, 0.f, 100.f, b.bloom_threshold, "%.0f%%");
			ADD("Threshold width", "visuals>world>bloom>threshold width", slider<float>, 0.f, 100.f, b.bloom_threshold_width, "%.0f%%");
			for (int i = 0; i < 5; ++i) {
				const auto key = std::string("visuals>world>bloom>weight>") + std::to_string(i);
				const auto label_text = std::string("Blur weight ") + std::to_string(i + 1);
				auto weight_slider = MAKE_RUNTIME(key, slider<float>, 0.f, 100.f, b.bloom_weights[i], "%.0f%%", 1.f, vec2{70.f, 14.f});
				auto picker = MAKE_RUNTIME(key + ">color", color_picker, b.bloom_weight_colors[i]);
				e->add(make_inlined(hash(key + ">row"), [&](std::shared_ptr<layout> &row) {
					row->add(std::make_shared<label>(control_id{hash(key + ">label")}, label_text)); row->add(weight_slider); row->add(picker);
				}));
			}
		}));
		ADD_INLINE("Local contrast", MAKE("visuals>world>local contrast", checkbox, w.local_contrast), MAKE("visuals>world>local contrast>settings", settings, [](auto e) {
			auto &v = cfg.visuals.world_effects;
			ADD("Strength", "visuals>world>local contrast>strength", slider<float>, 0.f, 100.f, v.contrast_strength, "%.0f%%");
			ADD("Edge strength", "visuals>world>local contrast>edge", slider<float>, 0.f, 100.f, v.contrast_edge_strength, "%.0f%%");
			ADD("Vignette start", "visuals>world>local contrast>start", slider<float>, 0.f, 100.f, v.contrast_vignette_start, "%.0f%%");
			ADD("Vignette end", "visuals>world>local contrast>end", slider<float>, 0.f, 100.f, v.contrast_vignette_end, "%.0f%%");
			ADD("Vignette blur", "visuals>world>local contrast>blur", slider<float>, 0.f, 100.f, v.contrast_vignette_blur, "%.0f%%");
		}));
		ADD_INLINE("Color correction", MAKE("visuals>world>correction", checkbox, w.color_correction), MAKE("visuals>world>correction>settings", settings, [](auto e) {
			auto &v = cfg.visuals.world_effects;
			const auto curve = MAKE("visuals>world>correction>curve", combo_box, v.tone_curve);
			curve->add({MAKE("visuals>world>correction>curve>linear", selectable, "Linear", 0), MAKE("visuals>world>correction>curve>filmic", selectable, "Filmic", 1)});
			ADD_INLINE("Tone curve", curve);
			ADD("Contrast", "visuals>world>correction>contrast", slider<float>, -100.f, 100.f, v.correction_contrast, "%+.0f%%");
			ADD("Highlights", "visuals>world>correction>highlights", slider<float>, -100.f, 100.f, v.highlights, "%+.0f%%");
			ADD("Shadows", "visuals>world>correction>shadows", slider<float>, -100.f, 100.f, v.shadows, "%+.0f%%");
			ADD("Saturation", "visuals>world>correction>saturation", slider<float>, -100.f, 100.f, v.saturation, "%+.0f%%");
			ADD("Vibrance", "visuals>world>correction>vibrance", slider<float>, -100.f, 100.f, v.vibrance, "%+.0f%%");
			ADD("Temperature", "visuals>world>correction>temperature", slider<float>, -100.f, 100.f, v.temperature, "%+.0f%%");
			ADD("Tint", "visuals>world>correction>tint", slider<float>, -100.f, 100.f, v.tint, "%+.0f%%");
			ADD("Grayscale", "visuals>world>correction>grayscale", checkbox, v.grayscale);
		}));
		ADD_INLINE("Depth of field", MAKE("visuals>world>dof", checkbox, w.depth_of_field), MAKE("visuals>world>dof>settings", settings, [](auto e) {
			auto &v = cfg.visuals.world_effects;
			const auto mode = MAKE("visuals>world>dof>mode", combo_box, v.dof_mode);
			mode->add({MAKE("visuals>world>dof>mode>manual", selectable, "Manual", 0), MAKE("visuals>world>dof>mode>crosshair", selectable, "Crosshair", 1)});
			ADD_INLINE("Mode", mode);
			const auto quality = MAKE("visuals>world>dof>quality", combo_box, v.dof_quality);
			quality->add(MAKE("visuals>world>dof>quality>balanced", selectable, "Balanced", 0));
			ADD_INLINE("Quality", quality);
			const auto distance = make_control("Focus distance", MAKE("visuals>world>dof>distance", slider<float>, 0.f, 100.f, v.focus_distance, "%.0f%%"));
			const auto speed = make_control("Focus speed", MAKE("visuals>world>dof>speed", slider<float>, 0.f, 20.f, v.focus_speed, "%.0fx"));
			distance->is_visible = !v.dof_mode->test(1);
			speed->is_visible = v.dof_mode->test(1);
			mode->callback = [distance = std::weak_ptr<layout>(distance), speed = std::weak_ptr<layout>(speed)](bits &value) {
				if (const auto row = distance.lock()) row->set_visible(!value.test(1));
				if (const auto row = speed.lock()) row->set_visible(value.test(1));
			};
			e->add(distance); e->add(speed);
			ADD("Focus range", "visuals>world>dof>range", slider<float>, 0.f, 100.f, v.focus_range, "%.0f%%");
		}));
		{
			const auto weather = MAKE("visuals>world>weather", combo_box, w.weather);
			const auto none = MAKE("visuals>world>weather>none", selectable, "None", 0);
			const auto thunder = MAKE("visuals>world>weather>thunder", selectable, "Thunder", 1);
			const auto rain = MAKE("visuals>world>weather>rain", selectable, "Rain", 2);
			const auto snow = MAKE("visuals>world>weather>snow", selectable, "Snow", 3);
			thunder->swatch = MAKE("visuals>world>weather>thunder>color", color_picker, w.weather_colors[1]);
			rain->swatch = MAKE("visuals>world>weather>rain>color", color_picker, w.weather_colors[2]);
			snow->swatch = MAKE("visuals>world>weather>snow>color", color_picker, w.weather_colors[3]);
			weather->add({none, thunder, rain, snow});
			ADD_INLINE("Weather", weather);
		}
	}

}
