#include "tabs.hpp"
#include "../config.hpp"
#include "../controls/macros.h"
#include <ren/renderer.h>
#include <algorithm>
#include <utility>

using namespace evo;
using namespace evo::ren;
using namespace evo::gui;
using namespace evo::gui::helpers;

extern std::shared_ptr<evo::gui::control> make_stacked_groupboxes(uint64_t, const evo::ren::vec2&, const std::vector<std::shared_ptr<evo::gui::control>>&);

namespace menu::group
{
	void rage_antiaim_angles( std::shared_ptr<layout>& e ) {
		//VIRTUALIZER_TIGER_WHITE_START;
		ADD( "Anti-aim", "rage>anti-aim>angles>anti-aim", checkbox, cfg.aa.enabled );

		{
			const auto c = MAKE( "rage>anti-aim>angles>pitch", combo_box, cfg.aa.pitch );
			c->add(
				{ MAKE( "rage>anti-aim>angles>pitch>none", selectable, XOR( "None" ), cfg_t::aa_pitch_none ), MAKE(
					 "rage>anti-aim>angles>pitch>down", selectable, XOR( "Down" ), cfg_t::aa_pitch_down ),
				 MAKE( "rage>anti-aim>angles>pitch>up", selectable, XOR( "Up" ), cfg_t::aa_pitch_up ),
				 MAKE( "rage>anti-aim>angles>pitch>zero", selectable, XOR( "Zero" ), cfg_t::aa_pitch_zero ), MAKE(
					 "rage>anti-aim>angles>pitch>custom", selectable, XOR( "Custom" ), cfg_t::aa_pitch_custom ), } );

			c->callback = [ ]( bits& v ) {
				FIND_SAFE( ps, "rage>anti-aim>angles>pitch>settings" );
				ps->set_visible( v.test( cfg_t::aa_pitch_custom ) );
				};

			ADD_INLINE(
				"Pitch", c,
				MAKE( "rage>anti-aim>angles>pitch>settings", settings, [ ]( auto e ) { ADD( "Angle", "rage>anti-aim>angles>pitch>settings>value", slider<float>, -89.f, 89.f,
																							cfg.aa.pitch_value, XOR( "%.0f deg" ) ); } )->make_invisible( ) );
		}

		{
			const auto c = MAKE( "rage>anti-aim>angles>yaw base", combo_box, cfg.aa.yaw_base );
			c->add(
				{ MAKE( "rage>anti-aim>angles>yaw base>viewangles", selectable, XOR( "Viewangles" ), cfg_t::aa_yaw_base_viewangles ), MAKE(
					 "rage>anti-aim>angles>yaw base>at target", selectable, XOR( "At target" ), cfg_t::aa_yaw_base_at_target ), } );

			ADD_INLINE( "Yaw base", c );
		}

		ADD( "Freestanding", "rage>anti-aim>angles>freestanding", checkbox, cfg.aa.freestand );

		{
			const auto c = MAKE( "rage>anti-aim>angles>yaw", combo_box, cfg.aa.yaw );
			c->add(
				{ MAKE( "rage>anti-aim>angles>yaw>none", selectable, XOR( "None" ), cfg_t::aa_yaw_none ), MAKE(
					 "rage>anti-aim>angles>yaw>backwards", selectable, XOR( "Backwards" ), cfg_t::aa_yaw_backwards ), MAKE(
					 "rage>anti-aim>angles>yaw>custom", selectable, XOR( "Custom" ), cfg_t::aa_yaw_custom ), } );

			c->callback = [ ]( bits& v ) {
				FIND_SAFE( ps, "rage>anti-aim>angles>yaw>settings" );
				ps->set_visible( v.test( cfg_t::aa_yaw_custom ) );
				};

			ADD_INLINE(
				"Yaw", c,
				MAKE( "rage>anti-aim>angles>yaw>settings", settings, [ ]( auto e ) { ADD( "Angle", "rage>anti-aim>angles>yaw>settings>value", slider<float>, -180.f, 180.f, cfg
																						  .aa.yaw_value, XOR( "%.0f deg" ) ); } )->make_invisible( ) );
		}

		{
			const auto c = MAKE( "rage>anti-aim>angles>yaw jitter", combo_box, cfg.aa.yaw_jitter );
			c->add(
				{ MAKE( "rage>anti-aim>angles>yaw jitter>none", selectable, XOR( "None" ), cfg_t::aa_yaw_jitter_none ), MAKE(
					 "rage>anti-aim>angles>yaw jitter>center", selectable, XOR( "Center" ), cfg_t::aa_yaw_jitter_center ), MAKE(
					 "rage>anti-aim>angles>yaw jitter>offset", selectable, XOR( "Offset" ), cfg_t::aa_yaw_jitter_offset ), } );

			c->callback = [ ]( bits& v ) {
				FIND_SAFE( ps, "rage>anti-aim>angles>yaw jitter>settings" );
				ps->set_visible( !v.test( cfg_t::aa_yaw_jitter_none ) );
				};

			ADD_INLINE(
				"Yaw jitter", c,
				MAKE( "rage>anti-aim>angles>yaw jitter>settings", settings, [ ]( auto e ) { ADD( "Amount", "rage>anti-aim>angles>yaw jitter>settings>value", slider<float>, -
																								 180.f, 180.f, cfg.aa.yaw_jitter_value, XOR( "%.0f deg" ) ); } ) );
		}

		ADD_INLINE(
			"Anti-aim override", MAKE( "rage>anti-aim>angles>anti-aim override", checkbox, cfg.aa.manual ),
			MAKE( "rage>anti-aim>angles>anti-aim override>settings", settings, [ ]( auto e ) {
				{
					const auto c = MAKE( "rage>anti-aim>angles>anti-aim override>settings>left",
										 checkbox, cfg.aa.manual_left ); c->callback = [ ]( bool v ) { if ( !v ) return; const auto c1 = ctx->find<checkbox>( GUI_HASH(
											 "rage>anti-aim>angles>anti-aim override>settings>back" ) ); if ( !c1 ) return; c1->set_value( false ); const auto c2 = ctx->find<checkbox>( GUI_HASH(
												 "rage>anti-aim>angles>anti-aim override>settings>right" ) ); if ( !c2 ) return; c2->set_value( false ); }; ADD_INLINE( "Left", c );
				}
				{
					const auto c = MAKE(
						"rage>anti-aim>angles>anti-aim override>settings>right", checkbox, cfg.aa.manual_right ); c->callback = [ ]( bool v ) { if ( !v ) return; const auto c1 =
						ctx->
						find<checkbox>( GUI_HASH( "rage>anti-aim>angles>anti-aim override>settings>back" ) ); if ( !c1 ) return; c1->set_value( false ); const auto c2 = ctx->find<
						checkbox>(
							GUI_HASH( "rage>anti-aim>angles>anti-aim override>settings>left" ) ); if ( !c2 ) return; c2->set_value( false ); }; ADD_INLINE( "Right", c );
				}
				{
					const auto c
						=
						MAKE( "rage>anti-aim>angles>anti-aim override>settings>back", checkbox, cfg.aa.manual_back ); c->callback = [ ]( bool v ) { if ( !v ) return; const auto c1 =
						ctx->
						find<checkbox>( GUI_HASH( "rage>anti-aim>angles>anti-aim override>settings>left" ) ); if ( !c1 ) return; c1->set_value( false ); const auto c2 = ctx->find<
						checkbox>(
							GUI_HASH( "rage>anti-aim>angles>anti-aim override>settings>right" ) ); if ( !c2 ) return; c2->set_value( false ); }; ADD_INLINE( "Back", c );
				} } ) );
		//VIRTUALIZER_TIGER_WHITE_END;
	}

