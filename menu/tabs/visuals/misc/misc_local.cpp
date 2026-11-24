#include "../visuals.hpp"

namespace menu::group
{
	void visuals_misc_local(std::shared_ptr<layout> &e)
	{
		//VIRTUALIZER_TIGER_WHITE_START;
		ADD_INLINE(
			"Thirdperson", MAKE("visuals>misc>local>thirdperson", checkbox, cfg.misc.thirdperson),
			MAKE("visuals>misc>local>thirdperson>settings", settings, [](auto e) { ADD("Force when dead", "visuals>misc>local>thirdperson>settings>force when dead",
					checkbox, cfg. misc.thirdperson_dead); ADD("Disable on grenade", "visuals>misc>local>thirdperson>settings>disable on grenade", checkbox, cfg.misc.
					thirdperson_grenade); 
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
				rss->set_visible(v.test(cfg_t::noscope_static) || v.test(cfg_t::noscope_dynamic) || v.test(cfg_t::noscope_gradient_static) || v.test(cfg_t::noscope_gradient_dynamic));
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

}
