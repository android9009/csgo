#include "../../visuals.hpp"

namespace menu::group
{
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
					(v.test(cfg_t::cham_glow)));
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
				secondary->get_parent()->set_visible(enabled && (v.test(cfg_t::cham_glow)));
			};
			ADD_INLINE("Backtrack", c, MAKE("visuals>chams>enemy>history>settings", settings, [](auto e) {
				ADD("Primary color", "visuals>chams>enemy>history>settings>primary color", color_picker, cfg.visuals.chams.enemy_backtrack.col1, true, vec2{}, true);
				const auto secondary = make_control("Secondary color", MAKE("visuals>chams>enemy>history>settings>secondary color", color_picker, cfg.visuals.chams.enemy_backtrack.col2, true, vec2{}, true));
				const auto &v = *cfg.visuals.chams.enemy_backtrack.mat;
				secondary->set_visible(!v.none() && !v.test(cfg_t::cham_none) && (v.test(cfg_t::cham_glow)));
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
					(v.test(cfg_t::cham_glow)));
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
					(v.test(cfg_t::cham_glow)));
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
					(v.test(cfg_t::cham_glow)));
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

}
