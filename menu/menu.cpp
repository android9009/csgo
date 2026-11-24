#include "preview/bridge.hpp"
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

#include "../sdk/third_party/gui/resources/cursor.h"
#include "../sdk/third_party/gui/resources/icon_add.h"
#include "../sdk/third_party/gui/resources/icon_clear.h"
#include "../sdk/third_party/gui/resources/icon_cloud.h"
#include "../sdk/third_party/gui/resources/icon_copy.h"
#include "../sdk/third_party/gui/resources/icon_down.h"
#include "../sdk/third_party/gui/resources/icon_legit.h"
#include "../sdk/third_party/gui/resources/icon_misc.h"
#include "../sdk/third_party/gui/resources/icon_paste.h"
#include "../sdk/third_party/gui/resources/icon_rage.h"
#include "../sdk/third_party/gui/resources/icon_refresh.h"
#include "../sdk/third_party/gui/resources/icon_search.h"
#include "../sdk/third_party/gui/resources/icon_skins.h"
#include "../sdk/third_party/gui/resources/icon_scripts.h"
#include "../sdk/third_party/gui/resources/icon_up.h"
#include "../sdk/third_party/gui/resources/icon_visuals.h"
#include "../sdk/third_party/gui/resources/icon_save.h"
#include "../sdk/third_party/gui/resources/icon_configs.h"
#include "../sdk/third_party/gui/resources/icon_keys.h"
#include "../sdk/third_party/gui/resources/icon_info.h"
#include "../sdk/third_party/gui/resources/icon_close.h"
#include "../sdk/third_party/gui/resources/icon_settings.h"
#include "../sdk/third_party/gui/resources/icon_load.h"
#include "../sdk/third_party/gui/resources/icon_import.h"
#include "../sdk/third_party/gui/resources/icon_export.h"
#include "../sdk/third_party/gui/resources/icon_delete.h"
#include "../sdk/third_party/gui/resources/icon_bug.h"
#include "../sdk/third_party/gui/resources/icon_file.h"
#include "../sdk/third_party/gui/resources/icon_autoload.h"
#include "../sdk/third_party/gui/resources/logo_head.h"
#include "../sdk/third_party/gui/resources/logo_stripes.h"
#include "../sdk/third_party/gui/resources/icon_allow_insecure.h"
#include "../sdk/third_party/gui/resources/icon_cloud_upd.h"
#include "../sdk/third_party/gui/resources/loading.h"
#include "../sdk/third_party/gui/resources/hotkey_icons.h"

#include "../sdk/third_party/gui/src/gui/popups/about_popup.h"
#include "../sdk/third_party/gui/src/gui/popups/search_popup.h"
#include "../sdk/third_party/gui/src/gui/popups/configs_popup.h"
#include "../sdk/third_party/gui/src/gui/popups/hotkey_overview_popup.h"


#include <ren/adapters/adapter_dx9.h>
#include <ren/renderer.h>
#include <ren/types/animated_texture.h>
#include "config.hpp"
#include "menu.hpp"
#include "tabs/tabs.hpp"
#include "bridges/visual_runtime.hpp"
#include "bridges/misc_runtime.hpp"
#include "bridges/removals.hpp"
#include "overlays/player_esp_overlay.hpp"
#include "overlays/screen_overlay.hpp"
#include "overlays/fx_overlay.hpp"
#include <chrono>
#include <mutex>
#include <exception>
#include <cstring>
#include "../utils/log.hpp"
using namespace evo;
using namespace evo::ren;
using namespace evo::gui;
using namespace evo::gui::helpers;
static std::weak_ptr<evo::gui::window> main_wnd;
static std::recursive_mutex gui_mutex;
static IDirect3DDevice9* gui_device = nullptr;
static bool objects_ready = false;
cfg_t cfg;

