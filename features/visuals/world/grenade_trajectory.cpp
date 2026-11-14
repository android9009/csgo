#include "render.hpp"
#include "trace.hpp"

#include "../dependencies/source.hpp"
#include "../esp/esp_draw.hpp"
#include "../settings.hpp"
#include "../../../menu/weapon_icons.hpp"
#include "../../../sdk/config_system/config_system.hpp"

#include "../../../runtime/debug_console.hpp"

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_DEBUG)
// Debug-only re-simulation monitor: prints every 120 re-simulations (and every
// single sim over 2 ms) so the console shows whether the arc is what eats frames
// or whether it is the menu / ESP / blur elsewhere. Release compiles this out.
namespace {
struct sim_monitor_t {
    unsigned long long calls = 0;
    double total_ms = 0.0;
    double worst_ms = 0.0;
    unsigned long long slow = 0;
    void add(const char* tag, double ms) {
        ++calls;
        total_ms += ms;
        if (ms > worst_ms)
            worst_ms = ms;
        if (ms > 1.0)
            ++slow;
        if (ms > 2.0)
            AIRFLOW_DEBUG_LOGF("grenade: slow %s sim %.3f ms", tag, ms);
        if (calls % 120 == 0) {
            AIRFLOW_DEBUG_LOGF(
                "grenade: %s sims=120 avg=%.3f ms worst=%.3f ms slow(>1ms)=%llu",
                tag, total_ms / 120.0, worst_ms, slow);
            total_ms = 0.0;
            worst_ms = 0.0;
            slow = 0;
        }
    }
};
sim_monitor_t& held_sim_monitor() { static sim_monitor_t m; return m; }
sim_monitor_t& projectile_sim_monitor() { static sim_monitor_t m; return m; }
sim_monitor_t& beam_sim_monitor() { static sim_monitor_t m; return m; }
} // namespace
#endif

namespace world_visuals {
using visuals_source::vector3;

// -----------------------------------------------------------------------------
//  Grenade prediction — Airflow's engine (main branch), surgically ported.
// -----------------------------------------------------------------------------
// The physics runs on the game thread (FrameStageNotify) and is cached; the Present
// hook only draws the arcs published with the frame snapshot. The engine itself is a line-by-line port of Airflow's
// grenade_prediction.cpp/.hpp: sv_gravity * 0.4 with the z-velocity average, the
// 2x2x2 hull, the entity-aware filter (skip the thrower, hit everything else), the
// water current re-trace, the line re-trace against players, player/breakable
// elasticity, the debris-group switch on repeated hits, the ground damper and the
// think() timer that detonates on the fuse. Own extensions: damage-coloured
// proximity warning with a meters readout, echo of the landing circle for molotovs,
// and the Exponent/Precise/Shimmer UI knobs (Exponent = 1.0 is stock Airflow).
namespace {

// CS:GO definition indices of the grenades the prediction cares about.
constexpr int weapon_flashbang = 43;
constexpr int weapon_hegrenade = 44;
constexpr int weapon_smokegrenade = 45;
constexpr int weapon_molotov = 46;
constexpr int weapon_decoy = 47;
constexpr int weapon_incgrenade = 48;

// Airflow contents_t / masks_t (defines.hpp).
constexpr unsigned int contents_solid = 0x1u;
constexpr unsigned int contents_window = 0x2u;
constexpr unsigned int contents_grate = 0x8u;
constexpr unsigned int contents_opaque = 0x80u;
constexpr unsigned int contents_ignore_nodraw_opaque = 0x2000u;
constexpr unsigned int contents_moveable = 0x4000u;
constexpr unsigned int contents_current_90 = 0x80000u;
constexpr unsigned int contents_monster = 0x2000000u;
constexpr unsigned int contents_hitbox = 0x40000000u;
constexpr unsigned int mask_solid = contents_solid | contents_moveable | contents_window |
    contents_monster | contents_grate;                                          // 0x200400B
// Airflow physics_push_entity, normal collision group.
constexpr unsigned int grenade_mask = mask_solid | contents_opaque | contents_ignore_nodraw_opaque |
    contents_current_90 | contents_hitbox;
// Airflow physics_push_entity, COLLISION_GROUP_DEBRIS (and the start_solid re-trace).
constexpr unsigned int grenade_debris_mask = (mask_solid | contents_current_90) & ~contents_monster;

constexpr int collision_group_projectile = 0;      // COLLISION_GROUP_PROJECTILE
constexpr int collision_group_interactive_deb = 1; // COLLISION_GROUP_INTERACTIVE_DEB
constexpr int collision_group_debris = 2;           // COLLISION_GROUP_DEBRIS

constexpr float hull_extent = 2.f;
constexpr float pi = 3.14159265358979323846f;
// Airflow stores a path point every 1/30 s instead of every tick: same shape, a third of
// the projections and fewer vertices for the draw list.
constexpr float path_interval = 1.f / 30.f;

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
float length_sqr(const vector3& a) { return dot(a, a); }
float length(const vector3& a) { return std::sqrt(dot(a, a)); }

void angle_vectors(const vector3& angles, vector3& forward) {
    const float pitch = angles.x * (pi / 180.f);
    const float yaw = angles.y * (pi / 180.f);
    const float cp = std::cos(pitch);
    forward = { cp * std::cos(yaw), cp * std::sin(yaw), -std::sin(pitch) };
}

float tick_interval() {
    const float interval = visuals_source::tick_interval();
    return interval > 0.001f && interval < 1.f ? interval : 1.f / 64.f;
}

// sv_gravity / molotov_throw_detonate_time / weapon_molotov_maxdetonateslope are read
// through the engine's FindVar, which walks the cvar list — not something to do per
// simulated tick. Refreshed twice per second on a wall clock: the prediction runs on
// the game thread, so it must not read ImGui's frame counter any more.
struct convar_cache_t {
    float gravity = 800.f;
    float molotov_fuse = 2.f;
    float molotov_max_slope = 45.f;
    std::chrono::steady_clock::time_point refreshed{};
    bool valid = false;
};

convar_cache_t& cached_convars() {
    static convar_cache_t cache;
    const auto now = std::chrono::steady_clock::now();
    if (!cache.valid || now - cache.refreshed > std::chrono::milliseconds(500)) {
        cache.valid = true;
        cache.refreshed = now;
        cache.gravity = world_convar("sv_gravity", 800.f);
        cache.molotov_fuse = world_convar("molotov_throw_detonate_time", 2.f);
        cache.molotov_max_slope = world_convar("weapon_molotov_maxdetonateslope", 45.f);
    }
    return cache;
}

// One stored point of an arc; `bounce` marks a collision.
struct sim_point {
    vector3 position{};
    bool bounce = false;
};

// A finished simulation. The owner keeps the object alive between simulations so
// `points` reuses its capacity instead of allocating a fresh vector per grenade
// per tick.
struct simulated_grenade {
    std::vector<sim_point> points{};
    vector3 end{};
    bool detonated = false;
    bool landed = false;
    float fuse_time = 0.f;    // main's nade_detonate_time (0 for smoke/decoy)
    float expire_time = 0.f;  // predicted detonation, absolute clock time
};

// Nothing a grenade can do past the map bounds matters. Without this a smoke or
// decoy thrown off the map (no fuse) kept falling for the full 60 simulated
// seconds: 3840 engine traces for one arc.
constexpr float world_limit = 16384.f;

// Airflow's nade_path_t, line for line. The only behavioural grafts: Exponent
// (settings.trajectory_exponent, 1.0 = stock) on gravity, the rest-on-ground
// mark for the landing circle and the map-bounds stop. Everything else — masks,
// re-traces, elasticity, damper, caps — is Airflow verbatim.
struct nade_prediction {
    int weapon_id = 0;
    void* owner = nullptr; // the thrower; the sweep never hits this entity
    vector3 origin{};
    vector3 velocity{};

