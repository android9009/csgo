#include "preview_bridge.hpp"
#include <gui/helpers.h>
#include <gui/values.h>
#include <gui/controls.h>



#include <windows.h>
#include <windowsx.h>
#include <memory>
#include <sstream>
#include <iostream>
#include <functional>
#include <WinUser.h>
#include <string_view>
#include <map>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <deque>
#include <mutex>
#include <optional>
#include <filesystem>
#include <fstream>
#include <random>
#include <array>
#include <mutex>

#include "resources/cursor.h"
#include "resources/icon_add.h"
#include "resources/icon_clear.h"
#include "resources/icon_cloud.h"
#include "resources/icon_copy.h"
#include "resources/icon_down.h"
#include "resources/icon_legit.h"
#include "resources/icon_misc.h"
#include "resources/icon_paste.h"
#include "resources/icon_rage.h"
#include "resources/icon_refresh.h"
#include "resources/icon_search.h"
#include "resources/icon_skins.h"
#include "resources/icon_scripts.h"
#include "resources/icon_up.h"
#include "resources/icon_visuals.h"
#include "resources/icon_save.h"
#include "resources/icon_configs.h"
#include "resources/icon_keys.h"
#include "resources/icon_info.h"
#include "resources/icon_close.h"
#include "resources/icon_settings.h"
#include "resources/icon_load.h"
#include "resources/icon_import.h"
#include "resources/icon_export.h"
#include "resources/icon_delete.h"
#include "resources/icon_bug.h"
#include "resources/icon_file.h"
#include "resources/icon_autoload.h"
#include "resources/logo_head.h"
#include "resources/logo_stripes.h"
#include "resources/icon_allow_insecure.h"
#include "resources/icon_cloud_upd.h"
#include "resources/loading.h"
#include "resources/hotkey_icons.h"

#include "src/gui/popups/about_popup.h"
#include "src/gui/popups/search_popup.h"
#include "src/gui/popups/configs_popup.h"
#include "src/gui/popups/hotkey_overview_popup.h"


#include <ren/adapters/adapter_dx9.h>
#include <ren/renderer.h>
#include <ren/types/animated_texture.h>
#include "cfg.h"
#include "port.hpp"
#include "extra/menu.h"
#include <chrono>
#include <mutex>
#include <exception>
#include <cstring>
#include "../../utils/log.hpp"
using namespace evo;
using namespace evo::ren;
using namespace evo::gui;
using namespace evo::gui::helpers;
static std::weak_ptr<evo::gui::window> main_wnd;
static std::recursive_mutex gui_mutex;
static IDirect3DDevice9* gui_device = nullptr;
static bool objects_ready = false;
cfg_t cfg;

std::shared_ptr<control> make_stacked_groupboxes( uint64_t id, const vec2& size,
                                                  const std::vector<std::shared_ptr<control>>& groups ) {
    const auto l = std::make_shared<layout>( evo::gui::control_id{ id, "" }, vec2( ), size, s_vertical );
    l->make_not_clip( )->margin = rect( );

    std::shared_ptr<control> last{};
    for ( const auto& c : groups ) {
        c->margin = rect( 0.f, 0.f, 0.f, 10.f );
        l->add( c );
        last = c;
    }

    last->size.y -= 2.f;
    last->margin = rect( );
    return l;
}
#define XOR(s) s

#define FIND_SAFE(v, n) const auto v = evo::gui::ctx->find(GUI_HASH(n)); if (!v) return;
#define FIND_SAFE_T(v, n, t) const auto v = evo::gui::ctx->find<t>(GUI_HASH(n)); if (!v) return;

#define MAKE_TAB(i, n) std::make_shared<tab>(evo::gui::control_id{GUI_HASH(i), ""}, (n), GUI_HASH(i "_container"), draw.textures[GUI_HASH("icon_" i)])
#define MAKE(h, t, ...) std::make_shared<t>(evo::gui::control_id{GUI_HASH(h),(h)}, __VA_ARGS__)
#define MAKE_RUNTIME(h, t, ...) std::make_shared<t>(evo::gui::control_id{hash((h).c_str()), h}, __VA_ARGS__)