// Diagnostics for the "menu is open (input captured) but invisible" case: every
// Render() early return is logged, at most once per second per condition, so the
// csgo_base.log shows which gate keeps the frame from reaching draw.render().
namespace {
struct RenderThrottle {
    bool fired = false;
    std::chrono::steady_clock::time_point last{};
    bool ok() {
        const auto now = std::chrono::steady_clock::now();
        if (fired && now - last < std::chrono::seconds(1)) return false;
        fired = true;
        last = now;
        return true;
    }
} g_diag_init, g_diag_device, g_diag_objects, g_diag_wnd;
}

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


namespace FatalityMenu {
bool Initialize(IDirect3DDevice9* device, HWND hwnd) {
    std::lock_guard<std::recursive_mutex> lock(gui_mutex);
    if (gui_device) return gui_device == device && objects_ready;
    if (!device || !hwnd) {
        if (g_diag_init.ok())
            LOG_WARN("menu init: no device/hwnd (device=%p hwnd=%p - FindWindowA(\"Valve001\") failed?)",
                     (void*)device, (void*)hwnd);
        return false;
    }
    // EndScene is also hooked on temporary probe devices during hook setup.
    // Do not bind the GUI to a probe rather than the actual game device.
    D3DDEVICE_CREATION_PARAMETERS params{};
    if (FAILED(device->GetCreationParameters(&params))) {
        if (g_diag_init.ok()) LOG_WARN("menu init: GetCreationParameters failed on device %p", (void*)device);
        return false;
    }
    if (params.hFocusWindow && !IsWindow(params.hFocusWindow)) {
        if (g_diag_init.ok()) LOG_WARN("menu init: device %p focus window %p is not a live window", (void*)device, (void*)params.hFocusWindow);
        return false;
    }
    char class_name[64]{};
    if (params.hFocusWindow && GetClassNameA(params.hFocusWindow, class_name, sizeof(class_name)) &&
        std::strcmp(class_name, "csgo_base_dummy") == 0) {
        if (g_diag_init.ok()) LOG_WARN("menu init: device %p is the dummy probe, skipped", (void*)device);
        return false;
    }
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
                    LOG_ERROR("menu init: vertex shader compilation failed: %s", err ? err : "(no log)");
                };