    int collision_group = collision_group_projectile;
    void* last_hit_entity = nullptr;
    bool is_detonated = false;
    bool landed = false;
    float detonate_time = 0.f;
    int nade_tick_count = 0;
    int next_think_tick = 0;
    int last_update_tick = 0;
    int bounces_count = 0;
    std::vector<sim_point>* path = nullptr; // caller-owned output

    // Constant for one simulation; resolved once in predict() instead of once
    // per simulated tick.
    float interval = 1.f / 64.f;
    float gravity = 320.f;
    float molotov_slope = 0.f;

    int to_ticks(const float seconds) const {
        return static_cast<int>(seconds / interval + 0.5f);
    }

    // Airflow physics_trace_entity: hull sweep, water re-trace, line re-trace
    // against players. `trace` keeps the final answer.
    void physics_trace_entity(const vector3& src, const vector3& dst,
                              unsigned int mask, swept_trace& trace) {
        const vector3 hull{ hull_extent, hull_extent, hull_extent };
        const vector3 neg_hull{ -hull_extent, -hull_extent, -hull_extent };

        if (!trace_swept_entities(src, dst, neg_hull, hull, mask, owner, trace)) {
            trace = swept_trace{};
            trace.hit = false;
            trace.fraction = 1.f;
            return;
        }

        // Start solid in a water current: re-trace without it, otherwise the hull
        // sticks for the whole arc (Airflow's CONTENTS_CURRENT_90 handling).
        if (trace.start_solid && (trace.contents & contents_current_90) != 0u)
            trace_swept_entities(src, dst, neg_hull, hull, mask & ~contents_current_90,
                owner, trace);

        if (trace.fraction >= 1.f || !trace.entity)
            return;

        if (!visuals_source::entity_is_player(trace.entity))
            return;

        // Against players Airflow re-traces a zero-extent line, so the bounce is
        // resolved against the surface the line actually crosses.
        const vector3 zero{};
        trace_swept_entities(src, dst, zero, zero, mask, owner, trace);
    }

    // Airflow physics_push_entity: move the grenade along `push`, react to contact.
    void physics_push_entity(const vector3& push, swept_trace& trace) {
        const unsigned int mask = collision_group == collision_group_debris
            ? grenade_debris_mask : grenade_mask;

        physics_trace_entity(origin, add(origin, push), mask, trace);

        // Start solid (grenade spawned inside geometry or inside an entity):
        // switch to the interactive-debris group and line-trace across the
        // embed so the grenade can get out instead of bouncing in place.
        if (trace.start_solid) {
            collision_group = collision_group_interactive_deb;
            const vector3 zero{};
            trace_swept_entities(sub(origin, push), add(origin, push), zero, zero,
                grenade_debris_mask, owner, trace);
        }

        if (trace.fraction > 0.f)
            origin = trace.end;

        if (!trace.entity)
            return;

        // Players never detonate a molotov. Nothing but molotovs detonates on
        // entity contact at all (HE/flash are fuse-only).
        if (visuals_source::entity_is_player(trace.entity))
            return;
        if (kind_of(weapon_id) != grenade_kind::molotov)
            return;

        // Airflow: molotovs burst on impact only on surfaces flatter than
        // weapon_molotov_maxdetonateslope (cos of the slope angle).
        if (trace.normal.z < molotov_slope)
            return;

        detonate<true>();
    }

    void think() {
        const grenade_kind kind = kind_of(weapon_id);
        switch (kind) {
        case grenade_kind::smoke:
            if (length_sqr(velocity) <= (weapon_id == weapon_decoy ? 0.04f : 0.01f))
                detonate<false>();
            break;
        case grenade_kind::timed:
        case grenade_kind::molotov:
            if (nade_tick_count * interval > detonate_time)
                detonate<false>();
            break;
        default:
            break;
        }

        next_think_tick = nade_tick_count + to_ticks(0.2f);
    }

    template <bool bounced>
    void detonate() {
        is_detonated = true;
        update_path<bounced>();
    }

    template <bool bounced>
    void update_path() {
        last_update_tick = nade_tick_count;
        path->push_back({ origin, bounced });
    }

    bool physics_simulate() {
        if (is_detonated)
            return true;

        // Airflow integrates the vertical speed as (v + new_v) / 2 so the arc does not
        // depend on the tick rate the way a plain Euler step would.
        const float new_velocity_z = velocity.z - gravity * interval;
        const vector3 move{ velocity.x * interval, velocity.y * interval,
            (velocity.z + new_velocity_z) * 0.5f * interval };

        velocity.z = new_velocity_z;

        swept_trace trace{};
        physics_push_entity(move, trace);

        if (is_detonated)
            return true;

        if (trace.fraction != 1.f) {
            update_path<true>();
            perform_fly_collision_resolution(trace);
        }

        return false;
    }