	void rage_antiaim_fakelag( std::shared_ptr<layout>& e ) {
		//VIRTUALIZER_TIGER_WHITE_START;
		{
			const auto c = MAKE( "rage>anti-aim>fakelag>mode", combo_box, cfg.aa.choke_mode );
			c->add(
				{ MAKE( "rage>anti-aim>fakelag>mode>none", selectable, XOR( "None" ), cfg_t::choke_mode_none ), MAKE(
					 "rage>anti-aim>fakelag>mode>always on", selectable, XOR( "Always on" ), cfg_t::choke_mode_always_on ), MAKE(
					 "rage>anti-aim>fakelag>mode>adaptive", selectable, XOR( "Adaptive" ), cfg_t::choke_mode_adaptive ), } );

			ADD_INLINE( "Mode", c );
		}

		ADD( "Limit", "rage>anti-aim>fakelag>limit", slider<float>, 1.f, 14.f, cfg.aa.choke_limit, XOR( "%.0f ticks" ) );
		//VIRTUALIZER_TIGER_WHITE_END;
	}

	void rage_antiaim_desync( std::shared_ptr<layout>& e ) {
		//VIRTUALIZER_TIGER_WHITE_START;
		{
			const auto c = MAKE( "rage>anti-aim>desync>fake yaw", combo_box, cfg.aa.fake );
			c->add(
				{ MAKE( "rage>anti-aim>desync>fake yaw>static", selectable, XOR( "Static" ), cfg_t::fake_static ), MAKE(
					 "rage>anti-aim>desync>fake yaw>opposite", selectable, XOR( "Opposite" ), cfg_t::fake_opposite ),
				 MAKE( "rage>anti-aim>desync>fake yaw>jitter", selectable, XOR( "Jitter" ), cfg_t::fake_jitter ),
				 MAKE( "rage>anti-aim>desync>fake yaw>peek fake", selectable, XOR( "Peek fake" ), cfg_t::fake_peek_fake ), MAKE(
					 "rage>anti-aim>desync>fake yaw>peek real", selectable, XOR( "Peek real" ), cfg_t::fake_peek_real ), } );

			ADD_INLINE(
				"Fake yaw", c,
				MAKE( "rage>anti-aim>desync>fake yaw>settings", settings, [ ]( auto e ) { ADD( "Amount", "rage>anti-aim>desync>fake yaw>settings>value", slider<float>, -180.f,
																							   180.f, cfg.aa.fake_amount, XOR( "%.0f deg" ) ); ADD( "Limit", "rage>anti-aim>desync>fake yaw>settings>limit", slider<float>, 0.f, 60.f, cfg.aa.fake_limit
																																					, XOR( "%.0f deg" ) ); } ) );
		}

		{
			const auto c = MAKE( "rage>anti-aim>desync>leg slide", combo_box, cfg.aa.strafing );
			c->add(
				{ MAKE( "rage>anti-aim>desync>leg slide>default", selectable, XOR( "Default" ), cfg_t::leg_default ), MAKE(
					 "rage>anti-aim>desync>leg slide>never slide", selectable, XOR( "Never slide" ), cfg_t::leg_never ), MAKE(
					 "rage>anti-aim>desync>leg slide>always slide", selectable, XOR( "Always slide" ), cfg_t::leg_always ), } );

			ADD_INLINE( "Leg slide", c );
		}

		{
			const auto c = MAKE( "rage>anti-aim>desync>roll lean", combo_box, cfg.aa.lean_type );
			c->add(
				{ MAKE( "rage>anti-aim>desync>roll lean>none", selectable, XOR( "None" ), cfg_t::lean_none ), MAKE(
					 "rage>anti-aim>desync>roll lean>static", selectable, XOR( "Static" ), cfg_t::lean_static ),
				 MAKE( "rage>anti-aim>desync>roll lean>extend fake", selectable, XOR( "Extend fake" ), cfg_t::lean_extend_fake ),
				 MAKE( "rage>anti-aim>desync>roll lean>invert fake", selectable, XOR( "Invert fake" ), cfg_t::lean_invert_fake ), MAKE(
					 "rage>anti-aim>desync>roll lean>jitter", selectable, XOR( "Jitter" ), cfg_t::lean_jitter ), } );

			ADD_INLINE(
				"Roll lean", c,
				MAKE( "rage>anti-aim>desync>roll lean>settings", settings, [ ]( auto e ) { ADD( "Amount", "rage>anti-aim>desync>roll lean>settings>value", slider<float>, -50.f
																								, 50.f, cfg.aa.lean_amount, XOR( "%.0f deg" ) ); ADD( "Ensure lean", "rage>anti-aim>desync>roll lean>settings>ensure lean", checkbox, cfg.aa.
																																					  ensure_lean )
					  ; } ) );
		}
		//VIRTUALIZER_TIGER_WHITE_END;
	}
	// A clipped, animated row preserves the control and its binds while collapsed.
	class dependent_row final : public layout {
	public:
		dependent_row(control_id id, std::shared_ptr<layout> content, bool shown)
			: layout(id, {}, {0.f, shown ? 24.f : 0.f}, s_none), content_(std::move(content)),
			  progress_(shown ? 1.f : 0.f), target_(shown) {
			size_to_parent_w = true;
			disable_scroll = true;
		}

