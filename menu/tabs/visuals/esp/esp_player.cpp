#include "../visuals.hpp"

namespace menu::group
{
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
			picker->gradient_state = &cfg.visuals.esp.health_gradient;
			ADD_INLINE("Health", MAKE("visuals>esp>player>health", checkbox, cfg.visuals.esp.health), picker);
		}


		for(int index=0;index<2;++index) {
            auto& color=index==0 ? cfg.visuals.esp.armor_color : cfg.visuals.esp.ammo_color;
            auto& end=index==0 ? cfg.visuals.esp.armor_color_2 : cfg.visuals.esp.ammo_color_2;
            auto& enabled=index==0 ? cfg.visuals.esp.armor : cfg.visuals.esp.ammo;
            auto& gradient=index==0 ? cfg.visuals.esp.armor_gradient : cfg.visuals.esp.ammo_gradient;
            const std::string key=index==0 ? "visuals>esp>player>armor" : "visuals>esp>player>ammo";
            auto picker=MAKE_RUNTIME(key+">color",color_picker,color,false);picker->gradient_color=&end;picker->gradient_state=&gradient;
            e->add(make_inlined(hash(key+">row"),[&](std::shared_ptr<layout>& row){
                row->add(std::make_shared<label>(control_id{hash(key+">label")},index==0 ? "Armor" : "Ammo"));
                row->add(MAKE_RUNTIME(key,checkbox,enabled));row->add(picker);
            }));
        }



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

}