    // Airflow perform_fly_collision_resolution: reflect, bleed energy, rest on the
    // floor, die after 20 bounces.
    void perform_fly_collision_resolution(swept_trace& trace) {
        float surface_elasticity = 1.f;

        if (trace.entity) {
            if (visuals_source::entity_is_breakable(trace.entity)) {
                // BREAKTROUGH: barely slowed smashing through a window.
                velocity = scale(velocity, 0.4f);
                return;
            }

            const bool is_player = visuals_source::entity_is_player(trace.entity);
            if (is_player)
                surface_elasticity = 0.3f;

            // Second hit on the same player: stop colliding with him entirely, so
            // the nade cannot reflect in place between two players.
            if (is_player && last_hit_entity == trace.entity) {
                collision_group = collision_group_debris;
                return;
            }

            last_hit_entity = trace.entity;
        }

        const vector3 reflected{
            velocity.x - trace.normal.x * dot(velocity, trace.normal) * 2.f,
            velocity.y - trace.normal.y * dot(velocity, trace.normal) * 2.f,
            velocity.z - trace.normal.z * dot(velocity, trace.normal) * 2.f
        };

        vector3 bounced{ reflected };
        // Airflow zeroes any component under 1 unit/s so resting contact can settle.
        if (std::fabs(bounced.x) < 1.f) bounced.x = 0.f;
        if (std::fabs(bounced.y) < 1.f) bounced.y = 0.f;
        if (std::fabs(bounced.z) < 1.f) bounced.z = 0.f;
        bounced = scale(bounced, std::clamp(surface_elasticity * 0.45f, 0.f, 0.9f));

        if (trace.normal.z > 0.7f) {
            const float speed_sqr = length_sqr(bounced);
            if (speed_sqr > 96000.f) {
                const float len = std::sqrt(speed_sqr);
                const float l = len > 0.f ? dot(scale(bounced, 1.f / len), trace.normal) : 0.f;
                if (l > 0.5f)
                    bounced = scale(bounced, 1.f - l + 0.5f);
            }

            if (speed_sqr < 400.f) {
                velocity = {};
                landed = true;
            } else {
                velocity = bounced;

                // Use up the remainder of this tick moving along the bounce.
                swept_trace push_trace{};
                physics_push_entity(scale(bounced, (1.f - trace.fraction) * interval),
                    push_trace);
            }
        } else {
            velocity = bounced;

            swept_trace push_trace{};
            physics_push_entity(scale(bounced, (1.f - trace.fraction) * interval),
                push_trace);
        }

        if (bounces_count > 20) {
            detonate<false>();
            return;
        }
        ++bounces_count;
    }

    bool outside_world() const {
        return std::fabs(origin.x) > world_limit || std::fabs(origin.y) > world_limit ||
            std::fabs(origin.z) > world_limit;
    }