		void initialize() {
			add(content_);
			process_queues();
		}

		void show(bool shown) { target_ = shown; }
		void refresh() override {
			if (target_ && progress_ >= .99f) layout::refresh();
		}
		void render() override {
			const float step = std::min(draw.frame_time / .18f, 1.f);
			progress_ = target_ ? std::min(1.f, progress_ + step) : std::max(0.f, progress_ - step);
			if (content_->size.x != size.x) {
				content_->size.x = size.x;
				content_->on_control_draw_param_changed(nullptr, 0);
			}
			const float new_height = 24.f * progress_;
			if (size.y != new_height) {
				size.y = new_height;
				if (const auto actual_parent = parent.lock(); actual_parent && actual_parent->is_container)
					actual_parent->as<control_container>()->on_control_draw_param_changed(shared_from_this(), dp_size);
			}
			content_->pos.y = size.y - 24.f;
			if (size.y > 0.f) layout::render();
		}
	private:
		std::shared_ptr<layout> content_;
		float progress_;
		bool target_;
	};

	static void add_dependent_row(const std::shared_ptr<control_container>& parent,
		const std::shared_ptr<checkbox>& toggle, const std::shared_ptr<layout>& row, bool visible)
	{
		const auto animated = std::make_shared<dependent_row>(control_id{row->id ^ hash("dependent")}, row, visible);
		animated->initialize();
		toggle->callback = [weak = std::weak_ptr<dependent_row>(animated)](bool value) {
			if (const auto target = weak.lock()) target->show(value);
		};
		parent->add(animated);
	}

