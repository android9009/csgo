#pragma once
// Translates the published VisualRuntime settings (chams / ESP / glow) into
// airflow's g_cfg so the *existing* airflow rendering features react to the new
// menu. Called once per frame from the menu render loop, right after the three
// Configure() bridges have republished the menu state.
//
// The two config systems are not 1:1: the menu has per-side (enemy/team/local)
// chams and three ESP profiles while airflow has category-based chams and a
// single player ESP. This forwards the highest-value fields and leaves the rest
// in menu state.
#include "features/airflow_bridge.hpp"
#include "../../sdk/visual_runtime_state.hpp"

namespace AirflowSync {
namespace {
using Material = VisualRuntime::Material;
inline void WriteChams(int cat, const Material& m, bool master) {
    auto& c = g_cfg.visuals.chams[cat];
    c.enable = master && m.enabled && m.kind > 0;
    c.material = m.kind;
    c.main_color = fatality_bridge::unpack(m.primary);
    c.glow_color = fatality_bridge::unpack(m.secondary);
}
}
inline void Configure() {
    const auto s = VisualRuntime::Get();
    const bool chams = s.chams_enabled;

    // --- Chams (best-effort category mapping) -------------------------------
    WriteChams(static_cast<int>(chams_type_t::c_vis), s.players[VisualRuntime::Enemy], chams);
    WriteChams(static_cast<int>(chams_type_t::c_local), s.players[VisualRuntime::Local], chams);
    WriteChams(static_cast<int>(chams_type_t::c_ragdolls), s.ragdolls[VisualRuntime::Enemy], chams);
    WriteChams(static_cast<int>(chams_type_t::c_attachments), s.attachments[VisualRuntime::Enemy], chams);
    WriteChams(static_cast<int>(chams_type_t::c_viewmodel), s.arms, chams);
    WriteChams(static_cast<int>(chams_type_t::c_onshot), s.shot, chams);

    // --- Player ESP ---------------------------------------------------------
    {
        const auto& e = s.esp[VisualRuntime::Enemy];
        auto& cfg = g_cfg.visuals.esp[esp_enemy];
        cfg.enable = e.show[VisualRuntime::Enabled];
        unsigned elements = 0;
        if (e.show[VisualRuntime::Box]) elements |= 1;
        if (e.show[VisualRuntime::Name]) elements |= 2;
        if (e.show[VisualRuntime::Health]) elements |= 4;
        if (e.show[VisualRuntime::Icon]) elements |= 8;
        if (e.show[VisualRuntime::Weapon]) elements |= 16;
        if (e.show[VisualRuntime::Ammo]) elements |= 32;
        if (e.show[VisualRuntime::Armor]) elements |= 64;
        cfg.elements = elements;
        cfg.colors.box = fatality_bridge::unpack(e.colors[VisualRuntime::BoxColor]);
        cfg.colors.name = fatality_bridge::unpack(e.colors[VisualRuntime::NameColor]);
        cfg.colors.health = fatality_bridge::unpack(e.colors[VisualRuntime::HealthColor]);
        cfg.colors.weapon = fatality_bridge::unpack(e.colors[VisualRuntime::WeaponColor]);
        cfg.colors.ammo_bar = fatality_bridge::unpack(e.colors[VisualRuntime::AmmoColor]);
        cfg.colors.skeleton = fatality_bridge::unpack(e.colors[VisualRuntime::SkeletonColor]);
    }

    // --- Local glow ---------------------------------------------------------
    {
        const auto& glow = s.player_glow[VisualRuntime::Local];
        g_cfg.visuals.local_glow = glow.enabled;
        g_cfg.visuals.local_glow_color = fatality_bridge::unpack(glow.color);
    }
}
}