    // Airflow nade_path_t::predict_nade. `offset_ticks` fast-forwards the fuse for
    // live projectiles (ticks since first sighting) without moving the grenade —
    // simulation starts at the current position, same as Airflow's spawn-time offset.
    void predict(const int weapon, void* skip, const vector3& start,
                 const vector3& start_velocity, const int offset_ticks) {
        weapon_id = weapon;
        owner = skip;
        origin = start;
        velocity = start_velocity;
        collision_group = collision_group_projectile;

        const auto& settings = player_visuals::world_settings();
        const convar_cache_t& convars = cached_convars();
        interval = tick_interval();
        gravity = convars.gravity * 0.4f * std::clamp(settings.trajectory_exponent, 0.05f, 3.f);
        molotov_slope = std::cos(std::clamp(convars.molotov_max_slope, 0.f, 89.9f) *
            (pi / 180.f));

        const int path_ticks = std::max(1, to_ticks(
            path_interval * (settings.trajectory_precise ? (1.f / 3.f) : 1.f)));
        const int max_ticks = to_ticks(60.f);

        last_update_tick = -path_ticks;

        switch (kind_of(weapon_id)) {
        case grenade_kind::smoke:
            next_think_tick = to_ticks(weapon_id == weapon_decoy ? 2.f : 1.5f);
            break;
        case grenade_kind::timed:
            detonate_time = 1.5f; // flashbang / HE fuse
            next_think_tick = to_ticks(0.02f);
            break;
        case grenade_kind::molotov:
            detonate_time = std::clamp(convars.molotov_fuse, 0.5f, 10.f);
            next_think_tick = to_ticks(0.02f);
            break;
        default:
            break;
        }

        for (; nade_tick_count < max_ticks; ++nade_tick_count) {
            if (next_think_tick <= nade_tick_count)
                think();
            // Fuse burned out inside the fast-forward region (offset longer than the
            // remaining fuse): nothing left to simulate.
            if (is_detonated)
                break;
            if (nade_tick_count < offset_ticks)
                continue;

            if (physics_simulate())
                break;
            if (outside_world())
                break;

            if (last_update_tick + path_ticks > nade_tick_count)
                continue;

            update_path<false>();
        }

        if (last_update_tick + path_ticks <= nade_tick_count)
            update_path<false>();
    }
};

// Airflow's data_t::predict_nade wrapper: physics up to the fuse / rest, written
// into a caller-owned result so its point buffer is reused between simulations.
void run_prediction(simulated_grenade& out, const int weapon_id, void* owner,
                    const vector3& origin, const vector3& velocity, const int offset_ticks,
                    const float now) {
    out.points.clear();
    nade_prediction sim{};
    sim.path = &out.points;
    sim.predict(weapon_id, owner, origin, velocity, offset_ticks);

    if (out.points.empty() || length(sub(out.points.back().position, sim.origin)) > 0.5f)
        out.points.push_back({ sim.origin, false });
    out.end = sim.origin;
    out.detonated = sim.is_detonated;
    out.landed = sim.landed || sim.is_detonated;
    out.fuse_time = sim.detonate_time;
    // The fast-forward ticks burned before the first simulated step must not
    // inflate the fuse: anchor to when this throw actually started.
    const float sim_start = now - offset_ticks * sim.interval;
    out.expire_time = sim_start + sim.nade_tick_count * sim.interval;

    // Airflow drives the molotov fire circle to the ground under the last path point.
    if (kind_of(weapon_id) == grenade_kind::molotov) {
        swept_trace drop{};
        const vector3 below{ out.end.x, out.end.y, out.end.z - 115.f };
        const vector3 hull{ hull_extent, hull_extent, hull_extent };
        const vector3 neg_hull{ -hull_extent, -hull_extent, -hull_extent };
        if (trace_swept_entities(out.end, below, neg_hull, hull, grenade_mask, nullptr,
                drop) && drop.hit) {
            out.end = drop.end;
            out.points.push_back({ out.end, false });
            out.landed = true;
        }
    }
}

// --- game-thread state ---------------------------------------------------------
// Everything below runs from the FrameStageNotify hook (source.cpp): the physics at
// FRAME_RENDER_END as part of the frame capture, the engine-overlay hand-off at
// FRAME_RENDER_START. It used to run inside the Present hook - hundreds of engine
// traces per arc on the render thread, under the renderer's frame mutex, racing the
// game thread and repeated for every thread that presents the device.

// A live projectile is re-simulated every few ticks, not every tick: its remaining
// arc only changes when it deviates from the previous prediction, and the published
// path is re-anchored to the live position on every frame in between.
constexpr int projectile_resim_ticks = 4;
// A projectile missing from the snapshot for this long is forgotten (its entity
// index is free for the next grenade after about a second).
constexpr int projectile_forget_ticks = 32;
// Fatality's path cutoff for the thrown-grenade arc.
constexpr float thrown_arc_cutoff = 2000.f;

struct held_state_t {
    int weapon_id = -1;
    int pin_pulled = -1;
    float pitch = 0.f, yaw = 0.f;
    float eye_x = 0.f, eye_y = 0.f, eye_z = 0.f;
    float vel_x = 0.f, vel_y = 0.f, vel_z = 0.f;
    float strength = -1.f;
    std::chrono::steady_clock::time_point last_sim{};
    bool valid = false;
    bool visible = false; // an arc was published by the last update
    simulated_grenade sim{};
};

struct projectile_state_t {
    simulated_grenade sim{};
    bool has_sim = false;
    int sim_tick = 0;
    int seen_tick = 0;
    // Velocity from the last two ticks, for projectiles whose networked velocity is
    // unusable: (origin_now - origin_previous) / dt is the speed the physics applies.
    int sample_tick = 0;
    float sample_time = -1.f;
    vector3 sample_origin{};
    vector3 sampled_velocity{};
    float first_seen = 0.f;
    // What the last update published, kept for the engine-beam pass.
    vector3 origin{};
    std::size_t first_point = 0;
    bool arc_visible = false;
};

struct prediction_state_t {
    // The capture normally has a single caller (the game thread). The lock only
    // matters for the moment the frame-stage hook goes live while Present is still
    // on its fallback capture.
    std::mutex mutex;
    held_state_t held{};
    std::unordered_map<int, projectile_state_t> projectiles{};
    int beam_tick = -1;
    bool beam_pass_running = false; // submit_grenade_beams() is being driven
    std::chrono::steady_clock::time_point shimmer_epoch = std::chrono::steady_clock::now();
};

prediction_state_t& prediction_state() {
    static prediction_state_t state;
    return state;
}

// First stored point of `sim` the grenade has not flown past yet. The published
// arc of a live projectile starts at its current position and continues from here,
// so a prediction that is a few ticks old never trails behind the grenade.
std::size_t first_remaining_point(const simulated_grenade& sim, const vector3& live_origin) {
    const std::size_t count = sim.points.size();
    if (count < 2)
        return 0;
    const auto distance_sqr = [&](const std::size_t index) {
        return length_sqr(sub(sim.points[index].position, live_origin));
    };
    // Walk forward while the stored points keep getting closer to the grenade.
    std::size_t nearest = 0;
    float best = distance_sqr(0);
    while (nearest + 1 < count) {
        const float next = distance_sqr(nearest + 1);
        if (next > best)
            break;
        best = next;
        ++nearest;
    }
    if (nearest + 1 >= count)
        return nearest;
    // Already beyond `nearest` when the next point is closer to the grenade than it
    // is to `nearest` itself.
    const float segment_sqr = length_sqr(sub(sim.points[nearest + 1].position,
        sim.points[nearest].position));
    return distance_sqr(nearest + 1) < segment_sqr ? nearest + 1 : nearest;
}

// Copies an arc into the frame snapshot and returns its index in frame.grenades.
int publish(visuals_source::frame_data& frame, const simulated_grenade& sim,
            const int entity_index, const int weapon_id, const std::size_t first_point,
            const vector3* live_origin) {
    visuals_source::grenade_prediction out{};
    out.entity_index = entity_index;
    out.weapon_id = weapon_id;
    out.end = sim.end;
    out.detonated = sim.detonated;
    out.landed = sim.landed;
    out.fuse_time = sim.fuse_time;
    out.expire_time = sim.expire_time;

    out.path_begin = static_cast<int>(frame.grenade_points.size());
    if (live_origin)
        frame.grenade_points.push_back(*live_origin);
    for (std::size_t i = first_point; i < sim.points.size(); ++i)
        frame.grenade_points.push_back(sim.points[i].position);
    out.path_count = static_cast<int>(frame.grenade_points.size()) - out.path_begin;

    out.bounce_begin = static_cast<int>(frame.grenade_points.size());
    for (std::size_t i = first_point; i < sim.points.size(); ++i) {
        if (sim.points[i].bounce)
            frame.grenade_points.push_back(sim.points[i].position);
    }
    out.bounce_count = static_cast<int>(frame.grenade_points.size()) - out.bounce_begin;

    frame.grenades.push_back(out);
    return static_cast<int>(frame.grenades.size()) - 1;
}

// The held arc follows the crosshair, so it is re-simulated when the inputs that
// shape it move: view angles, eye position, player velocity, charged strength or the
// weapon. Airflow's calc_local_nade_path: the throw is computed from the eye, the
// view angles and the weapon's own toss speed.
bool simulate_held(const visuals_source::frame_data& frame,
                   const visuals_source::local_view_state& state, held_state_t& cache) {
    const visuals_source::vector3 view = frame.view_angles;
    const float pitch = view.x, yaw = view.y;
    const float eye_x = state.eye_origin.x, eye_y = state.eye_origin.y, eye_z = state.eye_origin.z;
    const float vel_x = state.velocity.x, vel_y = state.velocity.y, vel_z = state.velocity.z;
    const float strength = state.pin_pulled ? std::clamp(state.throw_strength, 0.f, 1.f) : 1.f;
    const bool pin = state.pin_pulled;
    const auto changed = [](const float a, const float b, const float e) {
        return std::fabs(a - b) > e;
    };
    // Re-simulate only when something that shapes the arc actually moves. Standing
    // still with the nade out does not re-run the physics at all - but a moving
    // camera changes the inputs every frame, and the engine traces are what they
    // are, so wall-clock throttle the re-simulation to ~30 Hz.
    const auto now = std::chrono::steady_clock::now();
    if (cache.valid && cache.weapon_id == state.weapon_id && cache.pin_pulled == (pin ? 1 : 0) &&
        !changed(cache.pitch, pitch, 0.05f) && !changed(cache.yaw, yaw, 0.05f) &&
        !changed(cache.eye_x, eye_x, 0.5f) && !changed(cache.eye_y, eye_y, 0.5f) &&
        !changed(cache.eye_z, eye_z, 0.5f) && !changed(cache.vel_x, vel_x, 2.f) &&
        !changed(cache.vel_y, vel_y, 2.f) && !changed(cache.vel_z, vel_z, 2.f) &&
        !changed(cache.strength, strength, 0.02f))
        return true;
    if (cache.valid && now - cache.last_sim < std::chrono::microseconds(33333))
        return true;

#if defined(_DEBUG)
    const auto predict_t0 = std::chrono::steady_clock::now();
#endif
    const grenade_kind kind = kind_of(state.weapon_id);
    if (kind == grenade_kind::other || !frame.has_view_matrix) {
        cache.valid = false;
        return false;
    }

    // Airflow flattens the pitch a little before sweeping the throw direction.
    vector3 angles = view;
    while (angles.x < -90.f) angles.x += 360.f;
    while (angles.x > 90.f) angles.x -= 360.f;
    angles.x -= (90.f - std::fabs(angles.x)) * 10.f / 90.f;
    vector3 direction{};
    angle_vectors(angles, direction);

    // Every CS:GO grenade throws at 750; the game scales it by strength and takes 0.9 off
    // the top before clamping.
    constexpr float throw_velocity = 750.f;
    const float launch = std::clamp(throw_velocity * 0.9f, 15.f, 750.f) *
        (strength * 0.7f + 0.3f);

    // A moving player throws further: the game adds m_vecVelocity * 1.25 to the toss.
    vector3 player_velocity = state.velocity;
    const float speed3 = std::sqrt(player_velocity.x * player_velocity.x +
        player_velocity.y * player_velocity.y + player_velocity.z * player_velocity.z);
    if (speed3 < 10.f)
        player_velocity = {}; // standing still: nothing to inherit

    const vector3 hull{ hull_extent, hull_extent, hull_extent };
    const vector3 neg_hull{ -hull_extent, -hull_extent, -hull_extent };
    // Fatality's lead: the server throws from where the eyes will be one
    // tick after the snapshot, so the arc starts there, not at the eye.
    const vector3 eye{ state.eye_origin.x + vel_x * 0.1f, state.eye_origin.y + vel_y * 0.1f,
        state.eye_origin.z + vel_z * 0.1f + (strength * 12.f - 12.f) };
    void* local = visuals_source::entity_for_index(frame.local_index);
    swept_trace trace{};
    if (!trace_swept_entities(eye, add(eye, scale(direction, 22.f)), neg_hull, hull,
            grenade_mask, local, trace)) {
        cache.valid = false;
        return false;
    }
    const vector3 start = sub(trace.end, scale(direction, 6.f));

    const vector3 launch_velocity = add(scale(direction, launch), scale(player_velocity, 1.25f));
    run_prediction(cache.sim, state.weapon_id, local, start, launch_velocity, 0,
        frame.game_time);
    cache.weapon_id = state.weapon_id;
    cache.pin_pulled = pin ? 1 : 0;
    cache.pitch = pitch;
    cache.yaw = yaw;
    cache.eye_x = eye_x;
    cache.eye_y = eye_y;
    cache.eye_z = eye_z;
    cache.vel_x = vel_x;
    cache.vel_y = vel_y;
    cache.vel_z = vel_z;
    cache.strength = strength;
    cache.last_sim = std::chrono::steady_clock::now();
    cache.valid = true;
#if defined(_DEBUG)
    held_sim_monitor().add("held",
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - predict_t0)
            .count());
#endif
    return true;
}

void update_held(visuals_source::frame_data& frame, held_state_t& held) {
    const auto& settings = player_visuals::world_settings();
    visuals_source::local_view_state view{};
    if (!visuals_source::get_local_view_state(view) || !view.alive || !view.holding_grenade)
        return;
    // World > Grenades > Trajectory > Pin Pulled Only: the throw is only committed once the
    // pin is out, which is what Airflow and Fatality gate their preview on.
    if (settings.trajectory_pin_pulled_only && !view.pin_pulled)
        return;
    if (!simulate_held(frame, view, held) || held.sim.points.size() < 2)
        return;
    held.visible = true;
    frame.held_grenade = publish(frame, held.sim, 0, view.weapon_id, 0, nullptr);
}

void update_projectiles(visuals_source::frame_data& frame, prediction_state_t& state,
                        const bool thrown_arcs, const bool warnings) {
    const int tick = visuals_source::tick_count();
    const float now = frame.game_time;

    for (auto& entity : frame.world) {
        if (entity.kind != visuals_source::world_projectile)
            continue;
        if (kind_of(entity.weapon_id) == grenade_kind::other)
            continue;
        // Airflow only draws the remaining arc for the thrower; everyone else's
        // grenades only matter to the proximity warning.
        const bool local_owned = entity.owner_index >= 0 &&
            entity.owner_index == frame.local_index;
        if (!warnings && !(thrown_arcs && local_owned))
            continue;

        const auto [it, inserted] = state.projectiles.try_emplace(entity.index);
        projectile_state_t& projectile = it->second;
        if (inserted) {
            // Prefer the exact server throw tick tracked through grenade_thrown
            // (Fatality does the same); fall back to the first frame we saw the
            // projectile when the throw happened before we joined or the event
            // was missed.
            projectile.first_seen = now;
            const float spawn = visuals_source::consume_grenade_throw(entity.owner_index, now);
            if (spawn >= 0.f)
                projectile.first_seen = spawn;
            projectile.sample_tick = tick - 1;
        }
        projectile.seen_tick = tick;
        projectile.origin = entity.origin;
        projectile.arc_visible = false;

        if (tick != projectile.sample_tick) {
            if (projectile.sample_time >= 0.f && now > projectile.sample_time) {
                projectile.sampled_velocity = scale(sub(entity.origin, projectile.sample_origin),
                    1.f / (now - projectile.sample_time));
            }
            projectile.sample_origin = entity.origin;
            projectile.sample_time = now;
            projectile.sample_tick = tick;
        }

        if (!projectile.has_sim || tick < projectile.sim_tick ||
            tick - projectile.sim_tick >= projectile_resim_ticks) {
            vector3 velocity = projectile.sampled_velocity;
            const float networked = length(entity.velocity);
            if (networked > 50.f && networked < 3000.f)
                velocity = entity.velocity;
            const int offset_ticks = std::max(0,
                static_cast<int>((now - projectile.first_seen) / tick_interval() + 0.5f));
            void* owner = visuals_source::entity_for_index(entity.owner_index);
#if defined(_DEBUG)
            const auto predict_t0 = std::chrono::steady_clock::now();
#endif
            run_prediction(projectile.sim, entity.weapon_id, owner, entity.origin, velocity,
                offset_ticks, now);
#if defined(_DEBUG)
            projectile_sim_monitor().add("live",
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - predict_t0).count());
#endif
            projectile.sim_tick = tick;
            projectile.has_sim = true;
        }

        if (projectile.sim.points.size() < 2)
            continue;
        projectile.first_point = first_remaining_point(projectile.sim, entity.origin);
        entity.prediction = publish(frame, projectile.sim, entity.index, entity.weapon_id,
            projectile.first_point, &entity.origin);
        projectile.arc_visible = thrown_arcs && local_owned &&
            length(sub(projectile.sim.end, frame.local_origin)) <= thrown_arc_cutoff;
    }