	void rage_aimbot_general(std::shared_ptr<layout>& e) {
		ADD_INLINE("Aimbot", MAKE("rage>aimbot>general>aimbot", checkbox, cfg.aim.enabled), MAKE("rage>aimbot>general>aimbot>settings", settings, [](auto e) {
			ADD("Silent", "rage>aimbot>general>silent", checkbox, cfg.aim.silent);
			ADD("Dormant aimbot", "rage>aimbot>general>target dormant", checkbox, cfg.aim.dormant);
			ADD("Anti-exploit", "rage>aimbot>general>anti-exploit", checkbox, cfg.aim.anti_exploit);
			ADD("Auto fire", "rage>aimbot>general>autofire", checkbox, cfg.aim.autofire);
			const auto extrapolation = MAKE("rage>aimbot>general>extrapolation", checkbox, cfg.aim.force_extrapolation);
			e->add(make_control("Force extrapolation", extrapolation));
			const auto ticks = make_control("Extrapolation ticks", MAKE("rage>aimbot>general>extrapolation>ticks", slider<float>, 0.f, 24.f, cfg.aim.extrapolation_ticks, "%.0f ticks"));
			add_dependent_row(e, extrapolation, ticks, *cfg.aim.force_extrapolation);
			ADD("Max FOV", "rage>aimbot>general>maximum fov", slider<float>, 0.f, 180.f, cfg.aim.fov, "%.0f deg", 1.f, vec2{80.f, 14.f});
		}));

		ADD_INLINE("Visualization of aimbot", MAKE("rage>aimbot>general>visualize aimbot", checkbox, cfg.aim.visualize), MAKE("rage>aimbot>general>visualize aimbot>settings", settings, [](auto e) {
			ADD("Safe color", "rage>aimbot>general>visualize aimbot>settings>safe color", color_picker, cfg.aim.visualize_color_safe);
			ADD("Aim color", "rage>aimbot>general>visualize aimbot>settings>aim color", color_picker, cfg.aim.visualize_color);
		}));
		ADD("Fake duck", "rage>aimbot>general>fake duck", checkbox, cfg.aim.fake_duck);
		ADD_INLINE("Peek assist", MAKE("rage>aimbot>general>peek assist", checkbox, cfg.misc.peek_assist),
			MAKE("rage>aimbot>general>peek assist>color1", color_picker, cfg.misc.peek_assist_col),
			MAKE("rage>aimbot>general>peek assist>color2", color_picker, cfg.misc.peek_assist_col2),
			MAKE("rage>aimbot>general>peek assist>settings", settings, [](auto e) {
				ADD("Retreat on release", "rage>aimbot>general>peek assist>retreat", checkbox, cfg.misc.peek_assist_retreat_on_release);
				const auto mode = MAKE("rage>aimbot>general>peek assist>mode", combo_box, cfg.misc.peek_assist_visual);
				mode->add({MAKE("rage>aimbot>general>peek assist>mode>glow", selectable, "Glow", 0),
					MAKE("rage>aimbot>general>peek assist>mode>filled", selectable, "Filled", 1)});
				ADD_INLINE("Visual mode", mode);
				const auto distance = MAKE("rage>aimbot>general>peek assist>distance", slider<float>, -1.f, 240.f, cfg.misc.peek_assist_distance, "%.0f units");
				distance->infinite_at_min = true;
				e->add(make_control("Distance", distance));
			}));
		ADD("Hide shots", "rage>aimbot>general>hide shot", checkbox, cfg.aim.hideshot);
		ADD("Double tap", "rage>aimbot>general>double tap", checkbox, cfg.aim.doubletap);
		const auto defensive = MAKE("rage>aimbot>general>defensive lag", combo_box, cfg.aim.dt_exploit, vec2{}, vec2{80.f, 20.f});
		defensive->add({MAKE("rage>aimbot>general>defensive lag>none", selectable, "None", cfg_t::def_none),
			MAKE("rage>aimbot>general>defensive lag>air", selectable, "In air", cfg_t::def_in_air),
			MAKE("rage>aimbot>general>defensive lag>always", selectable, "Always on", cfg_t::def_always_on)});
		ADD_INLINE("Defensive lag", defensive);
		const auto delay = MAKE("rage>aimbot>general>delay shot", combo_box, cfg.aim.delay_shot, vec2{}, vec2{80.f, 20.f});
		delay->allow_multiple = true;
		delay->add({MAKE("rage>aimbot>general>delay shot>damage", selectable, "Wait damage", 0),
			MAKE("rage>aimbot>general>delay shot>hitchance", selectable, "Wait hitchance", 1),
			MAKE("rage>aimbot>general>delay shot>safe", selectable, "Wait safe point or resolved", 2)});
		ADD_INLINE("Delay shot", delay);
	}