#define ADD(n, h, t, ...) e->add(make_control(XOR(n), MAKE(h, t, __VA_ARGS__)))
#define ADD_RUNTIME(n, h, t, ...) e->add(make_control((n), MAKE_RUNTIME(h, t, __VA_ARGS__)))
#define ADD_INV(n, h, t, ...) e->add(make_control((n), MAKE(h, t, __VA_ARGS__))->make_invisible())
#define ADD_DEP(n, h, ht, t, ...) { const auto _ = MAKE(h, t, __VA_ARGS__); _->callback = [](bool v) { static const auto _ = ctx->find(GUI_HASH(ht))->parent.lock(); _->set_visible(v); }; e->add(make_control(XOR(n), _)); }
#define ADD_INLINE(l, ...) e->add(make_inlined(GUI_HASH(l "_content"), [&](std::shared_ptr<layout>& e) { \
	const std::vector<std::shared_ptr<evo::gui::control>> _ { __VA_ARGS__ };                                                           \
	e->add(std::make_shared<label>(control_id{hash((l)) + _.size()}, XOR(l)));                                                                                 \
	for (const auto& c : _)                                                                                                  \
		e->add(c);																										     \
}))

#define ADD_LINE(l, f, ...)                                                                                            \
	e->add(make_inlined(GUI_HASH(l "_content"),                                                                        \
						[&](std::shared_ptr<layout> &e)                                                                \
						{                                                                                              \
							const std::vector<std::shared_ptr<evo::gui::control>> _{__VA_ARGS__};                                \
							e->add(f);                                                                                 \
							for (const auto &c : _)                                                                    \
								e->add(c);                                                                             \
						}))


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

	void rage_antiaim_custom(std::shared_ptr<layout>& e) { }

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
		{ auto e = hitbox_settings; ADD_RUNTIME("Ignore limbs in moving", id + "hitboxes>ignore limbs", checkbox, cfg.weapon_config.ignore_limbs[grp]); }
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
		{ auto e = mp_settings;
			ADD_RUNTIME("Head scale", id + "multipoint>head scale", slider<float>, -1.f, 100.f, cfg.weapon_config.head_scale[grp], (ssa<float>{ssv<float>{0.f, "%.0f%%"}, ssv<float>{-1.f, "Auto"}}));
			ADD_RUNTIME("Body scale", id + "multipoint>body scale", slider<float>, -1.f, 100.f, cfg.weapon_config.body_scale[grp], (ssa<float>{ssv<float>{0.f, "%.0f%%"}, ssv<float>{-1.f, "Auto"}}));
			ADD_RUNTIME("Prefer center points", id + "multipoint>center", checkbox, cfg.weapon_config.center_multipoint[grp]); }
		ADD_INLINE("Multipoint", multipoint, mp_settings);

		auto hc_settings = MAKE_RUNTIME(id + "hit chance>settings", settings, nullptr);
		{ auto e = hc_settings;
			auto force = MAKE_RUNTIME(id + "hit chance>force", checkbox, cfg.weapon_config.force_shot[grp]);
			e->add(make_control("Force shot", force));
			auto minimum = make_control("Minimum hitchance", MAKE_RUNTIME(id + "hit chance>minimum", slider<float>, 0.f, 100.f, cfg.weapon_config.minimum_hitchance[grp], "%.0f%%"));
			add_dependent_row(e, force, minimum, *cfg.weapon_config.force_shot[grp]);
			ADD_RUNTIME("Double tap hitchance", id + "hit chance>double tap", slider<float>, 0.f, 100.f, cfg.weapon_config.doubletap_hitchance[grp], "%.0f%%"); }
		ADD_INLINE("Hitchance", MAKE_RUNTIME(id + "hit chance", slider<float>, 0.f, 100.f, cfg.weapon_config.hitchance[grp], "%.0f%%", 1.f, vec2{80.f, 14.f}), hc_settings);
		ADD_RUNTIME("Min damage", id + "min damage", slider<float>, 0.f, 125.f, cfg.weapon_config.mindmg[grp], (ssa<float>{ssv<float>{101.f, "HP + %.0f"}, ssv<float>{0.f, "%.0f hp"}}), 1.f, vec2{80.f, 14.f});

	}

	void rage_aimbot_extra(std::shared_ptr<layout>& e, uint32_t grp, const std::string& id) {
		auto add_mode = [&](const char* name, const std::string& key, value_param<bits>& mode_value, value_param<bits>& disabled, value_param<bool>& peek) {
			auto mode = MAKE_RUNTIME(key, combo_box, mode_value, vec2{}, vec2{80.f, 20.f});
			mode->add({MAKE_RUNTIME(key + ">off", selectable, "Off", 0), MAKE_RUNTIME(key + ">prefer", selectable, "Prefer", 1), MAKE_RUNTIME(key + ">force", selectable, "Force", 2)});
			auto gear = MAKE_RUNTIME(key + ">settings", settings, nullptr);
			{ auto e = gear;
				auto disablers = MAKE_RUNTIME(key + ">disablers", combo_box, disabled);
				disablers->allow_multiple = true;
				disablers->add({MAKE_RUNTIME(key + ">shot", selectable, "Target shot", 0), MAKE_RUNTIME(key + ">resolved", selectable, "Target resolved", 1), MAKE_RUNTIME(key + ">safe", selectable, "Target safe point", 2), MAKE_RUNTIME(key + ">damage", selectable, "Low damage", 3)});
				e->add(make_control("Disablers", disablers));
				ADD_RUNTIME("Force on peek", key + ">peek", checkbox, peek); }
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
			auto e = autostop_settings;
			auto mode = MAKE_RUNTIME(id + "autostop>mode", combo_box, cfg.weapon_config.autostop_mode[grp]);
			mode->allow_multiple = true;
			mode->add({
				MAKE_RUNTIME(id + "autostop>between", selectable, "Between shots", cfg_t::autostop_inbetween),
				MAKE_RUNTIME(id + "autostop>early", selectable, "Early", cfg_t::autostop_predictive),
				MAKE_RUNTIME(id + "autostop>duck", selectable, "Duck", cfg_t::autostop_duck),
				MAKE_RUNTIME(id + "autostop>air", selectable, "In air", cfg_t::autostop_air),
				MAKE_RUNTIME(id + "autostop>land", selectable, "On land", cfg_t::autostop_land)});
			ADD_INLINE("Mode", mode);
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


namespace FatalityMenu {
bool Initialize(IDirect3DDevice9* device, HWND hwnd) {
    std::lock_guard<std::recursive_mutex> lock(gui_mutex);
    if (gui_device) return gui_device == device && objects_ready;
    if (!device || !hwnd) return false;
    // EndScene is also hooked on temporary probe devices during hook setup.
    // Do not bind the GUI to a probe rather than the actual game device.
    D3DDEVICE_CREATION_PARAMETERS params{};
    if (FAILED(device->GetCreationParameters(&params))) return false;
    if (params.hFocusWindow && !IsWindow(params.hFocusWindow)) return false;
    char class_name[64]{};
    if (params.hFocusWindow && GetClassNameA(params.hFocusWindow, class_name, sizeof(class_name)) &&
        std::strcmp(class_name, "csgo_base_dummy") == 0) return false;
    try {
                draw.adapter = std::make_shared<adapter_dx9>( hwnd, device );
                // draw.create_layer( dx_adapter::esp_layer );
                draw.create_layer( 32 );
                draw.create_layer( 63 );
               // draw.create_layer( 33 );
               // draw.create_layer( 34 );
               // draw.create_layer( 35 );
               // draw.create_layer( 36 );
               // draw.create_layer( 37 );
               // draw.create_layer( 38 );

                draw.adapter->on_vertex_shader_compilation_error = [ ]( const char* err ) {
                    // print_to_console( sdk::color::attention( ), XOR_STR( "Failed to compile vertex shader:\n" ) );
                    // print_to_console( err );
                    };

                draw.adapter->on_fragment_shader_compilation_error = [ ]( const char* err ) {
                    // print_to_console( sdk::color::attention( ), XOR_STR( "Failed to compile fragment shader:\n" ) );
                    // print_to_console( err );
                    };

                // buf = std::make_shared<layer>( draw.adapter->alloc_vb( ), draw.adapter->alloc_ib( ) );
                 //buf->vb->should_discard = false;
                 //buf->ib->should_discard = false;
                 //buf->create( );

                const auto& d = draw.layers[ 32 ];
                d->vb->should_discard = false;
                d->ib->should_discard = false;

                evo::gui::ctx = std::make_unique<evo::gui::context>( );

                char win_dir_arr[ 256 ]{};
                GetSystemWindowsDirectoryA( win_dir_arr, 256 );

                std::string win_dir{ win_dir_arr };
                draw.fonts[ GUI_HASH( "segoeui13" ) ] =
                    std::make_shared<font>( ( win_dir + ( "/fonts/segoeui.ttf" ) ).c_str( ), 13.f, font_flag_shadow, 0, 0x45F );
                draw.fonts[ GUI_HASH( "small8" ) ] =
                    std::make_shared<font_gdi>( ( "Small Fonts" ), 8.f, font_flag_outline, 0, 0x45F );
                draw.fonts[ GUI_HASH( "segoeuib13" ) ] =
                    std::make_shared<font>( ( win_dir + ( "/fonts/segoeuib.ttf" ) ).c_str( ), 13.f, font_flag_shadow, 0, 0x45F );
                draw.fonts[ GUI_HASH( "segoeuib28" ) ] =
                    std::make_shared<font>( ( win_dir + ( "/fonts/segoeuib.ttf" ) ).c_str( ), 28.f, font_flag_shadow, 0, 0x45F );

                // menu::menu.create( );

                {
                    ctx->res.general.logo_head = std::make_shared<texture>( data::logo_head, sizeof( data::logo_head ) );
                    ctx->res.general.logo_stripes = std::make_shared<texture>( data::logo_stripes, sizeof( data::logo_stripes ) );
                    ctx->res.general.cursor = std::make_shared<texture>( data::cursor, sizeof( data::cursor ) );
                    ctx->res.general.loader = std::make_shared<texture>( data::loading, sizeof( data::loading ) );

                    draw.textures[ GUI_HASH( "gui_logo_head" ) ] = ctx->res.general.logo_head;
                    draw.textures[ GUI_HASH( "gui_logo_stripes" ) ] = ctx->res.general.logo_stripes;
                    draw.textures[ GUI_HASH( "gui_cursor" ) ] = ctx->res.general.cursor;
                    draw.textures[ GUI_HASH( "gui_loading" ) ] = ctx->res.general.loader;

                    ctx->res.icons.up = std::make_shared<texture>( data::icon_up, sizeof( data::icon_up ) );
                    ctx->res.icons.down = std::make_shared<texture>( data::icon_down, sizeof( data::icon_down ) );
                    ctx->res.icons.notify_clear = std::make_shared<texture>( data::icon_clear, sizeof( data::icon_clear ) );
                    ctx->res.icons.copy = std::make_shared<texture>( data::icon_copy, sizeof( data::icon_copy ) );
                    ctx->res.icons.paste = std::make_shared<texture>( data::icon_paste, sizeof( data::icon_paste ) );
                    ctx->res.icons.add = std::make_shared<texture>( data::icon_add, sizeof( data::icon_add ) );
                    ctx->res.icons.search = std::make_shared<texture>( data::icon_search, sizeof( data::icon_search ) );
                    ctx->res.icons.settings = std::make_shared<texture>( data::icon_settings, sizeof( data::icon_settings ) );
                    ctx->res.icons.bug = std::make_shared<texture>( data::icon_bug, sizeof( data::icon_bug ) );

                    draw.textures[ GUI_HASH( "gui_icon_up" ) ] = ctx->res.icons.up;
                    draw.textures[ GUI_HASH( "gui_icon_down" ) ] = ctx->res.icons.down;
                    draw.textures[ GUI_HASH( "gui_icon_clear" ) ] = ctx->res.icons.notify_clear;
                    draw.textures[ GUI_HASH( "gui_icon_copy" ) ] = ctx->res.icons.copy;
                    draw.textures[ GUI_HASH( "gui_icon_paste" ) ] = ctx->res.icons.paste;
                    draw.textures[ GUI_HASH( "gui_icon_add" ) ] = ctx->res.icons.add;
                    draw.textures[ GUI_HASH( "gui_icon_search" ) ] = ctx->res.icons.search;
                    draw.textures[ GUI_HASH( "gui_icon_settings" ) ] = ctx->res.icons.settings;
                    draw.textures[ GUI_HASH( "gui_icon_bug" ) ] = ctx->res.icons.bug;

                    ctx->res.icons.keys[ VK_MENU ] = std::make_shared<texture>( data::key_alt, sizeof( data::key_alt ) );
                    ctx->res.icons.keys[ VK_DOWN ] = std::make_shared<texture>( data::key_down, sizeof( data::key_down ) );
                    ctx->res.icons.keys[ VK_LEFT ] = std::make_shared<texture>( data::key_left, sizeof( data::key_left ) );
                    ctx->res.icons.keys[ VK_RIGHT ] = std::make_shared<texture>( data::key_right, sizeof( data::key_right ) );
                    ctx->res.icons.keys[ VK_UP ] = std::make_shared<texture>( data::key_up, sizeof( data::key_up ) );
                    ctx->res.icons.keys[ VK_BACK ] = std::make_shared<texture>( data::key_back, sizeof( data::key_back ) );
                    ctx->res.icons.keys[ VK_CAPITAL ] = std::make_shared<texture>( data::key_caps, sizeof( data::key_caps ) );
                    ctx->res.icons.keys[ VK_CONTROL ] = std::make_shared<texture>( data::key_ctrl, sizeof( data::key_ctrl ) );
                    ctx->res.icons.keys[ VK_DELETE ] = std::make_shared<texture>( data::key_delete, sizeof( data::key_delete ) );
                    ctx->res.icons.keys[ VK_END ] = std::make_shared<texture>( data::key_end, sizeof( data::key_end ) );
                    ctx->res.icons.keys[ VK_HOME ] = std::make_shared<texture>( data::key_home, sizeof( data::key_home ) );
                    ctx->res.icons.keys[ VK_INSERT ] = std::make_shared<texture>( data::key_ins, sizeof( data::key_ins ) );
                    ctx->res.icons.keys[ VK_PRIOR ] = std::make_shared<texture>( data::key_pgup, sizeof( data::key_pgup ) );
                    ctx->res.icons.keys[ VK_NEXT ] = std::make_shared<texture>( data::key_pgdn, sizeof( data::key_pgdn ) );
                    ctx->res.icons.keys[ VK_RETURN ] = std::make_shared<texture>( data::key_ret, sizeof( data::key_ret ) );
                    ctx->res.icons.keys[ VK_SHIFT ] = std::make_shared<texture>( data::key_shift, sizeof( data::key_shift ) );
                    ctx->res.icons.keys[ VK_SPACE ] = std::make_shared<texture>( data::key_space, sizeof( data::key_space ) );
                    ctx->res.icons.keys[ VK_TAB ] = std::make_shared<texture>( data::key_tab, sizeof( data::key_tab ) );
                    ctx->res.icons.keys[ VK_LBUTTON ] = std::make_shared<texture>( data::key_m1, sizeof( data::key_m1 ) );
                    ctx->res.icons.keys[ VK_RBUTTON ] = std::make_shared<texture>( data::key_m2, sizeof( data::key_m2 ) );
                    ctx->res.icons.keys[ VK_MBUTTON ] = std::make_shared<texture>( data::key_m3, sizeof( data::key_m3 ) );
                    ctx->res.icons.keys[ VK_XBUTTON1 ] = std::make_shared<texture>( data::key_m4, sizeof( data::key_m4 ) );
                    ctx->res.icons.keys[ VK_XBUTTON2 ] = std::make_shared<texture>( data::key_m5, sizeof( data::key_m5 ) );
                    ctx->res.icons.keys[ VK_MULTIPLY ] = std::make_shared<texture>( data::key_num_mul, sizeof( data::key_num_mul ) );
                    ctx->res.icons.keys[ VK_ADD ] = std::make_shared<texture>( data::key_num_add, sizeof( data::key_num_add ) );
                    ctx->res.icons.keys[ VK_SEPARATOR ] = std::make_shared<texture>( data::key_num_dec, sizeof( data::key_num_dec ) );
                    ctx->res.icons.keys[ VK_SUBTRACT ] = std::make_shared<texture>( data::key_num_sub, sizeof( data::key_num_sub ) );
                    ctx->res.icons.keys[ VK_DECIMAL ] = std::make_shared<texture>( data::key_num_dec, sizeof( data::key_num_dec ) );
                    ctx->res.icons.keys[ VK_DIVIDE ] = std::make_shared<texture>( data::key_num_div, sizeof( data::key_num_div ) );
                    ctx->res.icons.keys[ VK_NUMPAD0 ] = std::make_shared<texture>( data::key_num0, sizeof( data::key_num0 ) );
                    ctx->res.icons.keys[ VK_NUMPAD1 ] = std::make_shared<texture>( data::key_num1, sizeof( data::key_num1 ) );
                    ctx->res.icons.keys[ VK_NUMPAD2 ] = std::make_shared<texture>( data::key_num2, sizeof( data::key_num2 ) );
                    ctx->res.icons.keys[ VK_NUMPAD3 ] = std::make_shared<texture>( data::key_num3, sizeof( data::key_num3 ) );
                    ctx->res.icons.keys[ VK_NUMPAD4 ] = std::make_shared<texture>( data::key_num4, sizeof( data::key_num4 ) );
                    ctx->res.icons.keys[ VK_NUMPAD5 ] = std::make_shared<texture>( data::key_num5, sizeof( data::key_num5 ) );
                    ctx->res.icons.keys[ VK_NUMPAD6 ] = std::make_shared<texture>( data::key_num6, sizeof( data::key_num6 ) );
                    ctx->res.icons.keys[ VK_NUMPAD7 ] = std::make_shared<texture>( data::key_num7, sizeof( data::key_num7 ) );
                    ctx->res.icons.keys[ VK_NUMPAD8 ] = std::make_shared<texture>( data::key_num8, sizeof( data::key_num8 ) );
                    ctx->res.icons.keys[ VK_NUMPAD9 ] = std::make_shared<texture>( data::key_num9, sizeof( data::key_num9 ) );

                    for ( const auto& [k, t] : ctx->res.icons.keys )
                        draw.textures[ GUI_HASH( "gui_icon_key" ) + k ] = t;

                    // menu resources
                    draw.textures[ GUI_HASH( "icon_rage" ) ] = std::make_shared<texture>( data::icon_rage, sizeof( data::icon_rage ) );
                    draw.textures[ GUI_HASH( "icon_legit" ) ] = std::make_shared<texture>( data::icon_legit, sizeof( data::icon_legit ) );
                    draw.textures[ GUI_HASH( "icon_visuals" ) ] = std::make_shared<texture>( data::icon_visuals, sizeof( data::icon_visuals ) );
                    draw.textures[ GUI_HASH( "icon_misc" ) ] = std::make_shared<texture>( data::icon_misc, sizeof( data::icon_misc ) );
                    draw.textures[ GUI_HASH( "icon_scripts" ) ] = std::make_shared<texture>( data::icon_scripts, sizeof( data::icon_scripts ) );
                    draw.textures[ GUI_HASH( "icon_skins" ) ] = std::make_shared<texture>( data::icon_skins, sizeof( data::icon_skins ) );

                    // hack resources
                    draw.textures[ GUI_HASH( "icon_cloud" ) ] = std::make_shared<texture>( data::icon_cloud, sizeof( data::icon_cloud ) );
                    draw.textures[ GUI_HASH( "icon_file" ) ] = std::make_shared<texture>( data::icon_file, sizeof( data::icon_file ) );
                    draw.textures[ GUI_HASH( "icon_refresh" ) ] = std::make_shared<texture>( data::icon_refresh, sizeof( data::icon_refresh ) );
                    draw.textures[ GUI_HASH( "icon_save" ) ] = std::make_shared<texture>( data::icon_save, sizeof( data::icon_save ) );
                    draw.textures[ GUI_HASH( "icon_configs" ) ] = std::make_shared<texture>( data::icon_configs, sizeof( data::icon_configs ) );
                    draw.textures[ GUI_HASH( "icon_keys" ) ] = std::make_shared<texture>( data::icon_keys, sizeof( data::icon_keys ) );
                    draw.textures[ GUI_HASH( "icon_info" ) ] = std::make_shared<texture>( data::icon_info, sizeof( data::icon_info ) );
                    draw.textures[ GUI_HASH( "icon_close" ) ] = std::make_shared<texture>( data::icon_close, sizeof( data::icon_close ) );
                    draw.textures[ GUI_HASH( "icon_load" ) ] = std::make_shared<texture>( data::icon_load, sizeof( data::icon_load ) );
                    draw.textures[ GUI_HASH( "icon_import" ) ] = std::make_shared<texture>( data::icon_import, sizeof( data::icon_import ) );
                    draw.textures[ GUI_HASH( "icon_export" ) ] = std::make_shared<texture>( data::icon_export, sizeof( data::icon_export ) );
                    draw.textures[ GUI_HASH( "icon_delete" ) ] = std::make_shared<texture>( data::icon_delete, sizeof( data::icon_delete ) );
                    draw.textures[ GUI_HASH( "icon_autoload" ) ] = std::make_shared<texture>( data::icon_autoload, sizeof( data::icon_autoload ) );
                    draw.textures[ GUI_HASH( "icon_allow_insecure" ) ] = std::make_shared<texture>( data::icon_allow_insecure, sizeof( data::icon_allow_insecure ) );
                    draw.textures[ GUI_HASH( "icon_cloud_upd" ) ] = std::make_shared<texture>( data::icon_cloud_upd, sizeof( data::icon_cloud_upd ) );
                  
                    // user
                    ctx->user.username = ( "developer" );
                    ctx->user.active_until = ( "Never" );

                    const auto window_sz = vec2{ 800.f, 530.f };
                    const auto wnd = evo::gui::helpers::make_window(( "wnd_main" ), { 50.f, 50.f }, { 848.f, 588.f }, [ ]( std::shared_ptr<layout>& e ) {
                        e->add( MAKE_TAB( "rage", "RAGE" )->make_selected( ) );
                        e->add( MAKE_TAB( "legit", "LEGIT" ) );
                        e->add( MAKE_TAB( "visuals", "VISUALS" ) );
                        e->add( MAKE_TAB( "misc", "MISC" ) );
                        e->add( MAKE_TAB( "scripts", "LUA" ) );
                        e->add( MAKE_TAB( "skins", "SKINS" ) );
                    }, [ & ]( std::shared_ptr<layout>& e ) {
						e->add( make_tab_area( GUI_HASH( "rage_container" ), true, s_none, menu::group::rage_tab ) );
                        const auto group_sz = window_size_to_groupbox_size(848.f, 588.f);
                        const auto half = group_sz * vec2{1.f, .5f} - vec2{0.f, 5.f};
                        e->add(make_tab_area(GUI_HASH("legit_container"), false, s_none, menu::group::legit_tab));
                        e->add(make_tab_area(GUI_HASH("visuals_container"), false, s_none, menu::group::visuals_tab));
                        e->add(make_tab_area(GUI_HASH("misc_container"), false, [&](const std::shared_ptr<layout>& e) {
                            e->add(menu::tools::make_stacked_groupboxes(GUI_HASH("misc>main,other"), group_sz, {
                                make_groupbox("misc>various", "MISC", half + vec2{0.f, 20.f}, menu::group::misc_various),
                                make_groupbox("misc>other", "OTHER", half - vec2{0.f, 20.f}, menu::group::misc_other)}));
                            e->add(menu::tools::make_stacked_groupboxes(GUI_HASH("misc>movement,extra"), group_sz, {
                                make_groupbox("misc>movement", "MOVEMENT", half, menu::group::misc_movement),
                                make_groupbox("misc>movement>extra", "MOVEMENT EXTRA", half, menu::group::misc_movement_extra)}));
                            e->add(make_groupbox("misc>buy bot", "BUY BOT", group_sz, menu::group::misc_buybot));
                        }));
                        e->add(make_tab_area(GUI_HASH("scripts_container"), false, [](const std::shared_ptr<layout>&) {}));
                        e->add(make_tab_area(GUI_HASH("skins_container"), false, s_horizontal, [&](const std::shared_ptr<layout>& e) {
                            e->stack_offset.x = 10.f;
                            e->add(menu::tools::make_stacked_groupboxes(GUI_HASH("skins_preview_agents"), group_sz, {
                                make_groupbox("skins>preview", "PREVIEW", vec2{group_sz.x, 330.f}, [](auto e) {
                                    e->add(menu::group::make_model_preview("skins>preview>viewport", vec2{220.f, 292.f}));
                                }),
                                make_groupbox("skins>equipment", "EQUIPMENT", vec2{group_sz.x, group_sz.y - 340.f}, menu::group::skins_equipment_tiles)}));
                            e->add(make_groupbox("skins>weapon grid", "WEAPONS", vec2{group_sz.x * 2.f + 6.f, group_sz.y}, menu::group::skins_weapon_grid));
                        }));
                        }, [ & ]( std::shared_ptr<layout>& e) {
							{
								const auto s = MAKE( "tools>search", button, "", ctx->res.icons.search )->adjust_margin( { 0.f, 0.f, 8.f, 0.f } )->adjust_size( { 20.f, 20.f } )->as<button>( );
								s->callback = [ ]( ) {
									static const auto s = ctx->find( GUI_HASH( "tools>search" ) );
									const auto p = std::make_shared<search_popup>( control_id{ GUI_HASH( "search" ) }, s->pos_abs( ) - vec2{ 0.f, 200.f } );
									p->open( );
									};
								s->tooltip = XOR( "Search" );
								s->disable_id_display = true;
								e->add( s );
							}
							{
								const auto s = MAKE( "tools>save", button, "", draw.textures[ GUI_HASH( "icon_save" ) ] )->adjust_margin( { 0.f, 0.f, 8.f, 0.f } )->adjust_size( { 20.f, 20.f } )->
									as<button>( );
								s->tooltip = XOR( "Quicksave" );
								//s->callback = on_quicksave;
								s->disable_id_display = true;
								e->add( s );
							}
							{
								const auto s = MAKE( "tools>configs", button, "", draw.textures[ GUI_HASH( "icon_configs" ) ] )->adjust_margin( { 0.f, 0.f, 8.f, 0.f } )->adjust_size(
									{ 20.f, 20.f } )->as<button>( );
								
								s->tooltip = XOR( "Configs" );
								s->disable_id_display = true;
								e->add( s );
							}
							/* TODO: {
								const auto s = MAKE("tools>keys", button, "", draw.textures[GUI_HASH("icon_keys")])->adjust_margin({ 0.f, 0.f, 8.f, 0.f })->adjust_size({ 20.f, 20.f })->as<button>();
								s->callback = []()
								{
									static const auto s = ctx->find(GUI_HASH("tools>hotkeys"));
									const auto p = std::make_shared<hotkey_overview_popup>(control_id{ GUI_HASH("hotkey_overview_popup") });
									p->open();
								};
								s->tooltip = XOR("Hotkeys");
								e->add(s);
							}*/
							{
								const auto s = MAKE( "tools>info", button, "", draw.textures[ GUI_HASH( "icon_info" ) ] )->adjust_margin( { 0.f, 0.f, 8.f, 0.f } )->adjust_size( { 20.f, 20.f } )->
									as<button>( );
								
								s->disable_id_display = true;
								s->tooltip = XOR( "About" );
								e->add( s );
							}
							} );

                   //wnd->is_visible = true;
                   // wnd->is_taking_input = true;

                    wnd->is_visible = wnd->is_taking_input = ctx->should_render_cursor = false;
                    ctx->add( wnd );

                    main_wnd = wnd;
                    menu::men.main_wnd = wnd;
                }

                draw.refresh( );
                draw.scale = menu::dpi_scale(*cfg.misc.dpi_scale, draw.display.y);
                draw.create_objects( );


        gui_device = device;
        objects_ready = true;
        ctx->reset();
        LOG_INFO("Fatality source menu initialized");
        return true;
    } catch (const std::exception& e) { LOG_ERROR("Fatality init failed: %s", e.what()); }
      catch (...) { LOG_ERROR("Fatality init failed"); }
    return false;
}
void Render(IDirect3DDevice9* device, HWND hwnd, bool open) {
    std::lock_guard<std::recursive_mutex> lock(gui_mutex);
    if (!gui_device && !Initialize(device, hwnd)) return;
    if (device != gui_device || !objects_ready) return;
    auto wnd = main_wnd.lock();
    if (!wnd || !ctx) return;
    if (!open) FatalityPreviewBridge::visible.store(false, std::memory_order_relaxed);
    if (wnd->is_visible != open) {
        wnd->is_visible = wnd->is_taking_input = open;
        ctx->should_render_cursor = open;
        if (!open) { ctx->active = nullptr; for (const auto& p : ctx->active_popups) p->close(); }
    }
    using clock = std::chrono::steady_clock;
    static const auto start = clock::now();
    static auto previous = start;
    const auto now = clock::now();
    draw.frame_time = std::chrono::duration<float>(now - previous).count();
    draw.time = std::chrono::duration<float>(now - start).count();
    previous = now;
    draw.refresh();
    const float new_scale = menu::dpi_scale(*cfg.misc.dpi_scale, draw.display.y);
    if (new_scale != draw.scale) {
        // Apply geometry, input and font metrics together, before building a frame.
        for (const auto& [id, font] : draw.fonts) {
            if (font) font->destroy();
        }
        draw.scale = new_scale;
        for (const auto& [id, font] : draw.fonts) {
            if (font) font->create();
        }
        const auto available = draw.display / draw.scale;
        wnd->pos.x = std::clamp(wnd->pos.x, 0.f, std::max(0.f, available.x - wnd->size.x));
        wnd->pos.y = std::clamp(wnd->pos.y, 0.f, std::max(0.f, available.y - wnd->size.y));
        for (const auto& p : ctx->active_popups) p->close();
    }
    FatalityPreviewBridge::visible.store(false, std::memory_order_relaxed);
    FatalityPreviewBridge::weapon_editor_visible.store(false, std::memory_order_relaxed);
    draw.begin_frame();
    ctx->render();
    draw.end_frame();
    draw.render();
    draw.flush();
}
bool Input(UINT msg, WPARAM wp, LPARAM lp, bool open) {
    std::lock_guard<std::recursive_mutex> lock(gui_mutex);
    if (!ctx || !gui_device) return false;
    ctx->should_process_hotkeys = !open;
    return ctx->refresh(msg, static_cast<uint32_t>(wp), static_cast<uint32_t>(lp)) && open;
}
void BeforeReset(IDirect3DDevice9* device) {
    std::lock_guard<std::recursive_mutex> lock(gui_mutex);
    if (device == gui_device && objects_ready) { draw.destroy_objects(); objects_ready = false; }
}
void AfterReset(IDirect3DDevice9* device, HRESULT result) {
    std::lock_guard<std::recursive_mutex> lock(gui_mutex);
    if (device == gui_device && !objects_ready && SUCCEEDED(result)) { draw.refresh(); draw.create_objects(); objects_ready = true; }
}
}