    for (auto it = state.projectiles.begin(); it != state.projectiles.end();) {
        const int age = tick - it->second.seen_tick;
        if (age < 0 || age > projectile_forget_ticks)
            it = state.projectiles.erase(it);
        else
            ++it;
    }
}

// Fatality's path renderer draws each segment as a world-space beam (a 1-unit-tall
// oriented box) in the tracer colour and stamps a solid cube at every bounce point.
void submit_arc(const simulated_grenade& sim, const std::size_t first_point,
                const vector3* live_origin, const c_color& ink, const float thickness) {
    const float half = std::clamp(thickness * 0.5f, 0.05f, 2.f);
    const float cube = std::clamp(thickness * 0.5f, 0.2f, 1.f);
    const vector3* previous = live_origin;
    for (std::size_t i = first_point; i < sim.points.size(); ++i) {
        const sim_point& point = sim.points[i];
        // debug_beam() itself drops segments shorter than half a unit.
        if (previous)
            visuals_source::debug_beam(*previous, point.position, half, ink);
        if (point.bounce)
            visuals_source::debug_cube(point.position, cube, ink);
        previous = &point.position;
    }
}

} // namespace

// -----------------------------------------------------------------------------
//  Game thread, FRAME_RENDER_END: simulate and publish
// -----------------------------------------------------------------------------
void update_grenade_predictions(visuals_source::frame_data& frame) {
    auto& state = prediction_state();
    std::scoped_lock lock(state.mutex);
    const auto& settings = player_visuals::world_settings();
    const bool arcs = settings.grenade_trajectory;
    const bool warnings = settings.grenade_proximity_warnings;

    state.held.visible = false;
    if (!frame.in_game || (!arcs && !warnings)) {
        state.projectiles.clear();
        return;
    }
    if (arcs)
        update_held(frame, state.held);
    update_projectiles(frame, state, arcs && settings.trajectory_thrown_grenades, warnings);

    // Engine Beams: the overlay pass draws the arcs in world space, so tell the
    // renderer to keep its 2D polyline out of the way. Without the frame-stage
    // hook nothing drives that pass and the 2D arc takes over seamlessly.
    frame.grenade_engine_beams = arcs && settings.trajectory_engine_beams &&
        state.beam_pass_running && visuals_source::debug_overlay_available();
}