	void rage_antiaim_custom(std::shared_ptr<layout>&) { }

	void rage_aimbot_weapon(std::shared_ptr<layout>& e, uint32_t grp, const std::string& id) {
		auto hitboxes = MAKE_RUNTIME(id + "hitboxes", combo_box, cfg.weapon_config.hitboxes[grp], vec2{}, vec2{80.f, 20.f});
		hitboxes->allow_multiple = true;
		hitboxes->add({MAKE_RUNTIME(id + "hitboxes>head", selectable, "Head", cfg_t::hitboxes_head),
			MAKE_RUNTIME(id + "hitboxes>chest", selectable, "Chest", cfg_t::hitboxes_chest),
			MAKE_RUNTIME(id + "hitboxes>stomach", selectable, "Stomach", cfg_t::hitboxes_stomach),
			MAKE_RUNTIME(id + "hitboxes>arms", selectable, "Arms", cfg_t::hitboxes_arms),
			MAKE_RUNTIME(id + "hitboxes>legs", selectable, "Legs", cfg_t::hitboxes_legs),
			MAKE_RUNTIME(id + "hitboxes>feet", selectable, "Feet", cfg_t::hitboxes_feet)});
		auto hitbox_settings = MAKE_RUNTIME(id + "hitboxes>settings", settings, nullptr);
		hitbox_settings->add(make_control(XOR("Ignore limbs in moving"), MAKE_RUNTIME(id + "hitboxes>ignore limbs", checkbox, cfg.weapon_config.ignore_limbs[grp])));
		ADD_INLINE("Hitboxes", hitboxes, hitbox_settings);

		auto multipoint = MAKE_RUNTIME(id + "multipoint", combo_box, cfg.weapon_config.pointscales[grp], vec2{}, vec2{80.f, 20.f});
		multipoint->allow_multiple = true;
		multipoint->add({MAKE_RUNTIME(id + "multipoint>head", selectable, "Head", cfg_t::hitboxes_head),
			MAKE_RUNTIME(id + "multipoint>chest", selectable, "Chest", cfg_t::hitboxes_chest),
			MAKE_RUNTIME(id + "multipoint>stomach", selectable, "Stomach", cfg_t::hitboxes_stomach),
			MAKE_RUNTIME(id + "multipoint>arms", selectable, "Arms", cfg_t::hitboxes_arms),
			MAKE_RUNTIME(id + "multipoint>legs", selectable, "Legs", cfg_t::hitboxes_legs),
			MAKE_RUNTIME(id + "multipoint>feet", selectable, "Feet", cfg_t::hitboxes_feet)});
		auto mp_settings = MAKE_RUNTIME(id + "multipoint>settings", settings, nullptr);
		mp_settings->add(make_control(XOR("Head scale"), MAKE_RUNTIME(id + "multipoint>head scale", slider<float>, -1.f, 100.f, cfg.weapon_config.head_scale[grp], (ssa<float>{ssv<float>{0.f, "%.0f%%"}, ssv<float>{-1.f, "Auto"}}))));
		mp_settings->add(make_control(XOR("Body scale"), MAKE_RUNTIME(id + "multipoint>body scale", slider<float>, -1.f, 100.f, cfg.weapon_config.body_scale[grp], (ssa<float>{ssv<float>{0.f, "%.0f%%"}, ssv<float>{-1.f, "Auto"}}))));
		mp_settings->add(make_control(XOR("Prefer center points"), MAKE_RUNTIME(id + "multipoint>center", checkbox, cfg.weapon_config.center_multipoint[grp])));
		ADD_INLINE("Multipoint", multipoint, mp_settings);

		auto hc_settings = MAKE_RUNTIME(id + "hit chance>settings", settings, nullptr);
		{
			auto force = MAKE_RUNTIME(id + "hit chance>force", checkbox, cfg.weapon_config.force_shot[grp]);
			hc_settings->add(make_control("Force shot", force));
			auto minimum = make_control("Minimum hitchance", MAKE_RUNTIME(id + "hit chance>minimum", slider<float>, 0.f, 100.f, cfg.weapon_config.minimum_hitchance[grp], "%.0f%%"));
			add_dependent_row(hc_settings, force, minimum, *cfg.weapon_config.force_shot[grp]);
			hc_settings->add(make_control(XOR("Double tap hitchance"), MAKE_RUNTIME(id + "hit chance>double tap", slider<float>, 0.f, 100.f, cfg.weapon_config.doubletap_hitchance[grp], "%.0f%%")));
		}
		ADD_INLINE("Hitchance", MAKE_RUNTIME(id + "hit chance", slider<float>, 0.f, 100.f, cfg.weapon_config.hitchance[grp], "%.0f%%", 1.f, vec2{80.f, 14.f}), hc_settings);
		ADD_RUNTIME("Min damage", id + "min damage", slider<float>, 0.f, 125.f, cfg.weapon_config.mindmg[grp], (ssa<float>{ssv<float>{101.f, "HP + %.0f"}, ssv<float>{0.f, "%.0f hp"}}), 1.f, vec2{80.f, 14.f});

	}

