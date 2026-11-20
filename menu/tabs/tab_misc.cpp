#include "tabs.hpp"

#include "../config.hpp"
#include "../controls/macros.h"

using namespace evo::gui;
using namespace evo::gui::helpers;
using namespace evo::ren;

namespace menu
{
	float dpi_scale(const bits &b, float display_height)
	{
		if (b.test(cfg_t::dpi_50)) return .5f;
		if (b.test(cfg_t::dpi_75)) return .75f;
		if (b.test(cfg_t::dpi_100)) return 1.f;
		if (b.test(cfg_t::dpi_125)) return 1.25f;
		if (b.test(cfg_t::dpi_150)) return 1.5f;
		if (b.test(cfg_t::dpi_200)) return 2.f;
		return display_height > 0.f ? std::clamp(display_height / 1080.f, .5f, 2.f) : 1.f;
	}

}

namespace menu::group
{
	static std::shared_ptr<combo_box> make_multi(const std::string &id, value_param<bits> &value, const std::vector<std::string> &items)
	{
		auto c = MAKE_RUNTIME(id, combo_box, value);
		c->allow_multiple = true;
		for (size_t i = 0; i < items.size(); ++i)
			c->add(MAKE_RUNTIME(id + ">" + std::to_string(i), selectable, items[i], static_cast<int>(i)));
		return c;
	}

	static std::shared_ptr<combo_box> make_sound(const std::string &id, value_param<bits> &value)
	{
		auto c = MAKE_RUNTIME(id, combo_box, value);
		const std::vector<std::string> names{"None", "Bell", "CoD", "Fatality", "Arena switch", "Bepis", "Phonk", "Sparkle", "Cock"};
		for (size_t i = 0; i < names.size(); ++i)
			c->add(MAKE_RUNTIME(id + ">" + std::to_string(i), selectable, names[i], static_cast<int>(i)));
		return c;
	}

	void misc_general(std::shared_ptr<layout> &e)
	{
		const auto logs = make_multi("misc>general>logs", cfg.misc.log_types, {"Hit logs", "Miss logs", "Buy logs", "Hurt logs", "Naded logs"});
		const auto log_settings = MAKE("misc>general>logs>settings", settings, [](auto e) {
			const std::vector<std::string> details{"Damage dealt", "Damage aimed", "Hitchance", "Shot", "Backtrack", "Safe point"};
			auto hits = make_multi("misc>general>logs>hit details", cfg.misc.hit_log_details, details);
			auto miss_details = details; miss_details.emplace_back("Miss reason");
			auto misses = make_multi("misc>general>logs>miss details", cfg.misc.miss_log_details, miss_details);
			auto buys = make_multi("misc>general>logs>buy targets", cfg.misc.buy_log_targets, {"Teammates", "Enemy", "Local", "All"});
			e->add(make_control("Hit logs", hits));
			e->add(make_control("Miss logs", misses));
			e->add(make_control("Buy logs", buys));
			const std::vector<std::string> names{"Hit color", "Miss color", "Buy color", "Hurt color", "Naded color"};
			for (size_t i = 0; i < names.size(); ++i) {
				const auto key = std::string("misc>general>logs>color>") + std::to_string(i);
				e->add(make_control(names[i], MAKE_RUNTIME(key, color_picker, cfg.misc.log_colors[i])));
			}
			ADD("Console logs", "misc>general>logs>console", checkbox, cfg.misc.console_logs);
		});
		ADD_INLINE("Logs", logs, log_settings);
		const auto sound = make_sound("misc>general>hitsound", cfg.misc.hitsound_select);
		const auto sound_settings = MAKE("misc>general>hitsound>settings", settings, [](auto e) {
			ADD("Hit sound", "misc>general>hitsound>hit enabled", checkbox, cfg.misc.hit_sound_enabled);
			ADD("Kill sound", "misc>general>hitsound>kill enabled", checkbox, cfg.misc.kill_sound_enabled);
			e->add(make_control("Kill sound", make_sound("misc>general>hitsound>kill select", cfg.misc.kill_sound_select)));
		});
		ADD_INLINE("Hitsound", sound, sound_settings);
		{
			const auto c = MAKE("misc>general>indicators", combo_box, cfg.aa.indicators);
			c->allow_multiple = true;
			c->add(
			{MAKE("misc>general>indicators>manual anti-aim", selectable, XOR("Manual anti-aim"), cfg_t::indicators_manual), MAKE(
				 "misc>general>indicators>freestanding", selectable, XOR("Freestanding"), cfg_t::indicators_fs),
			 MAKE("misc>general>indicators>lc", selectable, XOR("LC"), cfg_t::indicators_lc),
			 MAKE("misc>general>indicators>double tap", selectable, XOR("Double tap"), cfg_t::indicators_dt),
			 MAKE("misc>general>indicators>hide shot", selectable, XOR("Hide shot"), cfg_t::indicators_st),
			 MAKE("misc>general>indicators>damage override", selectable, XOR("Damage override"), cfg_t::indicators_do),
			 MAKE("misc>general>indicators>force safety", selectable, XOR("Force safety"), cfg_t::indicators_safe),
			 MAKE("misc>general>indicators>headshot only", selectable, XOR("Headshot only"), cfg_t::indicators_head),
			 MAKE("misc>general>indicators>bodyaim", selectable, XOR("Bodyaim"), cfg_t::indicators_baim), MAKE(
				 "misc>general>indicators>ping", selectable, XOR("Ping"), cfg_t::indicators_ping),});

			ADD_INLINE("Indicators", c);
		}

		ADD("Clantag", "misc>general>clan tag", checkbox, cfg.misc.clantag);
		ADD("Reveal enemy chat", "misc>general>enemy chat", checkbox, cfg.misc.reveal_enemy_chat);
		ADD_INLINE("Ping spike", MAKE("misc>general>ping spike", checkbox, cfg.misc.ping_spike_enabled), MAKE("misc>general>ping spike>settings", settings, [](auto e) {
			ADD("Amount", "misc>general>ping spike>amount", slider<float>, 0.f, 200.f, cfg.aim.ping_spike_amt, "%.0f ms");
		}));
	}

