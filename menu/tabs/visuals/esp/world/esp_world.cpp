#include "../../visuals.hpp"

namespace menu::group
{
	void visuals_esp_world(std::shared_ptr<layout> &e, int category)
	{
		auto &world = cfg.visuals.esp.world;
		const std::string key = category == 0 ? "visuals>esp>weapon>" : category == 1 ? "visuals>esp>grenades>" : "visuals>esp>c4>";
		auto &enabled = category == 0 ? world.weapon_enabled : category == 1 ? world.grenade_enabled : world.c4_enabled;
		auto &icon_color = category == 0 ? world.weapon_color : category == 1 ? world.grenade_color : world.c4_color;

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
			add_inline("icons", icon_label, {MAKE_RUNTIME(key + "icons", checkbox, category == 0 ? world.weapon_icons : world.grenade_icons), MAKE_RUNTIME(key + "icons>color", color_picker, icon_color)});
		else {
			auto options = MAKE_RUNTIME(key + "options", combo_box, world.c4_options, vec2{}, vec2{80.f, 20.f});
			options->allow_multiple = true;
			options->add({MAKE_RUNTIME(key + "options>timer", selectable, "Timer", 0),
				MAKE_RUNTIME(key + "options>damage", selectable, "Damage", 1),
				MAKE_RUNTIME(key + "options>defuse", selectable, "Defusing", 2)});
			add_inline("options", "Options", {options, MAKE_RUNTIME(key + "timer>color", color_picker, world.c4_timer_color)});
            auto c4_icons=MAKE_RUNTIME(key + "icons", combo_box, world.c4_options, vec2{}, vec2{80.f,20.f});
            c4_icons->allow_multiple=true;
            c4_icons->add(MAKE_RUNTIME(key + "icons>enabled", selectable, "Enabled", 3));
            add_inline("icons", "C4 icons", {c4_icons,MAKE_RUNTIME(key + "icons>color",color_picker,world.c4_color)});
		}
		auto material = MAKE_RUNTIME(key + "chams", combo_box, visual.material, vec2{}, vec2{80.f, 20.f});
		insert_chams(key + "chams>", material);
		add_inline("chams", "Chams", {material, MAKE_RUNTIME(key + "chams>color", color_picker, visual.material_color)});
		auto& glow_enabled=category==0 ? world.weapon_glow : category==1 ? world.grenade_glow : world.c4_glow;
        add_inline("glow", "Glow", {MAKE_RUNTIME(key+"glow>enabled",checkbox,glow_enabled),MAKE_RUNTIME(key+"glow>color",color_picker,visual.glow_color)});
	}

}