	void rage_aimbot_extra(std::shared_ptr<layout>& e, uint32_t grp, const std::string& id) {
		auto add_mode = [&](const char* name, const std::string& key, value_param<bits>& mode_value, value_param<bits>& disabled, value_param<bool>& peek) {
			auto mode = MAKE_RUNTIME(key, combo_box, mode_value, vec2{}, vec2{80.f, 20.f});
			mode->add({MAKE_RUNTIME(key + ">off", selectable, "Off", 0), MAKE_RUNTIME(key + ">prefer", selectable, "Prefer", 1), MAKE_RUNTIME(key + ">force", selectable, "Force", 2)});
			auto gear = MAKE_RUNTIME(key + ">settings", settings, nullptr);
			{
				auto disablers = MAKE_RUNTIME(key + ">disablers", combo_box, disabled);
				disablers->allow_multiple = true;
				disablers->add({MAKE_RUNTIME(key + ">shot", selectable, "Target shot", 0), MAKE_RUNTIME(key + ">resolved", selectable, "Target resolved", 1), MAKE_RUNTIME(key + ">safe", selectable, "Target safe point", 2), MAKE_RUNTIME(key + ">damage", selectable, "Low damage", 3)});
				gear->add(make_control("Disablers", disablers));
				gear->add(make_control(XOR("Force on peek"), MAKE_RUNTIME(key + ">peek", checkbox, peek)));
			}
			e->add(make_inlined(hash(key + ">row"), [&](std::shared_ptr<layout>& row) {
				row->add(std::make_shared<label>(control_id{hash(key + ">label")}, name)); row->add(mode); row->add(gear);
			}));
		};
		add_mode("Safe point", id + "safe point", cfg.weapon_config.safe_mode[grp], cfg.weapon_config.safe_disablers[grp], cfg.weapon_config.safe_on_peek[grp]);
		add_mode("Body aim", id + "body aim", cfg.weapon_config.body_mode[grp], cfg.weapon_config.body_disablers[grp], cfg.weapon_config.body_on_peek[grp]);
		ADD_RUNTIME("Force extra safety", id + "force extra safety", checkbox, cfg.aim.fallback_on_key);
		ADD_RUNTIME("Force body aim", id + "force bodyaim", checkbox, cfg.aim.baim_on_key);
		ADD_RUNTIME("Headshot only", id + "headshot only", checkbox, cfg.aim.hsmode);

		auto autostop_settings = MAKE_RUNTIME(id + "autostop>settings", settings, nullptr);
		{
			auto mode = MAKE_RUNTIME(id + "autostop>mode", combo_box, cfg.weapon_config.autostop_mode[grp]);
			mode->allow_multiple = true;
			mode->add({
				MAKE_RUNTIME(id + "autostop>between", selectable, "Between shots", cfg_t::autostop_inbetween),
				MAKE_RUNTIME(id + "autostop>early", selectable, "Early", cfg_t::autostop_predictive),
				MAKE_RUNTIME(id + "autostop>duck", selectable, "Duck", cfg_t::autostop_duck),
				MAKE_RUNTIME(id + "autostop>air", selectable, "In air", cfg_t::autostop_air),
				MAKE_RUNTIME(id + "autostop>land", selectable, "On land", cfg_t::autostop_land)});
			autostop_settings->add(make_inlined(GUI_HASH("Mode_content"), [&](std::shared_ptr<layout>& row) {
				const std::vector<std::shared_ptr<evo::gui::control>> _{ mode };
				row->add(std::make_shared<label>(control_id{hash(XOR("Mode")) + _.size()}, XOR("Mode")));
				for (const auto& item : _)
					row->add(item);
			}));
		}
		ADD_INLINE("Autostop", MAKE_RUNTIME(id + "autostop", checkbox, cfg.weapon_config.autostop[grp]), autostop_settings);
	}

