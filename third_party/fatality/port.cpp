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
	void rage_aimbot_general( std::shared_ptr<layout>& e ) {
		//VIRTUALIZER_TIGER_WHITE_START;
		ADD( "Aimbot", "rage>aimbot>general>aimbot", checkbox, cfg.aim.enabled );
		ADD( "Silent", "rage>aimbot>general>silent", checkbox, cfg.aim.silent );
		ADD( "Maximum fov", "rage>aimbot>general>maximum fov", slider<float>, 0.f, 180.f, cfg.aim.fov, XOR( "%.0f deg" ), 1.f, vec2{ 80.f, 14.f } );
		ADD_INLINE(
			"Visualize aimbot", MAKE( "rage>aimbot>general>visualize aimbot", checkbox, cfg.aim.visualize ),
			MAKE( "rage>aimbot>general>visualize aimbot>settings", settings, [ ]( auto e ) { ADD( "Safe color", "rage>aimbot>general>visualize aimbot>settings>safe color",
																								  color_picker, cfg.aim.visualize_color_safe ); ADD( "Aim color", "rage>aimbot>general>visualize aimbot>settings>aim color", color_picker, cfg.aim .
																																					 visualize_color ); } ) );

		ADD( "Anti-exploit", "rage>aimbot>general>anti-exploit", checkbox, cfg.aim.anti_exploit );
		ADD( "Target dormant", "rage>aimbot>general>target dormant", checkbox, cfg.aim.dormant );
		ADD( "Autofire", "rage>aimbot>general>autofire", checkbox, cfg.aim.autofire );
		ADD( "Hide shot", "rage>aimbot>general>hide shot", checkbox, cfg.aim.hideshot );
		ADD( "Double tap", "rage>aimbot>general>double tap", checkbox, cfg.aim.doubletap );

		{
			const auto c = MAKE( "rage>aimbot>general>defensive lag", combo_box, cfg.aim.dt_exploit, vec2{}, vec2{ 80.f, 20.f } );
			c->add(
				{ MAKE( "rage>aimbot>general>defensive lag>none", selectable, XOR( "None" ), cfg_t::def_none ), MAKE(
					 "rage>aimbot>general>defensive lag>in air", selectable, XOR( "In air" ), cfg_t::def_in_air ), MAKE(
					 "rage>aimbot>general>defensive lag>always on", selectable, XOR( "Always on" ), cfg_t::def_always_on ), } );

			ADD_INLINE( "Defensive lag", c );
		}

		{
			const auto c = MAKE( "rage>aimbot>general>defensive fake", combo_box, cfg.aa.dt_exploit_angles, vec2{}, vec2{ 80.f, 20.f } );
			c->add(
				{ MAKE( "rage>aimbot>general>defensive fake>none", selectable, XOR( "None" ), cfg_t::dfake_none ), MAKE(
					 "rage>aimbot>general>defensive fake>yaw", selectable, XOR( "Yaw" ), cfg_t::dfake_yaw ),
				 MAKE( "rage>aimbot>general>defensive fake>yaw flip", selectable, XOR( "Yaw flip" ), cfg_t::dfake_yaw_flip ),
				 MAKE( "rage>aimbot>general>defensive fake>quick yaw", selectable, XOR( "Quick yaw" ), cfg_t::dfake_quick_yaw ), MAKE(
					 "rage>aimbot>general>defensive fake>quick yaw flip", selectable, XOR( "Quick yaw flip" ), cfg_t::dfake_quick_yaw_flip ), } );

			ADD_INLINE( "Defensive fake", c );
		}

		ADD( "Delay shot", "rage>aimbot>general>delay shot", checkbox, cfg.aim.extend_peek );
		ADD( "Force extra safety", "rage>aimbot>general>force extra safety", checkbox, cfg.aim.fallback_on_key );
		ADD( "Force bodyaim", "rage>aimbot>general>force bodyaim", checkbox, cfg.aim.baim_on_key );
		ADD( "Headshot only", "rage>aimbot>general>headshot only", checkbox, cfg.aim.hsmode );
		ADD( "Knife bot", "rage>aimbot>general>knife bot", checkbox, cfg.aim.knife_bot );

		ADD_INLINE(
			"Zeus bot", MAKE( "rage>aimbot>general>zeus bot", checkbox, cfg.aim.zeus_bot ),
			MAKE( "rage>aimbot>general>zeus bot>settings", settings, [ ]( auto e ) { ADD( "Hitchance", "rage>aimbot>general>zeus bot>settings>hitchance", slider<float>, 0.f,
																						  100.f, cfg.aim.zeus_hc, XOR( "%.0f%%" ) ); } ) );

		{
			const auto c = MAKE( "rage>aimbot>general>resolver mode", combo_box, cfg.aim.resolver_mode, vec2{}, vec2{ 80.f, 20.f } );
			c->add(
				{ MAKE( "rage>aimbot>general>resolver mode>roll", selectable, XOR( "Roll" ), cfg_t::res_roll ), MAKE(
					 "rage>aimbot>general>resolver mode>roll disabled", selectable, XOR( "Roll disabled" ), cfg_t::res_roll_disabled ), } );

			ADD_INLINE( "Resolver mode", c );
		}

		{
			const auto c = MAKE( "rage>aimbot>general>low perf. mode", combo_box, cfg.aim.low_perf, vec2{}, vec2{ 80.f, 20.f } );
			c->allow_multiple = true;
			c->add(
				{ MAKE( "rage>aimbot>general>low perf. mode>limit targets", selectable, XOR( "Limit targets" ), cfg_t::low_perf_targets ), MAKE(
					 "rage>aimbot>general>low perf. mode>limit scan accuracy", selectable, XOR( "Limit scan accuracy" ), cfg_t::low_perf_scan_accuracy ), } );

			ADD_INLINE( "Low perf. mode", c );
		}
		//VIRTUALIZER_TIGER_WHITE_END;
	}



	
	void rage_antiaim_custom( std::shared_ptr<layout>& e ) { }

	void rage_aimbot_weapon( std::shared_ptr<layout>& e, uint32_t grp, const std::string& id ) {
		//VIRTUALIZER_TIGER_WHITE_START;
		ADD_RUNTIME( "Hit chance", id + XOR( "hit chance" ), slider<float>, 0.f, 100.f, cfg.weapon_config.hitchance[ ( int )grp ], XOR( "%.0f%%" ), 1.f, vec2{ 80.f, 14.f } );
		ADD_RUNTIME( "Pointscale", id + XOR( "pointscale" ), slider<float>, 0.f, 100.f, cfg.weapon_config.pointscale[ ( int )grp ], XOR( "%.0f%%" ), 1.f, vec2{ 80.f, 14.f } );
		ADD_RUNTIME(
			"Min damage", id + XOR( "min damage" ), slider<float>, 0.f, 125.f, cfg.weapon_config.mindmg[ ( int )grp ], ssa<float>{ ssv<float>{101.f, XOR( "HP + %.0f" )},
			ssv<float>{0.f, XOR( "%.0f hp" )} }, 1.f, vec2{ 80.f, 14.f } );

		{
			std::shared_ptr<settings> e_unshadow;
			{
				// shadow e
				auto e = MAKE_RUNTIME( id + XOR( "override>settings" ), settings, nullptr );
				ADD_RUNTIME( "Value", id + XOR( "override>settings>value" ), slider<float>, 0.f, 100.f, cfg.weapon_config.mindmg_override[ ( int )grp ], XOR( "%.0f hp" ) );
				e_unshadow = e;
			}

			ADD_INLINE( "Override", MAKE_RUNTIME( id + XOR( "override" ), checkbox, cfg.weapon_config.mindmg_override_enabled[ ( int )grp ] ), e_unshadow );
		}

		{
			const auto c = MAKE_RUNTIME( id + XOR( "hitboxes" ), combo_box, cfg.weapon_config.hitboxes[ ( int )grp ], vec2{}, vec2{ 80.f, 20.f } );
			c->allow_multiple = true;
			c->add(
				{ MAKE_RUNTIME( id + XOR( "hitboxes>head" ), selectable, XOR( "Head" ), cfg_t::hitboxes_head ), MAKE_RUNTIME(
					 id + XOR( "hitboxes>chest" ), selectable, XOR( "Chest" ), cfg_t::hitboxes_chest ),
				 MAKE_RUNTIME( id + XOR( "hitboxes>stomach" ), selectable, XOR( "Stomach" ), cfg_t::hitboxes_stomach ),
				 MAKE_RUNTIME( id + XOR( "hitboxes>arms" ), selectable, XOR( "Arms" ), cfg_t::hitboxes_arms ),
				 MAKE_RUNTIME( id + XOR( "hitboxes>legs" ), selectable, XOR( "Legs" ), cfg_t::hitboxes_legs ),
				 MAKE_RUNTIME( id + XOR( "hitboxes>feet" ), selectable, XOR( "Feet" ), cfg_t::hitboxes_feet ), } );

			ADD_INLINE( "Hitboxes", c );
		}

		{
			const auto c = MAKE_RUNTIME( id + XOR( "multipoint" ), combo_box, cfg.weapon_config.pointscales[ ( int )grp ], vec2{}, vec2{ 80.f, 20.f } );
			c->allow_multiple = true;
			c->add(
				{ MAKE_RUNTIME( id + XOR( "multipoint>head" ), selectable, XOR( "Head" ), cfg_t::hitboxes_head ), MAKE_RUNTIME(
					 id + XOR( "multipoint>chest" ), selectable, XOR( "Chest" ), cfg_t::hitboxes_chest ),
				 MAKE_RUNTIME( id + XOR( "multipoint>stomach" ), selectable, XOR( "Stomach" ), cfg_t::hitboxes_stomach ),
				 MAKE_RUNTIME( id + XOR( "multipoint>arms" ), selectable, XOR( "Arms" ), cfg_t::hitboxes_arms ),
				 MAKE_RUNTIME( id + XOR( "multipoint>legs" ), selectable, XOR( "Legs" ), cfg_t::hitboxes_legs ), MAKE_RUNTIME(
					 id + XOR( "multipoint>feet" ), selectable, XOR( "Feet" ), cfg_t::hitboxes_feet ), } );

			ADD_INLINE( "Multipoint", c );
		}
		//VIRTUALIZER_TIGER_WHITE_END;
	}

	void rage_aimbot_extra( std::shared_ptr<layout>& e, uint32_t grp, const std::string& id ) {
		//VIRTUALIZER_TIGER_WHITE_START;
		{
			std::shared_ptr<settings> e_unshadow;
			{
				auto e = MAKE_RUNTIME( id + XOR( "autostop>settings" ), settings, nullptr );

				const auto c = MAKE_RUNTIME( id + XOR( "autostop>settings>mode" ), combo_box, cfg.weapon_config.autostop_mode[ ( int )grp ] );
				c->allow_multiple = true;
				c->add(
					{ MAKE_RUNTIME( id + XOR( "autostop>settings>mode>between shots" ), selectable, XOR( "Between shots" ), cfg_t::autostop_inbetween ), MAKE_RUNTIME(
						 id + XOR( "autostop>settings>mode>early" ), selectable, XOR( "Early" ), cfg_t::autostop_predictive ), MAKE_RUNTIME(
						 id + XOR( "autostop>settings>mode>only when lethal" ), selectable, XOR( "Only when lethal" ), cfg_t::autostop_lethal ), } );

				ADD_INLINE( "Mode", c );

				e_unshadow = e;
			}

			ADD_INLINE( "Autostop", MAKE_RUNTIME( id + XOR( "autostop" ), checkbox, cfg.weapon_config.autostop[ ( int )grp ] ), e_unshadow );
		}

		/*const auto should_add_autoscope = util::in_array(grp, {
															 general_id, group_id + 4, group_id + 6, group_id + 7,
															 WEAPON_AUG, WEAPON_SG556, WEAPON_AWP, WEAPON_G3SG1,
															 WEAPON_SCAR20, WEAPON_SSG08
														 });

		if (should_add_autoscope)
		{
			ADD_RUNTIME("Autoscope", id + XOR("autoscope"), checkbox, cfg.weapon_config.autoscope[(int)grp]);
		}*/

		{
			const auto c = MAKE_RUNTIME( id + XOR( "safepoint mode" ), combo_box, cfg.weapon_config.fallback_mode[ ( int )grp ], vec2{}, vec2{ 80.f, 20.f } );
			c->add(
				{ MAKE_RUNTIME( id + XOR( "safepoint mode>none" ), selectable, XOR( "None" ), cfg_t::fallback_off ), MAKE_RUNTIME(
					 id + XOR( "safepoint mode>adaptive" ), selectable, XOR( "Adaptive" ), cfg_t::fallback_adaptive ), MAKE_RUNTIME(
					 id + XOR( "safepoint mode>prefer safepoint" ), selectable, XOR( "Prefer safepoint" ), cfg_t::fallback_prefer_safepoint ), } );

			ADD_INLINE( "Safepoint mode", c );
		}

		{
			const auto c = MAKE_RUNTIME( id + XOR( "safety" ), combo_box, cfg.weapon_config.force_safe[ ( int )grp ], vec2{}, vec2{ 80.f, 20.f } );
			c->allow_multiple = true;
			c->add(
				{ MAKE_RUNTIME( id + XOR( "safety>lethal" ), selectable, XOR( "Lethal" ), cfg_t::safety_lethal ), MAKE_RUNTIME(
					 id + XOR( "safety>in air target" ), selectable, XOR( "In air target" ), cfg_t::safety_air ),
				 MAKE_RUNTIME( id + XOR( "safety>moving target" ), selectable, XOR( "Moving target" ), cfg_t::safety_moving ),
				 MAKE_RUNTIME( id + XOR( "safety>standing target" ), selectable, XOR( "Standing target" ), cfg_t::safety_standing ),
				 MAKE_RUNTIME( id + XOR( "safety>limbs" ), selectable, XOR( "Limbs" ), cfg_t::safety_limbs ), MAKE_RUNTIME(
					 id + XOR( "safety>on override damage" ), selectable, XOR( "On override damage" ), cfg_t::safety_override ), } );

			ADD_INLINE( "Safety", c );
		}

		{
			const auto c = MAKE_RUNTIME( id + XOR( "extra safety" ), combo_box, cfg.weapon_config.extra_safety[ ( int )grp ], vec2{}, vec2{ 80.f, 20.f } );
			c->allow_multiple = true;
			c->add(
				{ MAKE_RUNTIME( id + XOR( "extra safety>lethal" ), selectable, XOR( "Lethal" ), cfg_t::safety_lethal ), MAKE_RUNTIME(
					 id + XOR( "extra safety>in air target" ), selectable, XOR( "In air target" ), cfg_t::safety_air ),
				 MAKE_RUNTIME( id + XOR( "extra safety>moving target" ), selectable, XOR( "Moving target" ), cfg_t::safety_moving ),
				 MAKE_RUNTIME( id + XOR( "extra safety>standing target" ), selectable, XOR( "Standing target" ), cfg_t::safety_standing ),
				 MAKE_RUNTIME( id + XOR( "extra safety>limbs" ), selectable, XOR( "Limbs" ), cfg_t::safety_limbs ), MAKE_RUNTIME(
					 id + XOR( "extra safety>on override damage" ), selectable, XOR( "On override damage" ), cfg_t::safety_override ), } );

			ADD_INLINE( "Extra safety", c );
		}

		{
			const auto c = MAKE_RUNTIME( id + XOR( "bodyaim" ), combo_box, cfg.weapon_config.baim[ ( int )grp ], vec2{}, vec2{ 80.f, 20.f } );
			c->allow_multiple = true;
			c->add(
				{ MAKE_RUNTIME( id + XOR( "bodyaim>lethal" ), selectable, XOR( "Lethal" ), cfg_t::safety_lethal ), MAKE_RUNTIME(
					 id + XOR( "bodyaim>in air target" ), selectable, XOR( "In air target" ), cfg_t::safety_air ),
				 MAKE_RUNTIME( id + XOR( "bodyaim>moving target" ), selectable, XOR( "Moving target" ), cfg_t::safety_moving ),
				 MAKE_RUNTIME( id + XOR( "bodyaim>standing target" ), selectable, XOR( "Standing target" ), cfg_t::safety_standing ), MAKE_RUNTIME(
					 id + XOR( "bodyaim>high risk" ), selectable, XOR( "High risk" ), cfg_t::safety_risky ), } );

			ADD_INLINE( "Bodyaim", c );
		}

		ADD_RUNTIME( "Ignore limbs on moving", id + XOR( "ignore limbs on moving" ), checkbox, cfg.weapon_config.ignore_limbs[ ( int )grp ] );

		/*if (util::in_array(grp, {general_id, group_id + 7, WEAPON_SSG08}))
			ADD_RUNTIME("Jumpscout", id + XOR("jumpscout"), checkbox, cfg.weapon_config.scout_jumpshot[(int)grp]);
		if (util::in_array(grp, {general_id, group_id + 2, WEAPON_REVOLVER}))
			ADD_RUNTIME("Autorevolver", id + XOR("autorevolver"), checkbox, cfg.weapon_config.autorevolver[(int)grp]);*/

			//VIRTUALIZER_TIGER_WHITE_END;
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






	void rebuild_rage_layout( bool skip_adjust ) {
		//VIRTUALIZER_TIGER_WHITE_START;
		const auto wp_tabs = ctx->find<tabs_layout>( GUI_HASH( "rage>weapons" ) );
		const auto wp_area = ctx->find<layout>( GUI_HASH( "rage>tab>aimbot" ) );

		// remove spacers
		wp_tabs->remove( GUI_HASH( "rage>weapons>spacer" ) );
		wp_tabs->remove( GUI_HASH( "rage>weapons>spacer2" ) );

		bool any_added{};
		std::vector<uint32_t> to_add_grp, to_add_wep;
		//for (auto &[i, v] : cfg.weapon_config.cfgs)
		//{
		//	const auto &name = i >= group_id ? grp_to_name[i] : wpn_to_name[i];
		//	any_added = any_added || v;

		//	// erase everything!
		//	wp_tabs->remove(hash(XOR("rage>weapons>") + name));
		//	wp_area->remove(hash(XOR("rage>weapon>") + name));

		//	// add new ones
		//	if (v)
		//		i >= group_id ? to_add_grp.push_back(i) : to_add_wep.push_back(i);
		//}

		// process queues here so it doesnt delete controls right after they are created
		wp_tabs->process_queues( );
		wp_area->process_queues( );

		if ( to_add_grp.empty( ) && to_add_wep.empty( ) )
			return;

		// sort both arrays ascending
		std::sort( to_add_grp.begin( ), to_add_grp.end( ) );
		std::sort( to_add_wep.begin( ), to_add_wep.end( ) );

		// add spacer
		wp_tabs->add( MAKE( "rage>weapons>spacer", spacer, vec2{ 0.f, 0.f }, vec2{ 0.f, 8.f } )->make_width_automatic( ) );

		//for (auto &i : to_add_grp)
		//{
		//	const auto &name = grp_to_name[i];

		//	// make tab
		//	const auto tb = MAKE_RUNTIME("rage>weapons>" + name, child_tab, name, hash(XOR("rage>weapon>") + name))->
		//		make_vertical();
		//	if (!skip_adjust)
		//		tb->margin.maxs.y = -8.f;

		//	wp_tabs->add(tb);
		//	rage_aimbot_weapon_extra(wp_area, i, XOR("rage>weapon>") + name);
		//}

		if ( !to_add_grp.empty( ) && !to_add_wep.empty( ) )
			wp_tabs->add( MAKE( "rage>weapons>spacer2", spacer, vec2{ 0.f, 0.f }, vec2{ 0.f, 8.f } )->make_width_automatic( ) );

		/*for (auto &i : to_add_wep)
		{
			const auto &name = wpn_to_name[i];

			const auto tb = MAKE_RUNTIME("rage>weapons>" + name, child_tab, name, hash(XOR("rage>weapon>") + name))->
				make_vertical();
			if (!skip_adjust)
				tb->margin.maxs.y = -8.f;

			wp_tabs->add(tb);
			rage_aimbot_weapon_extra(wp_area, i, XOR("rage>weapon>") + name);
		}*/

		wp_tabs->process_queues( );
		wp_area->process_queues( );

		// force select general no matter what
		ctx->find<child_tab>( GUI_HASH( "rage>weapons>general" ) )->select( );
		//VIRTUALIZER_TIGER_WHITE_END;
	}

	
	void rage_aimbot( std::shared_ptr<layout>& e ) {
		//VIRTUALIZER_TIGER_WHITE_START;
		auto group_sz = window_size_to_groupbox_size( 848.f, 588.f, 4.f ) - vec2{ 0.f, 36.f };

		const auto tabs = MAKE( "rage>weapons", tabs_layout, vec2{ 0.f, 36.f }, vec2{ group_sz.x * .5f, 406.f }, td_vertical );
		tabs->add( MAKE( "rage>weapons>general", child_tab, XOR( "General" ), GUI_HASH( "rage>weapon>general" ) )->make_vertical( )->make_selected( ) );

		auto area = MAKE( "rage>tab>aimbot", layout, vec2{ group_sz.x * .5f + 16.f, 36.f }, vec2{ 800.f - ( group_sz.x * .5f + 16.f ), 446.f }, s_none );
		area->make_not_clip( );

		group_sz.x = 218.f;
		rage_aimbot_weapon_extra( area, 0, XOR( "rage>weapon>general" ), true );

		area->add( make_groupbox( XOR( "rage>aimbot>general" ), XOR( "GENERAL" ), { group_sz.x * 2 + 32.f, 0.f }, group_sz, rage_aimbot_general ) );

		const auto ovr = MAKE( "rage>override", button, XOR( "Add override" ), nullptr, vec2{ 0.f, 462.f }, vec2{ group_sz.x * .5f - 16.f, 20.f } );
		ovr->margin = {};
		ovr->center_text = false;
		//ovr->callback = []()
		//{
		//	const auto ovr = ctx->find(GUI_HASH("rage>override"));

		//	const auto ovr_pick = std::make_shared<action_list_popup>(control_id{GUI_HASH("action_list_popup")},
		//															  action_list_popup::params_t{
		//																  grp_to_name, wpn_to_name},
		//															  cfg.weapon_config.cfgs,
		//															  ovr->area_abs().bl() + vec2{0.f, 4.f});
		//	ovr_pick->open();
		//	ovr_pick->callback = [](auto i, auto v)
		//	{
		//		// only derive if enabled
		//		if (v)
		//			derive_weapon_setting(i);

		//		rebuild_rage_layout();
		//	};
		//};

		e->add( tabs );
		e->add( area );
		e->add( ovr );
		//VIRTUALIZER_TIGER_WHITE_END;
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
                }

                draw.refresh( );
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