	void misc_other(std::shared_ptr<layout> &e)
	{
		ADD("Bypass SV Pure", "misc>other>bypass sv_pure", checkbox, cfg.misc.sv_pure);
		ADD("Bypass SV Cheats", "misc>other>bypass sv_cheats", checkbox, cfg.misc.sv_cheats);
		ADD("Console filter", "misc>other>console filter", checkbox, cfg.misc.console_filter);
		ADD("Auto-accept", "misc>other>auto-accept", checkbox, cfg.misc.autoaccept);
		ADD("Radar", "misc>other>radar", checkbox, cfg.misc.radar);
		{
			const auto c = MAKE("misc>other>dpi scale", combo_box, cfg.misc.dpi_scale);
			c->add(
			{MAKE("misc>other>dpi scale>automatic", selectable, XOR("Automatic"), cfg_t::dpi_automatic), MAKE(
				 "misc>other>dpi scale>50%", selectable, XOR("50%"), cfg_t::dpi_50), MAKE("misc>other>dpi scale>75%", selectable, XOR("75%"), cfg_t::dpi_75),
			 MAKE("misc>other>dpi scale>100%", selectable, XOR("100%"), cfg_t::dpi_100), MAKE("misc>other>dpi scale>125%", selectable, XOR("125%"), cfg_t::dpi_125),
			 MAKE("misc>other>dpi scale>150%", selectable, XOR("150%"), cfg_t::dpi_150),
			 MAKE("misc>other>dpi scale>200%", selectable, XOR("200%"), cfg_t::dpi_200),});


			ADD_INLINE("DPI scale", c);
		}
	}

	void misc_movement(std::shared_ptr<layout> &e)
	{
		const auto c = MAKE("misc>movement>autostrafer", combo_box, cfg.misc.autostrafe);
		c->allow_multiple = true;
		c->add({MAKE("misc>movement>autostrafer>viewangles", selectable, "Viewangles", cfg_t::autostrafe_viewangles),
			MAKE("misc>movement>autostrafer>movement keys", selectable, "Movement keys", cfg_t::autostrafe_movement_keys),
			MAKE("misc>movement>autostrafer>easy strafe", selectable, "Easy strafe", cfg_t::autostrafe_easy_strafe),
			MAKE("misc>movement>autostrafer>jump release", selectable, "Jump release", cfg_t::autostrafe_jump_release)});
		ADD_INLINE("Air strafe", c, MAKE("misc>movement>autostrafer>settings", settings, [](auto e) {
			ADD("Smoothing", "misc>movement>smoothing", slider<float>, 0.f, 100.f, cfg.misc.autostrafe_smoothing, "%.0f");
			ADD("Avoid walls", "misc>movement>avoid walls", checkbox, cfg.misc.avoid_walls);
		}));
		ADD("Bhop", "misc>movement>bhop", checkbox, cfg.misc.bhop);
        const auto no_fall = MAKE("misc>movement>no fall damage", checkbox, cfg.misc.no_fall_damage);
        no_fall->tooltip = "Experimental landing assist: times duck/jump around a predicted landing; does not guarantee zero server fall damage.";
        e->add(make_control("No fall damage", no_fall));
		ADD("Instant duck", "misc>movement>instant unduck", checkbox, cfg.misc.instant_unduck);
		{ const auto control = MAKE("misc>movement>edge jump", checkbox, cfg.misc.edge_jump); control->tooltip = "Experimental isolated movement forecast; not full engine prediction."; e->add(make_control("Edge jump", control)); }
		{ const auto control = MAKE("misc>movement>edge stop", checkbox, cfg.misc.edge_stop); control->tooltip = "Experimental counter-movement when the forecast leaves ground."; e->add(make_control("Edge stop", control)); }
		ADD("Quick stop", "misc>movement>standalone quick stop", checkbox, cfg.legit.quick_stop);
	}