	void rage_aimbot_weapon_extra( const std::shared_ptr<layout>& e, uint32_t grp, const std::string& id, bool vis = false ) {
		//VIRTUALIZER_TIGER_WHITE_START;
		auto group_sz = window_size_to_groupbox_size( 848.f, 588.f, 4.f ) - vec2{ 0.f, 36.f };
		group_sz.x = 218.f;

		const auto doubletab = MAKE_RUNTIME( id, layout, vec2{ 0.f, 0.f }, vec2{ group_sz.x * 2.f + 16.f, 446.f }, s_justify );
		doubletab->is_visible = vis;
		doubletab->make_not_clip( );
		doubletab->add(
			make_groupbox(
				id + XOR( ">weapon" ), XOR( "WEAPON" ), group_sz, [ &grp, &id ]( auto e ) {
					rage_aimbot_weapon( e, grp, id + XOR( ">weapon>" ) );
				} ) );
		doubletab->add(
			make_groupbox(
				id + XOR( ">extra" ), XOR( "EXTRA" ), group_sz, [ &grp, &id ]( auto e ) {
					rage_aimbot_extra( e, grp, id + XOR( ">extra>" ) );
				} ) );

		doubletab->process_queues( );
		doubletab->reset( );

		e->add( doubletab );
		//VIRTUALIZER_TIGER_WHITE_END;
	}
	void rage_antiaim( std::shared_ptr<layout>& e ) {
		//VIRTUALIZER_TIGER_WHITE_START;
		const auto group_sz = window_size_to_groupbox_size( 848.f, 588.f ) - vec2{ 0.f, 36.f };
		const auto group_half_sz = group_sz * vec2{ 1.f, .5f } - vec2{ 0.f, 5.f };

		const auto area = MAKE( "rage>tab>anti-aim", layout, vec2{ 0.f, 36.f }, vec2{ 800.f, 446.f }, s_justify );
		area->is_visible = false;
		area->make_not_clip( );
		area->add( make_groupbox( XOR( "rage>anti-aim>angles" ), XOR( "ANGLES" ), group_sz, rage_antiaim_angles ) );
		area->add(
			make_stacked_groupboxes(
				GUI_HASH( "rage>anti-aim>fakelag,desync" ), group_sz,
				{ make_groupbox( XOR( "rage>anti-aim>fakelag" ), XOR( "FAKELAG" ), group_half_sz * vec2{1.f, .5f}, rage_antiaim_fakelag ),
				 make_groupbox( XOR( "rage>anti-aim>desync" ), XOR( "DESYNC" ), group_half_sz + group_half_sz * vec2{0.f, .5f}, rage_antiaim_desync ) } ) );
		area->add( make_groupbox( XOR( "rage>anti-aim>custom options" ), XOR( "CUSTOM OPTIONS" ), group_sz, rage_antiaim_custom ) );

		e->add( area );
		//VIRTUALIZER_TIGER_WHITE_END;
	}