// -----------------------------------------------------------------------------
//  Game thread, FRAME_RENDER_START: engine-overlay arcs
// -----------------------------------------------------------------------------
// A debug-overlay box added with duration 0 lives until the client tick advances.
// The arcs used to be re-added from every Present: at 300 fps that stacked ~5
// copies of every segment per tick, each one a solid + wireframe draw in the
// engine. They are submitted once per tick now, before the view is rendered.
void submit_grenade_beams() {
    auto& state = prediction_state();
    std::scoped_lock lock(state.mutex);
    state.beam_pass_running = true;

    const auto& settings = player_visuals::world_settings();
    if (!settings.grenade_trajectory || !settings.trajectory_engine_beams)
        return;
    bool any = state.held.visible;
    if (!any) {
        for (const auto& entry : state.projectiles) {
            if (entry.second.arc_visible) {
                any = true;
                break;
            }
        }
    }
    if (!any)
        return;
    const int tick = visuals_source::tick_count();
    if (tick == state.beam_tick || !visuals_source::debug_overlay_available())
        return;
    state.beam_tick = tick;

#if defined(_DEBUG)
    const auto beam_t0 = std::chrono::steady_clock::now();
#endif
    const float thickness = std::clamp(settings.trajectory_thickness, 0.5f, 5.f);
    if (state.held.visible) {
        auto ink = settings.trajectory_color;
        if (settings.trajectory_shimmer > 0) {
            const float speed = settings.trajectory_shimmer == 3 ? 6.f : 3.f;
            const float seconds = std::chrono::duration<float>(
                std::chrono::steady_clock::now() - state.shimmer_epoch).count();
            ink.a *= 0.78f + 0.22f * std::sin(seconds * speed);
        }
        submit_arc(state.held.sim, 0, nullptr, ink, thickness);
    }
    for (const auto& entry : state.projectiles) {
        const projectile_state_t& projectile = entry.second;
        if (!projectile.arc_visible)
            continue;
        auto ink = settings.trajectory_color;
        ink.a *= 0.85f;
        submit_arc(projectile.sim, projectile.first_point, &projectile.origin, ink, thickness);
    }
#if defined(_DEBUG)
    beam_sim_monitor().add("beams",
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - beam_t0)
            .count());
