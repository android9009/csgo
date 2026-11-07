#include "world_render.hpp"
#include "world_trace.hpp"

#include "../dependencies/source.hpp"
#include "../esp/esp_draw.hpp"
#include "../settings.hpp"
#include "../../../menu/weapon_icons.hpp"
#include "../../../sdk/config_system/config_system.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace world_visuals {
using visuals_source::vector3;

// -----------------------------------------------------------------------------
//  Grenade prediction
// -----------------------------------------------------------------------------
// Ported from Fatality (features/grenade_pred.cpp): the arc is a real per-tick
// physics simulation - a 2x2x2 box swept through the world with MASK_SOLID, bouncing
// off whatever it hits - instead of the flat-ground parabola this used to draw.
namespace {

// CS:GO definition indices of the grenades the prediction cares about.
constexpr int weapon_flashbang = 43;
constexpr int weapon_hegrenade = 44;
constexpr int weapon_smokegrenade = 45;
constexpr int weapon_molotov = 46;
constexpr int weapon_decoy = 47;
constexpr int weapon_incgrenade = 48;

// MASK_SOLID | CONTENTS_CURRENT_90, Fatality's grenade sweep.
constexpr unsigned int grenade_mask = 0x200400Bu | 0x80000u;
constexpr float hull_extent = 2.f;
constexpr float molotov_radius = 150.f;
constexpr float smoke_radius = 144.f;
constexpr float he_damage_range = 350.f;
constexpr float molotov_damage_range = 140.f;
constexpr float pi = 3.14159265358979323846f;

enum class grenade_kind { other, timed, smoke, molotov };

grenade_kind kind_of(const int weapon_id) {
    switch (weapon_id) {
    case weapon_flashbang:
    case weapon_hegrenade: return grenade_kind::timed;
    case weapon_molotov:
    case weapon_incgrenade: return grenade_kind::molotov;
    case weapon_smokegrenade:
    case weapon_decoy: return grenade_kind::smoke;
    default: return grenade_kind::other;
    }
}

vector3 add(const vector3& a, const vector3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
vector3 sub(const vector3& a, const vector3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
vector3 scale(const vector3& a, const float k) { return { a.x * k, a.y * k, a.z * k }; }
float dot(const vector3& a, const vector3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float length(const vector3& a) { return std::sqrt(dot(a, a)); }

void angle_vectors(const vector3& angles, vector3& forward) {
    const float pitch = angles.x * (pi / 180.f);
    const float yaw = angles.y * (pi / 180.f);
    const float cp = std::cos(pitch);
    forward = { cp * std::cos(yaw), cp * std::sin(yaw), -std::sin(pitch) };
}

float fuse_time(const int weapon_id) {
    switch (kind_of(weapon_id)) {
    case grenade_kind::timed:
        return 1.5f; // flashbang / HE fuse
    case grenade_kind::molotov:
        return std::clamp(world_convar("molotov_throw_detonate_time", 2.f), 1.f, 10.f);
    case grenade_kind::smoke:
        return 4.f; // a smoke settles on its own; the timer is only a safety net
    default:
        return 0.f;
    }
}

struct simulated_grenade {
    std::vector<vector3> path{};
    std::vector<vector3> bounces{};
    vector3 end{};
    bool detonated = false;
    bool landed = false;
};

// Fatality's data_t::predict(): step the grenade once per tick with
// sv_gravity * 0.4, sweep the 2x2x2 hull, reflect on contact and stop on the fuse
// (timed grenades) or when the projectile comes to rest (smoke / decoy).
void simulate(const int weapon_id, vector3 origin, vector3 velocity, float seconds,
              simulated_grenade& out) {
    const float dt = visuals_source::tick_interval();
    const auto& settings = player_visuals::world_settings();
    const float exponent = std::clamp(settings.trajectory_exponent, 0.05f, 3.f);
    const float gravity = world_convar("sv_gravity", 800.f) * 0.4f * exponent;
    if (dt <= 0.f || !std::isfinite(gravity))
        return;

    const vector3 hull{ hull_extent, hull_extent, hull_extent };
    const vector3 neg_hull{ -hull_extent, -hull_extent, -hull_extent };
    const grenade_kind kind = kind_of(weapon_id);
    const float limit = std::clamp(seconds, 0.02f, 10.f);
    const int max_ticks = std::clamp(static_cast<int>(limit / dt) + 2, 2, 900);

    out.path.clear();
    out.bounces.clear();
    out.path.push_back(origin);

    float elapsed = 0.f;
    int bounces = 0;
    for (int tick = 0; tick < max_ticks; ++tick) {
        elapsed += dt;
        // Fatality integrates the vertical speed as (v + new_v) / 2 so the arc does
        // not depend on the tick rate the way a plain Euler step would.
        const float new_velocity_z = velocity.z - gravity * dt;
        const vector3 move{ velocity.x * dt, velocity.y * dt,
            (velocity.z + new_velocity_z) * 0.5f * dt };
        velocity.z = new_velocity_z;

        swept_trace trace{};
        if (!trace_swept(origin, add(origin, move), neg_hull, hull, grenade_mask, trace))
            break;

        if (trace.hit) {
            origin = trace.end;
            out.path.push_back(origin);
            out.bounces.push_back(origin);

            // perform_fly_collision_resolution: mirror the velocity around the surface
            // normal and lose most of the energy.
            const float back_off = dot(velocity, trace.normal) * 2.f;
            vector3 bounced{ velocity.x - trace.normal.x * back_off,
                velocity.y - trace.normal.y * back_off, velocity.z - trace.normal.z * back_off };
            for (float* component : { &bounced.x, &bounced.y, &bounced.z })
                if (std::fabs(*component) < 1.f)
                    *component = 0.f;
            bounced = scale(bounced, 0.45f);
            if (trace.normal.z > 0.7f && dot(bounced, bounced) < 400.f) {
                velocity = {};
                out.landed = true;
                break; // came to rest on the ground
            }
            velocity = bounced;
            if (++bounces > 20)
                break;
        } else {
            origin = add(origin, move);
            out.path.push_back(origin);
        }

        if (kind == grenade_kind::smoke && dot(velocity, velocity) <= 0.04f) {
            out.landed = true;
            break;
        }
        if (elapsed >= limit) {
            out.detonated = true;
            break;
        }
    }

    out.end = origin;

    // Fatality drops the molotov fire circle to the ground under the last path point.
    if (kind == grenade_kind::molotov) {
        swept_trace drop{};
        const vector3 below{ out.end.x, out.end.y, out.end.z - 115.f };
        if (trace_swept(out.end, below, neg_hull, hull, grenade_mask, drop) && drop.hit) {
            out.end = drop.end;
            out.path.push_back(out.end);
            out.landed = true;
        }
    }
}

// Live projectiles don't carry their fuse in the snapshot, so the prediction times them
// from the moment the entity first showed up (Fatality listens to `grenade_thrown` for the
// same number).
std::unordered_map<int, float> g_projectile_seen{};

float projectile_elapsed(const int index) {
    const float now = visuals_source::current_time();
    if (g_projectile_seen.size() > 128)
        g_projectile_seen.clear(); // the walk was not running for a while - drop the stale ids
    auto it = g_projectile_seen.find(index);
    if (it == g_projectile_seen.end()) {
        g_projectile_seen[index] = now;
        return 0.f;
    }
    return std::max(0.f, now - it->second);
}

void prune_projectiles(const visuals_source::frame_data& frame) {
    if (g_projectile_seen.empty())
        return;
    for (auto it = g_projectile_seen.begin(); it != g_projectile_seen.end();) {
        const bool present = std::any_of(frame.world.begin(), frame.world.end(),
            [&](const visuals_source::world_data& entity) {
                return entity.kind == visuals_source::world_projectile && entity.index == it->first;
            });
        if (present)
            ++it;
        else
            it = g_projectile_seen.erase(it);
    }
}

// A ground ring, the way Fatality circles the molotov / smoke landing spot.
void draw_world_circle(ImDrawList* draw, const visuals_source::frame_data& frame,
                       const ImVec2& display, const vector3& center, const float radius,
                       const c_color& ink, const float thickness) {
    constexpr int segments = 60;
    ImVec2 previous{};
    bool previous_valid = false;
    for (int i = 0; i <= segments; ++i) {
        const float angle = static_cast<float>(i) * (2.f * pi / static_cast<float>(segments));
        const vector3 point{ center.x + std::sin(angle) * radius,
            center.y + std::cos(angle) * radius, center.z };
        ImVec2 screen{};
        const bool valid = world_to_screen(frame.view_matrix, point, display, screen);
        if (valid && previous_valid)
            draw->AddLine(previous, screen, color(ink), thickness);
        previous = screen;
        previous_valid = valid;
    }
}

void draw_path(ImDrawList* draw, const visuals_source::frame_data& frame, const ImVec2& display,
               const simulated_grenade& sim, const c_color& ink, const float thickness,
               const bool every_point) {
    if (sim.path.size() < 2)
        return;
    ImVec2 previous{};
    bool previous_valid = world_to_screen(frame.view_matrix, sim.path.front(), display, previous);
    for (std::size_t i = 1; i < sim.path.size(); ++i) {
        ImVec2 screen{};
        const bool valid = world_to_screen(frame.view_matrix, sim.path[i], display, screen);
        if (valid && previous_valid && (every_point || i % 2 == 0))
            draw->AddLine(previous, screen, color(ink), thickness);
        previous = screen;
        previous_valid = valid;
    }
    // Bounce marks, like the boxes Fatality drops on every contact.
    for (const vector3& point : sim.bounces) {
        ImVec2 screen{};
        if (!world_to_screen(frame.view_matrix, point, display, screen))
            continue;
        draw->AddRectFilled(ImVec2(screen.x - 2.f, screen.y - 2.f),
            ImVec2(screen.x + 2.f, screen.y + 2.f), color(ink));
    }
}

} // namespace

void render_trajectory(ImDrawList* draw, const visuals_source::world_data& entity,
                       const visuals_source::frame_data& frame, const ImVec2& display) {
    const auto& settings = player_visuals::world_settings();
    if (kind_of(entity.weapon_id) == grenade_kind::other)
        return;
    const bool draw_arc = settings.grenade_trajectory && settings.trajectory_thrown_grenades;
    const bool draw_ring = settings.grenade_proximity_warnings;
    if (!draw_arc && !draw_ring)
        return;
    const float fuse = fuse_time(entity.weapon_id);
    if (fuse <= 0.f)
        return;
    // Fatality keeps drawing a live projectile's remaining path, timed from the throw.
    const float remaining = std::clamp(fuse - projectile_elapsed(entity.index), 0.05f, fuse);
    simulated_grenade sim{};
    simulate(entity.weapon_id, entity.origin, entity.velocity, remaining, sim);
    auto ink = settings.trajectory_color;
    ink.a *= 0.85f;
    if (draw_arc)
        draw_path(draw, frame, display, sim, ink,
            std::clamp(settings.trajectory_thickness, 0.5f, 5.f), settings.trajectory_precise);
    if (draw_ring) {
        const grenade_kind kind = kind_of(entity.weapon_id);
        if (kind == grenade_kind::molotov)
            draw_world_circle(draw, frame, display, sim.end, molotov_radius, ink, 1.5f);
        else if (kind == grenade_kind::smoke)
            draw_world_circle(draw, frame, display, sim.end, smoke_radius, ink, 1.5f);
    }
}

void render_held_trajectory(ImDrawList* draw, const visuals_source::frame_data& frame,
                            const ImVec2& display) {
    const auto& settings = player_visuals::world_settings();
    visuals_source::local_view_state state{};
    if (!visuals_source::get_local_view_state(state) || !state.alive || !state.holding_grenade)
        return;
    // World > Grenades > Trajectory > Pin Pulled Only: the throw is only committed once
    // the pin is out, which is what NL and Fatality gate their preview on.
    if (settings.trajectory_pin_pulled_only && !state.pin_pulled)
        return;
    if (!frame.has_view_matrix)
        return;
    const grenade_kind kind = kind_of(state.weapon_id);
    if (kind == grenade_kind::other)
        return;

    // Fatality's sim: flatten the pitch a little, sweep 22 units out of the eye and start
    // the arc 6 units before the hit point.
    vector3 angles = frame.view_angles;
    angles.x -= (90.f - std::fabs(angles.x)) * 10.f / 90.f;
    vector3 direction{};
    angle_vectors(angles, direction);

    // flThrowVelocity and the throw strength netvar are not in the snapshot; 750 * 0.9 is
    // CS:GO's base toss at full strength, which is what Fatality ends up with too.
    constexpr float base_speed = 750.f;
    const float speed = std::clamp(base_speed * 0.9f, 15.f, 750.f);
    const vector3 velocity = scale(direction, speed);

    const vector3 hull{ hull_extent, hull_extent, hull_extent };
    const vector3 neg_hull{ -hull_extent, -hull_extent, -hull_extent };
    const vector3 eye{ state.eye_origin.x, state.eye_origin.y, state.eye_origin.z };
    swept_trace trace{};
    if (!trace_swept(eye, add(eye, scale(direction, 22.f)), neg_hull, hull, grenade_mask, trace))
        return;
    const vector3 start = sub(trace.end, scale(direction, 6.f));

    simulated_grenade sim{};
    simulate(state.weapon_id, start, velocity, fuse_time(state.weapon_id), sim);

    auto ink = settings.trajectory_color;
    const float thickness = std::clamp(settings.trajectory_thickness, 0.5f, 5.f);
    if (settings.trajectory_shimmer > 0) {
        const float speed_hint = settings.trajectory_shimmer == 3 ? 6.f : 3.f;
        ink.a *= 0.78f + 0.22f * std::sin(static_cast<float>(ImGui::GetTime()) * speed_hint);
    }
    draw_path(draw, frame, display, sim, ink, thickness, settings.trajectory_precise);

    // Landing ring: the molotov fire area, the smoke volume, or the HE damage sphere.
    if (kind == grenade_kind::molotov)
        draw_world_circle(draw, frame, display, sim.end, molotov_radius, ink, 1.5f);
    else if (kind == grenade_kind::smoke)
        draw_world_circle(draw, frame, display, sim.end, smoke_radius, ink, 1.5f);

    ImVec2 end_screen{};
    if (world_to_screen(frame.view_matrix, sim.end, display, end_screen))
        draw->AddCircleFilled(end_screen, 3.f, color(ink), 12);
}

// -----------------------------------------------------------------------------
//  Grenade proximity warning
// -----------------------------------------------------------------------------
// Fatality's grenade_helper: a per-projectile marker on its predicted detonation spot -
// filled circle coloured by whether it can still damage you, an arc for the fuse, the
// distance in metres, and an edge triangle when the spot is off screen.
void render_grenade_warning(ImDrawList* draw, const visuals_source::frame_data& frame,
                            const ImVec2& display) {
    const auto& settings = player_visuals::world_settings();
    if (!settings.grenade_proximity_warnings || !frame.in_game || !frame.has_view_matrix)
        return;
    prune_projectiles(frame);

    const vector3 local{ frame.local_origin.x, frame.local_origin.y, frame.local_origin.z };
    const ImVec2 center(display.x * 0.5f, display.y * 0.5f);
    constexpr float marker_radius = 23.f;

    for (const auto& entity : frame.world) {
        if (entity.kind != visuals_source::world_projectile)
            continue;
        const grenade_kind kind = kind_of(entity.weapon_id);
        if (kind == grenade_kind::other)
            continue;
        const float fuse = fuse_time(entity.weapon_id);
        const float elapsed = projectile_elapsed(entity.index);
        const float remaining = std::clamp(fuse - elapsed, 0.05f, fuse);

        simulated_grenade sim{};
        simulate(entity.weapon_id, entity.origin, entity.velocity, remaining, sim);

        const float distance = length(sub(sim.end, local));
        float damage = 0.f;
        if (entity.weapon_id == weapon_hegrenade)
            damage = he_damage_range - distance;
        else if (kind == grenade_kind::molotov)
            damage = molotov_damage_range - distance;
        const bool dangerous = damage > 0.f;

        // The ring on the ground: molotov fire area / smoke volume.
        if (kind == grenade_kind::molotov)
            draw_world_circle(draw, frame, display, sim.end, molotov_radius,
                c_color{ 1.f, 0.35f, 0.12f, 0.9f }, 1.5f);
        else if (kind == grenade_kind::smoke)
            draw_world_circle(draw, frame, display, sim.end, smoke_radius,
                c_color{ 0.65f, 0.65f, 0.65f, 0.9f }, 1.5f);

        ImVec2 screen{};
        const bool on_screen = world_to_screen(frame.view_matrix, sim.end, display, screen) &&
            screen.x > marker_radius && screen.x < display.x - marker_radius &&
            screen.y > marker_radius && screen.y < display.y - marker_radius;

        const c_color fill = dangerous ? c_color{ 0.93f, 0.20f, 0.20f, 0.55f }
                                       : c_color{ 0.35f, 0.36f, 0.40f, 0.45f };
        if (!on_screen) {
            // Off screen: put a triangle on the edge of an ellipse around the centre, pointing
            // at the grenade - Fatality's placement, so the warning still tells you which way.
            const float dx = sim.end.x - local.x;
            const float dy = sim.end.y - local.y;
            const float target_yaw = std::atan2(dy, dx) * 180.f / pi;
            float delta = target_yaw - frame.view_angles.y;
            while (delta > 180.f) delta -= 360.f;
            while (delta < -180.f) delta += 360.f;
            const float angle = (delta - 90.f) * (pi / 180.f);
            const float radius_x = center.x * 0.7f;
            const float radius_y = radius_x * (display.y / display.x);
            const ImVec2 tip(center.x + radius_x * std::cos(angle),
                center.y + radius_y * std::sin(angle));
            const ImVec2 direction(std::cos(angle), std::sin(angle));
            const ImVec2 side(-direction.y, direction.x);
            const ImVec2 back = tip - direction * 11.f;
            draw->AddTriangleFilled(tip, back + side * 8.f, back - side * 8.f, color(fill));
            draw->AddTriangle(tip, back + side * 8.f, back - side * 8.f,
                color(dangerous ? c_color{ 0.93f, 0.20f, 0.20f, 1.f } : c_color{ 0.6f, 0.62f, 0.66f, 1.f }),
                1.5f);
            screen = tip + direction * 26.f;
        } else {
            draw->AddCircleFilled(screen, marker_radius, color(fill), 32);
            // Fuse arc: how much time is left before it goes off.
            const float progress = std::clamp(1.f - remaining / fuse, 0.f, 1.f);
            if (progress > 0.001f) {
                draw->PathClear();
                draw->PathArcTo(screen, marker_radius, -pi * 0.5f,
                    -pi * 0.5f + progress * 2.f * pi, 32);
                draw->PathStroke(color(c_color{ 1.f, 0.93f, 0.55f, 0.95f }), 0, 2.f);
            }
            draw->AddCircle(screen, marker_radius,
                color(dangerous ? c_color{ 0.93f, 0.35f, 0.35f, 1.f } : c_color{ 0.62f, 0.64f, 0.70f, 1.f }),
                32, 1.5f);
        }

        char text[32];
        std::snprintf(text, sizeof(text), "%.0f m", distance * 0.01905f);
        draw_text(draw, ImVec2(screen.x, screen.y + marker_radius + 8.f), text,
            color(dangerous ? c_color{ 1.f, 0.72f, 0.70f, 1.f } : c_color{ 0.82f, 0.84f, 0.88f, 1.f }),
            0.85f);
    }
}

} // namespace world_visuals