                draw.adapter->on_fragment_shader_compilation_error = [ ]( const char* err ) {
                    LOG_ERROR("menu init: fragment shader compilation failed: %s", err ? err : "(no log)");
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
                            e->add(menu::tools::make_stacked_groupboxes(GUI_HASH("misc>general,other"), group_sz, {
                                make_groupbox("misc>general", "MISC", half + vec2{0.f, 20.f}, menu::group::misc_general),
                                make_groupbox("misc>other", "OTHER", half - vec2{0.f, 20.f}, menu::group::misc_other)}));
                            e->add(menu::tools::make_stacked_groupboxes(GUI_HASH("misc>movement,extra"), group_sz, {
                                make_groupbox("misc>movement", "MOVEMENT", half, menu::group::misc_movement),
                                make_groupbox("misc>movement>extra", "MOVEMENT EXTRA", half, menu::group::misc_movement_extra)}));
                            e->add(make_groupbox("misc>buybot", "BUY BOT", group_sz, menu::group::misc_buybot));
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
        // Which device the GUI is bound to, and whether it is a D3D9Ex device:
        // the CS:GO main menu and in-game can present on different devices, and a
        // stale binding is the classic "input captured, nothing drawn" cause.
        // __uuidof: the IID symbol itself lives in d3d9.lib, which this project
        // does not link (all D3D calls go through the game's device vtable);
        // the uuid is taken from the interface declaration instead.
        bool is_ex = false;
        IDirect3DDevice9Ex* ex_probe = nullptr;
        if (SUCCEEDED(device->QueryInterface(__uuidof(IDirect3DDevice9Ex), reinterpret_cast<void**>(&ex_probe)))) {
            is_ex = true;
            ex_probe->Release();
        }
        char focus_class[64]{};
        if (params.hFocusWindow) GetClassNameA(params.hFocusWindow, focus_class, sizeof(focus_class));
        LOG_INFO("Fatality source menu initialized (device=%p ex=%d focus_class=%s)",
                 (void*)device, (int)is_ex, focus_class);
        return true;
    } catch (const std::exception& e) { LOG_ERROR("Fatality init failed: %s", e.what()); }
      catch (...) { LOG_ERROR("Fatality init failed"); }
    return false;
}
void Render(IDirect3DDevice9* device, HWND hwnd, bool open) {
    std::lock_guard<std::recursive_mutex> lock(gui_mutex);
    if (!gui_device && !Initialize(device, hwnd)) return;
    if (device != gui_device) {
        // The presenting device is not the one the GUI bound to: the CS:GO main
        // menu and in-game can present on different D3D9/D3D9Ex devices, and a
        // stale binding is exactly the "input captured, nothing drawn" case.
        // (A rebind needs the renderer's per-device objects rebuilt - see the
        // device pointers below when this line repeats in csgo_base.log.)
        if (g_diag_device.ok())
            LOG_WARN("menu render: presenting device %p != bound device %p - menu invisible on this device",
                     (void*)device, (void*)gui_device);
        return;
    }
    if (!objects_ready) {
        if (g_diag_objects.ok()) LOG_WARN("menu render: objects not ready (waiting for AfterReset)");
        return;
    }
    auto wnd = main_wnd.lock();
    if (!wnd || !ctx) {
        if (g_diag_wnd.ok())
            LOG_WARN("menu render: main window or context lost (wnd=%d ctx=%d)", (int)(wnd != nullptr), (int)(ctx != nullptr));
        return;
    }
    // per-frame setup: visibility, display scale, fonts
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
    // config bridges publish the menu state to the runtimes
    VisualSettings::Configure();
    MiscSettings::Configure();
    RemovalsSettings::Configure();
    // begin frame + screen overlays (ESP / scope / FX)
    draw.begin_frame();
    {
        // One-shot safety net: if a runtime snapshot lock is held by another
        // thread at frame start, reading the snapshots would block the game
        // thread - skip this frame's overlays instead of waiting. (With the
        // snapshot locks now recursive, a same-thread re-entrancy can no
        // longer throw the "resource deadlock would occur" that used to
        // abort every frame before a single pixel was drawn.)
        static bool probe_logged = false;
        auto is_free = [](auto& m) {
            if (m.try_lock()) { m.unlock(); return true; }
            return false;
        };
        const bool fx_free = is_free(VisualRuntime::fx_mutex);
        const bool screen_free = is_free(VisualRuntime::screen_mutex);
        const bool settings_free = is_free(VisualRuntime::mutex);
        if (!probe_logged && !(fx_free && screen_free && settings_free)) {
            probe_logged = true;
            LOG_ERROR("menu render: runtime lock held at frame start (fx=%d screen=%d settings=%d); overlays skipped this frame",
                      (int)fx_free, (int)screen_free, (int)settings_free);
        }
        if (fx_free && screen_free && settings_free) {
            PlayerEspOverlay::Render();
            ScreenOverlay::Render();
            FxOverlay::Render();
        }
    }
    // the menu body: tabs, widgets, popups (the windows are hidden while the
    // menu is closed, but the context ticks every frame as before)
    ctx->render();
    // submit: build the GPU buffers and draw
    draw.end_frame();
    draw.render();
    draw.flush();
}
bool Input(UINT msg, WPARAM wp, LPARAM lp, bool open) {
    std::lock_guard<std::recursive_mutex> lock(gui_mutex);
    if (!ctx || !gui_device) return false;
    ctx->should_process_hotkeys = !open;
    const bool capture_keys = ctx->active && ctx->active->is_visible &&
        (ctx->active->is_taking_keys || ctx->active->is_taking_text);
    const bool consumed = ctx->refresh(msg, static_cast<uint32_t>(wp), static_cast<uint32_t>(lp));
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) return open && capture_keys;
    return consumed && open;
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