#endif
}

namespace {

// --- drawing -----------------------------------------------------------------
// Present-thread half. It only projects points that the game thread published
// into the snapshot: no engine calls, no simulation, no shared mutable state.
const visuals_source::grenade_prediction* prediction_at(const visuals_source::frame_data& frame,
                                                        const int index) {
    if (index < 0 || index >= static_cast<int>(frame.grenades.size()))
        return nullptr;
    const auto& prediction = frame.grenades[static_cast<std::size_t>(index)];
    const int points = static_cast<int>(frame.grenade_points.size());
    if (prediction.path_begin < 0 || prediction.path_count < 0 ||
        prediction.path_begin + prediction.path_count > points ||
        prediction.bounce_begin < 0 || prediction.bounce_count < 0 ||
        prediction.bounce_begin + prediction.bounce_count > points) {
        return nullptr;
    }
    return &prediction;
}

// The 2D arc: a soft, wide low-alpha underpass with a thin full-alpha core - it reads
// as a glowing beam without any engine-renderer dependency - plus a solid filled dot
// at every bounce. With Engine Beams on, the engine overlay draws the same arc in
// world space instead (submit_grenade_beams) and this stays out of the way.
void draw_path(ImDrawList* draw, const visuals_source::frame_data& frame, const ImVec2& display,
               const visuals_source::grenade_prediction& prediction, const c_color& ink,
               const float thickness, const float dot_radius) {
    if (prediction.path_count < 2 || frame.grenade_engine_beams)
        return;
    const vector3* path = frame.grenade_points.data() + prediction.path_begin;
    const vector3* bounces = frame.grenade_points.data() + prediction.bounce_begin;

    static thread_local std::vector<ImVec2> points;
    points.clear();
    for (int i = 0; i < prediction.path_count; ++i) {
        ImVec2 screen{};
        if (world_to_screen(frame.view_matrix, path[i], display, screen))
            points.push_back(screen);
    }
    const int count = static_cast<int>(points.size());
    if (count >= 2) {
        const auto soft = c_color{ ink.r, ink.g, ink.b, ink.a * 0.22f };
        draw->AddPolyline(points.data(), count, color(soft), ImDrawFlags_None, thickness * 2.75f);
        draw->AddPolyline(points.data(), count, color(ink), ImDrawFlags_None, thickness);
    }
    for (int i = 0; i < prediction.bounce_count; ++i) {
        ImVec2 screen{};
        if (!world_to_screen(frame.view_matrix, bounces[i], display, screen))
            continue;
        draw->AddCircleFilled(screen, std::max(1.5f, dot_radius), color(ink), 12);
    }
}

} // namespace

// -----------------------------------------------------------------------------
//  Held grenade: the arc you are about to throw
// -----------------------------------------------------------------------------
void render_held_trajectory(ImDrawList* draw, const visuals_source::frame_data& frame,
                            const ImVec2& display) {
    const auto& settings = player_visuals::world_settings();
    const auto* prediction = prediction_at(frame, frame.held_grenade);
    if (!prediction)
        return;

    auto ink = settings.trajectory_color;
    if (settings.trajectory_shimmer > 0) {
        const float speed = settings.trajectory_shimmer == 3 ? 6.f : 3.f;
        ink.a *= 0.78f + 0.22f * std::sin(static_cast<float>(ImGui::GetTime()) * speed);
    }
    draw_path(draw, frame, display, *prediction, ink,
        std::clamp(settings.trajectory_thickness, 0.5f, 5.f),
        std::clamp(settings.trajectory_points_thickness, 1.5f, 6.f) * 0.5f);
}

// -----------------------------------------------------------------------------
//  Already thrown grenades: remaining path (Airflow's local-owner line)
// -----------------------------------------------------------------------------
void render_trajectory(ImDrawList* draw, const visuals_source::world_data& entity,
                       const visuals_source::frame_data& frame, const ImVec2& display) {
    const auto& settings = player_visuals::world_settings();
    // Airflow only draws the remaining arc for the thrower (grenade_warning_line);
    // everyone else's grenades get the marker, not the line.
    if (entity.owner_index < 0 || entity.owner_index != frame.local_index)
        return;
    if (!settings.grenade_trajectory || !settings.trajectory_thrown_grenades)
        return; // the proximity warning owns the marker; this only draws the remaining arc

    const auto* prediction = prediction_at(frame, entity.prediction);
    if (!prediction || prediction->path_count < 2)
        return;
    const float end_distance = length(sub(prediction->end, frame.local_origin));
    if (end_distance > thrown_arc_cutoff)
        return;
    auto ink = settings.trajectory_color;
    ink.a *= 0.85f;
    draw_path(draw, frame, display, *prediction, ink,
        std::clamp(settings.trajectory_thickness, 0.5f, 5.f),
        std::clamp(settings.trajectory_points_thickness, 1.5f, 6.f) * 0.5f);
}

