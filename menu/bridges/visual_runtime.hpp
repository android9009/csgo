#pragma once
#include "../tabs/tabs.hpp"
#include "../config.hpp"
#include "../../sdk/visual_runtime_state.hpp"
#include "../../features/visuals/chams_passes.hpp"
#include "../../features/visuals/misc/misc.hpp"
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
    VisualRuntime::Publish(s);

    // Visuals -> Misc (third person and the two fov controls). Same rule as everywhere
    // else: the hooks never read GUI state, they read what this function published.
    VisualsMisc::Settings misc;
    misc.thirdperson=cfg.misc.thirdperson.get();
    misc.thirdperson_dist=cfg.misc.thirdperson_dist.get();
    misc.thirdperson_dead=cfg.misc.thirdperson_dead.get();
    misc.thirdperson_grenade=cfg.misc.thirdperson_grenade.get();
    misc.thirdperson_transparency=cfg.misc.transparency_in_scope.get();
    misc.thirdperson_transparency_nade=cfg.misc.transparency_nade.get();
    misc.fov_enabled=cfg.misc.fov_enabled.get();
    misc.fov=cfg.misc.fov.get();
    misc.aspect=cfg.misc.aspect.get();
    misc.fov_scope=cfg.misc.fov_scope.get();
    misc.force_second_zoom=cfg.misc.force_second_zoom.get();
    misc.force_viewmodel_scope=cfg.misc.force_viewmodel_scope.get();
    misc.vm_fov_enabled=cfg.misc.vm_fov_enabled.get();
    misc.vm_fov=cfg.misc.vm_fov.get();
    misc.vm_x=cfg.misc.viewmodel_x.get();
    misc.vm_y=cfg.misc.viewmodel_y.get();
    misc.vm_z=cfg.misc.viewmodel_z.get();
    misc.vm_roll=cfg.misc.viewmodel_roll.get();
    misc.vm_yaw=cfg.misc.viewmodel_yaw.get();
    misc.vm_pitch=cfg.misc.viewmodel_pitch.get();
    misc.remove_scope=s.screen.scope_mode;
    misc.spread_circle=s.screen.spread;
    VisualsMisc::Publish(misc);
}
}
