#pragma once
#include "../tabs/tabs.hpp"
#include "../config.hpp"
#include "../../sdk/visual_runtime_state.hpp"
#include "../../features/visuals/chams/chams_passes.hpp"
#include "../../features/visuals/misc/local/thirdperson.hpp"
#include "../../features/visuals/misc/local/fov.hpp"
#include "../../features/visuals/misc/local/viewmodel.hpp"
#include "../../features/visuals/misc/local/force_crosshair.hpp"
#include "../../features/visuals/misc/local/penetration_crosshair.hpp"
#include "../../features/visuals/misc/local/grenade_warning.hpp"
#include "../../features/visuals/misc/other/out_of_fov.hpp"
#include "../../features/visuals/misc/other/tracers.hpp"
#include "../../features/visuals/misc/other/impacts.hpp"
#include "../../features/visuals/misc/other/hitmarker.hpp"
#include "../../features/visuals/misc/world/world_postfx.hpp"
namespace VisualSettings {
using namespace evo::gui;
using namespace evo::ren;
inline std::array<VisualPreview::Layout,3> layouts{EspDock::Defaults(),EspDock::Defaults(),EspDock::Defaults()};
inline void SyncProfile() {
    auto& esp=cfg.visuals.esp;
    const int selected=std::clamp(static_cast<int>(cfg.visuals.preview.get()),0,2);
    if(esp.active_profile==selected)return;
    esp.profiles[esp.active_profile]=static_cast<cfg_t::visuals_t::esp_player_t&>(esp);
    layouts[esp.active_profile]=VisualPreview::GetLayout();
    static_cast<cfg_t::visuals_t::esp_player_t&>(esp)=esp.profiles[selected];
    esp.active_profile=selected;VisualPreview::SetLayout(layouts[selected]);
    // Control values stay at stable addresses; reset only native presentation.
    if(ctx)if(auto player=ctx->find(GUI_HASH("visuals>esp>player")))player->reset();
}
inline VisualRuntime::Esp ReadEsp(cfg_t::visuals_t::esp_player_t& e,int subject) {
    VisualRuntime::Esp s;
    s.show={e.enabled.get(),e.box.get(),e.skeleton.get(),e.history_skeleton.get(),e.name.get(),e.health.get(),e.armor.get(),e.ammo.get(),e.weapon.get(),e.weapon_icon.get(),e.sound_esp.get()};
    s.colors={e.box_color.get().rgba(),e.skeleton_color.get().rgba(),e.history_skeleton_color.get().rgba(),e.name_color.get().rgba(),e.health_color_1.get().rgba(),e.health_color_2.get().rgba(),e.armor_color.get().rgba(),e.ammo_color.get().rgba(),e.weapon_color.get().rgba(),e.weapon_icon_color.get().rgba(),e.sound_esp_color.get().rgba()};
    s.gradient={e.health_gradient.get(),e.armor_gradient.get(),e.ammo_gradient.get()};
    s.gradient_end={e.health_color_2.get().rgba(),e.armor_color_2.get().rgba(),e.ammo_color_2.get().rgba()};
    s.flags=static_cast<std::uint64_t>(e.flags.get());s.layout=subject==cfg.visuals.esp.active_profile ? VisualPreview::GetLayout() : layouts[subject];return s;
}
template<class T> inline VisualRuntime::Material Material(T& c) {
    VisualRuntime::Material s;s.enabled=true;s.kind=ChamsPasses::Selection(c.mat.get().first_set_bit().value_or(0));s.ignore_z=c.ignorez.get();
    s.primary=c.col1.get().rgba();s.secondary=c.col2.get().rgba();s.hidden=c.col_z.get().rgba();return s;
}
inline void Configure() {
    SyncProfile();
    auto& esp=cfg.visuals.esp;auto& c=cfg.visuals.chams;auto& w=esp.world;
    auto selection=[](auto& value){
        auto& bits=value.get();
        const int kind=ChamsPasses::Selection(bits.first_set_bit().value_or(0));
        bits=evo::gui::bits{1ull<<kind};
    };
    for(auto* entry:{&c.team_pl,&c.team_rag,&c.team_att,&c.enemy_pl,&c.enemy_backtrack,&c.enemy_shot,&c.enemy_rag,&c.enemy_att,&c.local_pl,&c.local_fake,&c.local_arms,&c.local_att})selection(entry->mat);
    VisualRuntime::Settings s;s.chams_enabled=c.enable.get();
    s.players={Material(c.enemy_pl),Material(c.team_pl),Material(c.local_pl)};
    s.ragdolls={Material(c.enemy_rag),Material(c.team_rag),Material(c.local_pl)};
    s.attachments={Material(c.enemy_att),Material(c.team_att),Material(c.local_att)};s.arms=Material(c.local_arms);s.shot=Material(c.enemy_shot);
    s.texture_transparency=cfg.misc.texture_transparency.get();
    s.prop_transparency=cfg.visuals.prop_transparency.get();
    // Remove scope / Spread. The bitset reports the selected option; the overlay wants the
    // index, so the first set bit is used (the combo is single-select).
    s.screen.scope_mode=static_cast<int>(cfg.misc.noscope.get().first_set_bit().value_or(0));
    s.screen.scope_line_color=cfg.misc.scope_line_color.get().rgba();
    s.screen.scope_line_size=cfg.misc.scope_line_size.get();
    s.screen.scope_invert=cfg.misc.scope_invert.get();
    s.screen.spread=cfg.visuals.spread_circle_type.get().first_set_bit().value_or(0)!=0;
    s.screen.spread_color1=cfg.visuals.spread_circle_color1.get().rgba();
    s.screen.spread_color2=cfg.visuals.spread_circle_color2.get().rgba();
    s.nightmode=cfg.visuals.world_effects.nightmode.get();
    s.nightmode_colour=cfg.visuals.world_effects.nightmode_color.get().rgba();
    s.sky=cfg.visuals.world_effects.sky.get();
    s.sky_colour=cfg.visuals.world_effects.sky_color.get().rgba();
    s.fullbright=cfg.visuals.world_effects.fullbright.get();
    s.sunlight=cfg.visuals.world_effects.sunlight.get();
    s.world_fog=cfg.visuals.world_effects.fog.get();
    s.world_fog_colour=cfg.visuals.world_effects.fog_color.get().rgba();
    s.player_glow={VisualRuntime::Highlight{esp.glow.enabled.get() && esp.glow.enemy.get(),esp.glow.enemy_color.get().rgba()},
        VisualRuntime::Highlight{esp.glow.enabled.get() && esp.glow.team.get(),esp.glow.team_color.get().rgba()},
        VisualRuntime::Highlight{esp.glow.enabled.get() && esp.glow.local.get(),esp.glow.local_color.get().rgba()}};
    auto object=[&](int index,auto& v,bool glow_enabled) {
        s.objects[index].kind=ChamsPasses::Selection(v.material.get().first_set_bit().value_or(0));s.objects[index].enabled=s.objects[index].kind>0;s.object_enabled[index]=s.objects[index].enabled;s.objects[index].primary=v.material_color.get().rgba();s.objects[index].secondary=s.objects[index].primary;
        s.object_glow[index]={glow_enabled,v.glow_color.get().rgba()};
    };
    object(0,w.weapon_visuals,w.weapon_glow.get());object(1,w.grenade_visuals,w.grenade_glow.get());object(2,w.c4_visuals,w.c4_glow.get());
    for(int i=0;i<3;++i)s.esp[i]=ReadEsp(i==esp.active_profile ? static_cast<cfg_t::visuals_t::esp_player_t&>(esp) : esp.profiles[i],i);
    // Visuals -> Misc -> LOCAL: "Thirdperson" has its own runtime (the same per-feature
    // pattern as the Misc tab); it is published here each frame and consumed in framestage.
    {
        ThirdPerson::Settings tp;
        tp.enabled = cfg.misc.thirdperson.get();
        tp.force_dead = cfg.misc.thirdperson_dead.get();
        tp.disable_grenade = cfg.misc.thirdperson_grenade.get();
        tp.distance = cfg.misc.thirdperson_dist.get();
        tp.scope_transparency = cfg.misc.transparency_in_scope.get();
        tp.nade_transparency = cfg.misc.transparency_nade.get();
        ThirdPerson::Publish(tp);
    }
    // Visuals -> Misc -> LOCAL: "Fov changer" (same per-feature pattern).
    {
        FovChanger::Settings fv;
        fv.enabled = cfg.misc.fov_enabled.get();
        fv.fov = static_cast<int>(cfg.misc.fov.get());
        fv.aspect = cfg.misc.aspect.get();
        fv.remove_zoom = cfg.misc.fov_scope.get();
        FovChanger::Publish(fv);
    }
    // Visuals -> Misc -> LOCAL: "Viewmodel changer" (vm_offset_* / v_viewmodel_fov cvars).
    {
        ViewModelChanger::Settings vm;
        vm.enabled = cfg.misc.vm_fov_enabled.get();
        vm.fov = cfg.misc.vm_fov.get();
        vm.x = cfg.misc.viewmodel_x.get();
        vm.y = cfg.misc.viewmodel_y.get();
        vm.z = cfg.misc.viewmodel_z.get();
        vm.roll = cfg.misc.viewmodel_roll.get();
        vm.yaw = cfg.misc.viewmodel_yaw.get();
        vm.pitch = cfg.misc.viewmodel_pitch.get();
        ViewModelChanger::Publish(vm);
    }
    // Visuals -> Misc -> LOCAL: the two crosshairs.
    {
        ForceCrosshair::Settings fc;
        fc.enabled = cfg.misc.force_crosshair.get();
        ForceCrosshair::Publish(fc);
        PenetrationCrosshair::Settings pc;
        pc.enabled = cfg.visuals.autowall_crosshair.get();
        PenetrationCrosshair::Publish(pc);
    }
    // Visuals -> Misc -> OTHER: "Out of FOV".
    {
        OutOfFov::Settings oof;
        oof.enabled = cfg.visuals.out_of_fov.get();
        oof.color = cfg.visuals.out_of_fov_col.get().rgba();
        oof.distance = cfg.visuals.out_of_fov_dst.get();
        oof.size = cfg.visuals.out_of_fov_size.get();
        OutOfFov::Publish(oof);
    }
    // Visuals -> Misc -> OTHER: "Bullet tracers" (per-target colour and style).
    {
        Tracers::Settings tr;
        for (int i = 0; i < Tracers::Target::Count; ++i) {
            tr.enabled[i] = cfg.misc.tracer_targets.get().test(i);
            tr.color[i] = cfg.misc.tracer_colors[i].get().rgba();
            tr.style[i] = static_cast<int>(cfg.misc.tracer_style[i].get().first_set_bit().value_or(0));
        }
        Tracers::Publish(tr);
    }
    // Visuals -> Misc -> OTHER: "Show impacts".
    {
        Impacts::Settings im;
        im.enabled = cfg.misc.impacts.get();
        im.color1 = cfg.misc.impacts_color.get().rgba();
        im.color2 = cfg.misc.impacts_color2.get().rgba();
        Impacts::Publish(im);
    }
    // Visuals -> Misc -> OTHER: "Hitmarker" (the bitset is keyed by cfg_t::hitmarker_*).
    {
        Hitmarker::Settings hm;
        const auto bits = cfg.misc.hitmarker.get();
        hm.world = bits.test(cfg_t::hitmarker_world);
        hm.screen = bits.test(cfg_t::hitmarker_cross);
        hm.kibit = bits.test(cfg_t::hitmarker_kibit);
        hm.world_color = cfg.misc.hitmarker_world_color.get().rgba();
        hm.screen_color = cfg.misc.hitmarker_screen_color.get().rgba();
        hm.kibit_color = cfg.misc.hitmarker_kibit_color.get().rgba();
        hm.world_duration = cfg.misc.hitmarker_duration[cfg_t::hitmarker_world].get();
        hm.screen_duration = cfg.misc.hitmarker_duration[cfg_t::hitmarker_cross].get();
        hm.kibit_duration = cfg.misc.hitmarker_duration[cfg_t::hitmarker_kibit].get();
        Hitmarker::Publish(hm);
    }
    // Visuals -> Misc -> LOCAL: "Grenade prediction" (trajectory + damage/distance
    // labels) and "Grenade proximity warning" (the edge disc). The trajectory's
    // base colour is the miss colour; its hit/miss menu colours are reused from the
    // bullet-tracer pair (the Lua script's own trajectory colour picker maps here).
    {
        GrenadeWarning::Settings gw;
        gw.prediction = cfg.misc.grenade_tracer.get();
        gw.prediction_hit_color = cfg.misc.tracer_color.get().rgba();
        gw.prediction_miss_color = cfg.misc.box_color.get().rgba();
        gw.warning = cfg.misc.grenade_helper.get();
        gw.warning_color = cfg.misc.grenade_warning_color.get().rgba();
        GrenadeWarning::Publish(gw);
    }
    // Visuals -> World: the post-processing block. Exposure/Bloom ride the map's
    // env_tonemap_controller entity; the rest are cvar writes. The module applies them on
    // the framestage (before the original) and hands everything back on level change /
    // unload, so publishing here each frame is all the wiring it needs.
    {
        WorldPostfx::Settings pf;
        auto& w = cfg.visuals.world_effects;
        pf.exposure_enabled = w.exposure_enabled.get();
        pf.exposure_amount = w.exposure.get();
        pf.bloom_enabled = w.bloom.get();
        pf.bloom_strength = w.bloom_strength.get();
        pf.local_contrast_enabled = w.local_contrast.get();
        pf.contrast_strength = w.contrast_strength.get();
        pf.contrast_edge_strength = w.contrast_edge_strength.get();
        pf.color_correction_enabled = w.color_correction.get();
        pf.tone_curve = static_cast<int>(w.tone_curve.get().first_set_bit().value_or(0));
        pf.grayscale = w.grayscale.get();
        pf.dof_enabled = w.depth_of_field.get();
        pf.dof_mode = static_cast<int>(w.dof_mode.get().first_set_bit().value_or(0));
        pf.focus_distance = w.focus_distance.get();
        pf.focus_speed = w.focus_speed.get();
        pf.focus_range = w.focus_range.get();
        pf.weather = static_cast<int>(w.weather.get().first_set_bit().value_or(0));
        WorldPostfx::Publish(pf);
    }
    VisualRuntime::Publish(s);
}
}
