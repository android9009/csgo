#pragma once
#include "menu.h"
#include "../cfg.h"
#include "../../../sdk/visual_runtime_state.hpp"
namespace VisualSettings {
using namespace evo::gui;
using namespace evo::ren;
inline std::array<VisualPreview::Layout,3> layouts{};
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
    s.flags=static_cast<std::uint64_t>(e.flags.get());s.layout=subject==cfg.visuals.esp.active_profile ? VisualPreview::GetLayout() : layouts[subject];return s;
}
template<class T> inline VisualRuntime::Material Material(T& c) {
    VisualRuntime::Material s;s.enabled=true;s.kind=c.mat.get().first_set_bit().value_or(0);s.ignore_z=c.ignorez.get();
    s.primary=c.col1.get().rgba();s.secondary=c.col2.get().rgba();s.hidden=c.col_z.get().rgba();return s;
}
inline void Configure() {
    SyncProfile();
    auto& esp=cfg.visuals.esp;auto& c=cfg.visuals.chams;auto& w=esp.world;
    VisualRuntime::Settings s;s.chams_enabled=c.enable.get();
    s.players={Material(c.enemy_pl),Material(c.team_pl),Material(c.local_pl)};
    s.ragdolls={Material(c.enemy_rag),Material(c.team_rag),Material(c.local_pl)};
    s.attachments={Material(c.enemy_att),Material(c.team_att),Material(c.local_att)};s.arms=Material(c.local_arms);
    s.player_glow={VisualRuntime::Highlight{esp.glow.enabled.get() && esp.glow.enemy.get(),esp.glow.enemy_color.get().rgba()},
        VisualRuntime::Highlight{esp.glow.enabled.get() && esp.glow.team.get(),esp.glow.team_color.get().rgba()},
        VisualRuntime::Highlight{esp.glow.enabled.get() && esp.glow.local.get(),esp.glow.local_color.get().rgba()}};
    s.object_enabled={w.weapon_enabled.get(),w.grenade_enabled.get(),w.c4_enabled.get()};
    auto object=[&](int index,auto& v) {
        s.objects[index].enabled=s.object_enabled[index];s.objects[index].kind=v.material.get().first_set_bit().value_or(0);s.objects[index].primary=v.material_color.get().rgba();s.objects[index].secondary=s.objects[index].primary;
        s.object_glow[index]={s.object_enabled[index] && v.glow_mode->test(1),v.glow_color.get().rgba()};
    };
    object(0,w.weapon_visuals);object(1,w.grenade_visuals);object(2,w.c4_visuals);
    for(int i=0;i<3;++i)s.esp[i]=ReadEsp(i==esp.active_profile ? static_cast<cfg_t::visuals_t::esp_player_t&>(esp) : esp.profiles[i],i);
    VisualRuntime::Publish(s);
}
}