// -----------------------------------------------------------------------------
//  Grenade proximity warning — Fatality's grenade marker: it tracks the nade while
//  it is IN FLIGHT (not after detonation) and shows how dangerous the landing spot
//  is. Filled circle (red when HE/molotov would reach it, dark grey otherwise), a
//  fuse-depleting arc ring in the tracer colour, the weapon glyph, and the distance
//  in metres. Off-screen, a triangle rides a 0.7x screen-edge ellipse instead.
// -----------------------------------------------------------------------------
void render_grenade_warning(ImDrawList* draw, const visuals_source::frame_data& frame,
                            const ImVec2& display) {
    const auto& settings = player_visuals::world_settings();
    if (!settings.grenade_proximity_warnings || !frame.in_game || !frame.has_view_matrix)
        return;

    // The game clock of the capture: the renderer does not read engine globals.
    const float now = frame.game_time;
    const c_color tracer = settings.trajectory_color;
    const c_color danger{ 0.90f, 0.28f, 0.28f, 1.f };   // Fatality's helper red
    const c_color safe{ 0.42f, 0.44f, 0.50f, 1.f };     // dark grey otherwise

    for (const auto& entity : frame.world) {
        if (entity.kind != visuals_source::world_projectile)
            continue;
        if (kind_of(entity.weapon_id) == grenade_kind::other)
            continue;
        const auto* sim = prediction_at(frame, entity.prediction);
        if (!sim || sim->path_count <= 1)
            continue;

        const float distance = length(sub(sim->end, frame.local_origin));
        if (distance > 1600.f)
            continue; // Fatality's marker cutoff

        // Damage forecast at the landing spot: HE ~350 flat minus falloff, molotov ~140.
        float damage = 0.f;
        if (entity.weapon_id == weapon_hegrenade)
            damage = 350.f - distance;
        else if (kind_of(entity.weapon_id) == grenade_kind::molotov)
            damage = 140.f - distance;
        const bool lethal = damage > 0.f;
        const c_color marker = lethal ? danger : safe;

        // Fuse fraction left: full ring until the fuse starts burning, then it drains.
        float percent = 1.f;
        if (sim->fuse_time > 0.f)
            percent = std::clamp((sim->expire_time - now) / sim->fuse_time, 0.f, 1.f);

        ImVec2 screen{};
        const bool oof = !world_to_screen(frame.view_matrix, sim->end, display, screen);
        if (oof) {
            // Edge helper: a triangle riding a 0.7x screen-height-adjusted ellipse,
            // aimed away from the centre, then the marker sits 35px inside it.
            const ImVec2 center(display.x * 0.5f, display.y * 0.5f);
            const float dx = sim->end.x - frame.local_origin.x;
            const float dy = sim->end.y - frame.local_origin.y;
            const float target_yaw = std::atan2(dy, dx) * (180.f / pi);
            float delta = frame.view_angles.y - target_yaw;
            while (delta > 180.f)
                delta -= 360.f;
            while (delta < -180.f)
                delta += 360.f;
            const float sa = (delta - 90.f) * (pi / 180.f);
            const float radius = display.x * 0.5f * 0.7f;
            const float ry = radius * (display.y / std::max(1.f, display.x));
            const float c = std::cos(sa);
            const float s = std::sin(sa);
            const ImVec2 tip(center.x + radius * c, center.y + ry * s);
            // Triangle, authored apex-up and rotated into the axis.
            const float rot = sa + pi * 0.5f;
            const float cosr = std::cos(rot);
            const float sinr = std::sin(rot);
            const ImVec2 local[3] = { ImVec2(0.f, -23.f), ImVec2(-11.5f, 0.f), ImVec2(11.5f, 0.f) };
            ImVec2 tri[3];
            for (int i = 0; i < 3; ++i)
                tri[i] = ImVec2(tip.x + local[i].x * cosr - local[i].y * sinr,
                                tip.y + local[i].x * sinr + local[i].y * cosr);
            auto fill = marker;
            fill.a = 100.f / 255.f;
            draw->AddTriangleFilled(tri[0], tri[1], tri[2], color(fill));
            auto outline = tracer;
            outline.a = 100.f / 255.f;
            draw->AddTriangle(tri[0], tri[1], tri[2], color(outline), 1.f);
            // The circle marker rides 35 px inside the arrow along the same axis.
            screen = ImVec2(tip.x - 35.f * c, tip.y - 35.f * s);
        }

        // On-screen marker (also drawn at the edge position when off-screen):
        // filled circle, tracer-coloured fuse arc, weapon glyph, metres to the spot.
        constexpr float radius = 23.f;
        auto fill = marker;
        fill.a = 100.f / 255.f;
        draw->AddCircleFilled(screen, radius, color(fill), 32);

        const auto ring = tracer;
        if (percent >= 1.f) {
            draw->AddCircle(screen, radius,
                color(c_color{ ring.r, ring.g, ring.b, 200.f / 255.f }), 32, 2.f);
        } else if (percent > 0.f) {
            constexpr int segments = 64;
            ImVec2 arc[segments + 1];
            for (int i = 0; i <= segments; ++i) {
                const float a = -pi * 0.5f + (2.f * pi) * percent
                    * (static_cast<float>(i) / static_cast<float>(segments));
                arc[i] = ImVec2(screen.x + radius * std::cos(a), screen.y + radius * std::sin(a));
            }
            draw->AddPolyline(arc, segments + 1,
                color(c_color{ ring.r, ring.g, ring.b, 200.f / 255.f }), ImDrawFlags_None, 2.f);
        }

        if (const int atlas = visuals_source::atlas_weapon_id(entity.weapon_id); atlas >= 0)
            weapon_icons::draw(draw, atlas, ImVec2(screen.x, screen.y - 4.f), ImVec2(22.f, 9.f),
                color(c_color{ 1.f, 1.f, 1.f, 1.f }));

        char text[24];
        std::snprintf(text, sizeof(text), "%.0f m", distance * 0.01905f);
        const ImVec2 size = ImGui::CalcTextSize(text);
        const ImVec2 text_pos(screen.x - size.x * 0.5f, screen.y + 13.f);
        draw->AddText(text_pos + ImVec2(1.f, 1.f), color(c_color{ 0.f, 0.f, 0.f, 0.6f }), text);
        draw->AddText(text_pos, color(c_color{ 1.f, 1.f, 1.f, 1.f }), text);
    }
}

} // namespace world_visuals
