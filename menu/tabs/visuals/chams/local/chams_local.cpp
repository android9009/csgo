#include "../../visuals.hpp"

namespace menu::group
{
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
					(v.test(cfg_t::cham_glow)));
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
					(v.test(cfg_t::cham_glow)));
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
					(v.test(cfg_t::cham_glow)));
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
					(v.test(cfg_t::cham_glow)));
			};

			ADD_INLINE(
				"Attachments", c,
				MAKE("visuals>chams>local>attachments>settings", settings, [](auto e) { ADD("Primary color", "visuals>chams>local>attachments>settings>primary color",
					color_picker, cfg.visuals.chams.local_att.col1); ADD("Secondary color", "visuals>chams>local>attachments>settings>secondary color", color_picker, cfg.
					visuals.chams.local_att.col2); })->make_invisible());
		}
		//VIRTUALIZER_TIGER_WHITE_END;
	}

}