	void rage_aimbot(std::shared_ptr<layout>& e) {
		auto group_sz = window_size_to_groupbox_size(848.f, 588.f, 4.f) - vec2{0.f, 36.f};
		const auto page = MAKE("rage>tab>aimbot", layout, vec2{0.f, 36.f}, vec2{800.f, 446.f}, s_none);
		page->make_not_clip();
		const auto tabs = MAKE("rage>weapons", tabs_layout, vec2{0.f, 0.f}, vec2{group_sz.x * .5f, 406.f}, td_vertical);
		tabs->add(MAKE("rage>weapons>general", child_tab, "General", GUI_HASH("rage>weapon>general"))->make_vertical()->make_selected());
		auto area = MAKE("rage>aimbot>content", layout, vec2{group_sz.x * .5f + 16.f, 0.f}, vec2{800.f - (group_sz.x * .5f + 16.f), 446.f}, s_none);
		area->make_not_clip();
		group_sz.x = 218.f;
		rage_aimbot_weapon_extra(area, 0, "rage>weapon>general", true);
		area->add(make_groupbox("rage>aimbot>general", "GENERAL", {group_sz.x * 2 + 32.f, 0.f}, group_sz, rage_aimbot_general));
		const auto ovr = MAKE("rage>override", button, "Add override", nullptr, vec2{0.f, 426.f}, vec2{group_sz.x * .5f - 16.f, 20.f});
		ovr->margin = {};
		ovr->center_text = false;
		page->add(tabs);
		page->add(area);
		page->add(ovr);
		e->add(page);
	}


	void rage_tab( std::shared_ptr<layout> &e ) {
	//VIRTUALIZER_TIGER_WHITE_START;
	const auto tabs = MAKE( "rage>tabs", tabs_layout, vec2{ 0.f, 0.f }, vec2{ 800.f, 22.f }, td_horizontal );
	tabs->add( MAKE( "rage>tabs>aimbot", child_tab, XOR( "Aimbot" ), GUI_HASH( "rage>tab>aimbot" ) )->make_selected( ) );
	tabs->add( MAKE( "rage>tabs>anti-aim", child_tab, XOR( "Anti-aim" ), GUI_HASH( "rage>tab>anti-aim" ) ) );

	e->add( tabs );
	rage_aimbot( e );
	rage_antiaim( e );
	//VIRTUALIZER_TIGER_WHITE_END;
}





}