	void misc_movement_extra(std::shared_ptr<layout> &e)
	{
		{ const auto control = MAKE("misc>movement>extra>edge bugs", checkbox, cfg.misc.edge_bug); control->tooltip = "Experimental bounded trajectory search; Source-style simulation, not engine prediction."; e->add(make_control("Edge bugs", control)); }
		{ const auto control = MAKE("misc>movement>extra>pixel surf", checkbox, cfg.misc.pixel_surf); control->tooltip = "Experimental trajectory search requiring repeated unsupported floor contacts."; e->add(make_control("Pixel surf", control)); }
		{ const auto control = MAKE("misc>movement>extra>long jump", checkbox, cfg.misc.long_jump); control->tooltip = "Jump-duck assist after takeoff; no artificial speed boost. Paused while edge/pixel/no-fall assists are enabled."; e->add(make_control("Long jump", control)); }
		{ const auto control = MAKE("misc>movement>extra>ladder jump", checkbox, cfg.misc.ladder_jump); control->tooltip = "Dismount assist near the ladder top while climbing forward; the server still enforces ladder-jump cooldown."; e->add(make_control("Ladder jump", control)); }

	}

	void misc_buybot(std::shared_ptr<layout> &e)
	{
		//VIRTUALIZER_TIGER_WHITE_START;
		{
			const auto c = MAKE("misc>buybot>primary", combo_box, cfg.misc.buybot_primary);
			c->legacy_mode = true;
			c->add(
			{MAKE("misc>buybot>primary>none", selectable, XOR("None"), cfg_t::buybot_primary_none), MAKE(
				 "misc>buybot>primary>ak47 / m4a1", selectable, XOR("AK47 / M4A1"), cfg_t::buybot_primary_ak47),
			 MAKE("misc>buybot>primary>sg-556", selectable, XOR("SG-556"), cfg_t::buybot_primary_sg556),
			 MAKE("misc>buybot>primary>awp", selectable, XOR("AWP"), cfg_t::buybot_primary_awp),
			 MAKE("misc>buybot>primary>ssg-08", selectable, XOR("SSG-08"), cfg_t::buybot_primary_ssg08),
			 MAKE("misc>buybot>primary>scar-20 / g3sg1", selectable, XOR("SCAR-20 / G3SG1"), cfg_t::buybot_primary_scar20), MAKE(
				 "misc>buybot>primary>negev", selectable, XOR("Negev"), cfg_t::buybot_primary_negev),});

			ADD_INLINE("Primary", c);
		}

		{
			const auto c = MAKE("misc>buybot>secondary", combo_box, cfg.misc.buybot_pistol);
			c->legacy_mode = true;
			c->add(
			{MAKE("misc>buybot>secondary>none", selectable, XOR("None"), cfg_t::buybot_pistol_none), MAKE(
				 "misc>buybot>secondary>deagle / revolver", selectable, XOR("Desert Eagle"), cfg_t::buybot_pistol_deagle),
			 MAKE("misc>buybot>secondary>dual berettas", selectable, XOR("Dual Berettas"), cfg_t::buybot_pistol_elites),
			 MAKE("misc>buybot>secondary>tec-9 / five-seven", selectable, XOR("Tec-9 / Five-SeveN"), cfg_t::buybot_pistol_tec9), MAKE(
				 "misc>buybot>secondary>p250", selectable, XOR("P250"), cfg_t::buybot_pistol_p250),
             MAKE("misc>buybot>secondary>cz75a", selectable, XOR("CZ75-Auto"), cfg_t::buybot_pistol_cz75a),
             MAKE("misc>buybot>secondary>revolver", selectable, XOR("R8 Revolver"), cfg_t::buybot_pistol_revolver),});

			ADD_INLINE("Secondary", c);
		}

		{
			const auto c = MAKE("misc>buybot>armor", combo_box, cfg.misc.buybot_kevlar_helmet);
			c->legacy_mode = true;
			c->add(
			{MAKE("misc>buybot>armor>none", selectable, XOR("None"), cfg_t::buybot_kevlar_none), MAKE(
				 "misc>buybot>armor>kevlar", selectable, XOR("Kevlar"), cfg_t::buybot_kevlar_k), MAKE(
				 "misc>buybot>armor>kevlar + helmet", selectable, XOR("Kevlar + Helmet"), cfg_t::buybot_kevlar_kh),});

			ADD_INLINE("Armor", c);
		}

		ADD("Defuser", "misc>buybot>defuser", checkbox, cfg.misc.buybot_defuser);
		ADD("Taser", "misc>buybot>taser", checkbox, cfg.misc.buybot_taser);
		ADD("Grenades", "misc>buybot>grenades", checkbox, cfg.misc.buybot_grenades);
		//VIRTUALIZER_TIGER_WHITE_END;
	}
}
