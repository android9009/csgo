#include "../visuals.hpp"

namespace menu::group
{
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
					(v.test(cfg_t::cham_glow)));
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
					(v.test(cfg_t::cham_glow)));
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
					(v.test(cfg_t::cham_glow)));
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

}
