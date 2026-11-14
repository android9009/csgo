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
// think() timer that detonates on the fuse. Own extensions: the Exponent/Precise/
// Shimmer UI knobs (Exponent = 1.0 is stock Airflow). The look of the arc and the
// Grenade Warning (bottom of this file) is ported from the Neverlose Lua.
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
    bool friendly_fire = false;
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
        cache.friendly_fire = world_convar("mp_friendlyfire", 0.f) != 0.f;
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
    // Grenade Warning forecast, refreshed together with the simulation.
    bool warn = false;
    bool local_in_range = false;
    float local_damage = 0.f;
    std::vector<visuals_source::grenade_victim> victims{};
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
            const vector3* live_origin, const projectile_state_t* danger = nullptr) {
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

    if (danger) {
        out.warn = danger->warn;
        out.local_in_range = danger->local_in_range;
        out.local_damage = danger->local_damage;
        out.victim_begin = static_cast<int>(frame.grenade_victims.size());
        frame.grenade_victims.insert(frame.grenade_victims.end(), danger->victims.begin(),
            danger->victims.end());
        out.victim_count = static_cast<int>(danger->victims.size());
    }

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

// Grenade Warning forecast (the Lua source's get_dangerous_data): who the predicted
// detonation reaches and for how much. Game thread: it traces from the landing
// spot to five points of every player within 350 units.
//   HE:      exp(-d^2 / 27222.2) * 100 * exposure, then the armor rule.
//   Molotov: no damage figure, only the distance to the landing spot.
// The source samples hitbox 0 for the first point; the eye position stands in for
// it here so the forecast does not need a bone setup per player.
void forecast_danger(projectile_state_t& projectile, const int weapon_id, const int owner_index,
                     const int local_index,
                     const std::vector<visuals_source::grenade_target>& targets) {
    projectile.victims.clear();
    projectile.local_damage = 0.f;
    projectile.local_in_range = false;
    projectile.warn = false;

    const bool molotov = kind_of(weapon_id) == grenade_kind::molotov;
    if (!molotov && weapon_id != weapon_hegrenade)
        return;
    // A teammate's grenade only matters with friendly fire on; the local player's
    // own grenade and everything thrown by the enemy (or by nobody we can resolve)
    // always does.
    if (owner_index != local_index && visuals_source::player_side(owner_index) == 1 &&
        !cached_convars().friendly_fire)
        return;
    projectile.warn = true;

    constexpr unsigned int mask_shot = 0x46004003u;
    constexpr float weights[5] = { 0.2f, 0.4f, 0.1f, 0.1f, 0.2f };
    const vector3 end = projectile.sim.end;
    const vector3 point_hull{};

    for (const auto& target : targets) {
        const float distance = length(sub(end, target.eye));
        if (distance > 350.f)
            continue;

        const float yaw = (target.eye_yaw + 90.f) * (pi / 180.f);
        const vector3 side{ std::cos(yaw) * 16.f, std::sin(yaw) * 16.f, 0.f };
        const vector3 points[5] = { target.eye, target.eye, sub(target.eye, side),
            add(target.eye, side), target.origin };
        float exposure = 0.f;
        // Points 0 and 1 are the same position here: one trace answers both.
        swept_trace trace{};
        for (int i = 1; i < 5; ++i) {
            if (!trace_swept(end, points[i], point_hull, point_hull, mask_shot, trace))
                continue;
            if (trace.fraction >= 1.f)
                exposure += i == 1 ? weights[0] + weights[1] : weights[i];
        }

        const float feet_distance = length(sub(end, target.origin));
        visuals_source::grenade_victim victim{};
        victim.entity_index = target.index;
        victim.health = target.health;
        if (molotov) {
            victim.distance = feet_distance * 0.0254f - 1.f;
            if (target.local) {
                projectile.local_in_range = true;
                projectile.local_damage = feet_distance / 12.f;
            }
            projectile.victims.push_back(victim);
            continue;
        }
        if (exposure <= 0.f)
            continue;
        float damage = std::exp(-distance * distance / 27222.2222f) * 100.f * exposure;
        if (target.armor > 0) {
            float after_armor = damage * 0.6f;
            if (static_cast<float>(target.armor) < (damage - after_armor) * 0.5f)
                after_armor = damage - static_cast<float>(target.armor) * 2.f;
            damage = after_armor;
        }
        if (target.local) {
            projectile.local_in_range = true;
            projectile.local_damage = damage;
        }
        victim.damage = static_cast<int>(damage + 0.5f);
        projectile.victims.push_back(victim);
    }
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
    // Players for the damage forecast, collected once per capture and only when a
    // projectile is actually re-simulated on it.
    static std::vector<visuals_source::grenade_target> targets;
    bool targets_ready = false;

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

            if (warnings) {
                if (!targets_ready) {
                    visuals_source::collect_grenade_targets(targets);
                    targets_ready = true;
                }
                forecast_danger(projectile, entity.weapon_id, entity.owner_index,
                    frame.local_index, targets);
            } else {
                projectile.warn = false;
                projectile.victims.clear();
            }
        }

        if (projectile.sim.points.size() < 2)
            continue;
        projectile.first_point = first_remaining_point(projectile.sim, entity.origin);
        entity.prediction = publish(frame, projectile.sim, entity.index, entity.weapon_id,
            projectile.first_point, &entity.origin, &projectile);
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
    // Same accent as the 2D arc: the detonation / rest point is red.
    if (sim.landed || sim.detonated)
        visuals_source::debug_cube(sim.end, cube * 1.25f, c_color{ 1.f, 0.f, 40.f / 255.f, ink.a });
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
    frame.friendly_fire = cached_convars().friendly_fire;
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

// -----------------------------------------------------------------------------
//  Visual layer ported from the Neverlose "Grenade Warning" Lua (draw_warning).
//  Only the look is taken from it; the physics above stays Airflow's.
// -----------------------------------------------------------------------------

// The Lua's colours are 0-255; keep them readable at the call sites.
ImU32 rgba(const float r, const float g, const float b, const float a) {
    return color(r / 255.f, g / 255.f, b / 255.f, a / 255.f);
}

// neverlose/smoothy with its default (linear) easing, as the source calls it:
// smoothy(duration, target). Per frame:
//     value = (target - value) * frametime / duration + value
// It snaps to the target when the frame is longer than the duration or the
// remaining difference drops to 0.001. The source always passes 0.1 s.
constexpr float smoothy_duration = 0.1f;

float approach(float& value, const float target, const float delta_time) {
    if (value == target)
        return value;
    if (delta_time <= 0.f || delta_time >= smoothy_duration) {
        value = target;
        return value;
    }
    value += (target - value) * delta_time / smoothy_duration;
    if (std::fabs(target - value) <= 0.001f)
        value = target;
    return value;
}

// MuseoSans 12 in the source. Slot 5 is the embedded MuseoSans (17 px), drawn at 12.
ImFont* warning_text_font() {
    const ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    return atlas->Fonts.Size > 5 ? atlas->Fonts[5] : ImGui::GetFont();
}

// Slot 6 carries the merged Font Awesome glyphs (timer / grenade / fire).
ImFont* warning_icon_font() {
    const ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    return atlas->Fonts.Size > 6 ? atlas->Fonts[6] : ImGui::GetFont();
}

constexpr float warning_text_size = 12.f;
constexpr const char* glyph_timer = "\xef\x80\x97"; // U+F017 clock
constexpr const char* glyph_bomb = "\xef\x87\xa2";  // U+F1E2 bomb
constexpr const char* glyph_fire = "\xef\x81\xad";  // U+F06D fire

ImVec2 text_size(ImFont* font, const float size, const char* text) {
    return font->CalcTextSizeA(size, FLT_MAX, 0.f, text);
}

// render.text(font, pos, color, "c", text): centred on both axes.
void text_centered(ImDrawList* draw, ImFont* font, const float size, const ImVec2& center,
                   const ImU32 ink, const char* text) {
    const ImVec2 extent = text_size(font, size, text);
    draw->AddText(font, size,
        ImVec2(std::floor(center.x - extent.x * 0.5f), std::floor(center.y - extent.y * 0.5f)),
        ink, text);
}

// render.shadow(): a soft glow around a rounded box. Stacked outlines with a
// falling alpha - no blur pass, a handful of AA strokes.
void soft_shadow(ImDrawList* draw, const ImVec2& a, const ImVec2& b, const float rounding,
                 const float alpha, const float spread = 8.f) {
    if (alpha <= 0.004f)
        return;
    constexpr int layers = 8;
    for (int i = 1; i <= layers; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(layers);
        const float grow = spread * t;
        const float fade = (1.f - t) * (1.f - t) * 0.22f * alpha;
        draw->AddRect(a - ImVec2(grow, grow), b + ImVec2(grow, grow),
            color(0.f, 0.f, 0.f, fade), rounding + grow, ImDrawFlags_None, spread / layers + 1.f);
    }
}

// Text with the Lua's glow behind it (render.shadow under render.text).
void text_glow(ImDrawList* draw, ImFont* font, const ImVec2& center, const ImU32 ink,
               const float alpha, const char* text) {
    const ImU32 shade = color(0.f, 0.f, 0.f, 0.55f * alpha);
    const ImVec2 extent = text_size(font, warning_text_size, text);
    const ImVec2 top_left(std::floor(center.x - extent.x * 0.5f),
        std::floor(center.y - extent.y * 0.5f));
    static const ImVec2 offsets[4] = { { 1.f, 1.f }, { -1.f, 1.f }, { 1.f, -1.f }, { -1.f, -1.f } };
    for (const ImVec2& offset : offsets)
        draw->AddText(font, warning_text_size, top_left + offset, shade, text);
    draw->AddText(font, warning_text_size, top_left, ink, text);
}

// render.circle_gradient(pos, inner, outer, radius): a radial fan, transparent in
// the middle and coloured at the rim.
void circle_gradient(ImDrawList* draw, const ImVec2& center, const float radius,
                     const ImU32 inner, const ImU32 outer) {
    if (((inner | outer) & IM_COL32_A_MASK) == 0)
        return;
    constexpr int segments = 48;
    const ImVec2 uv = draw->_Data->TexUvWhitePixel;
    draw->PrimReserve(segments * 3, segments + 1);
    const unsigned int base = draw->_VtxCurrentIdx;
    draw->PrimWriteVtx(center, uv, inner);
    for (int i = 0; i < segments; ++i) {
        const float angle = (2.f * pi) * static_cast<float>(i) / static_cast<float>(segments);
        draw->PrimWriteVtx(ImVec2(center.x + std::cos(angle) * radius,
            center.y + std::sin(angle) * radius), uv, outer);
    }
    for (int i = 0; i < segments; ++i) {
        draw->PrimWriteIdx(static_cast<ImDrawIdx>(base));
        draw->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1 + i));
        draw->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1 + (i + 1) % segments));
    }
    // The fan itself is not anti-aliased; a thin stroke cleans the rim up.
    draw->AddCircle(center, radius, outer, segments, 1.f);
}

// render.get_offscreen(pos, 0.9): the projected point while it is on screen,
// otherwise a point on a 0.9x screen ellipse in the direction of the target.
ImVec2 screen_or_edge(const visuals_source::frame_data& frame, const ImVec2& display,
                      const vector3& world) {
    ImVec2 screen{};
    if (world_to_screen(frame.view_matrix, world, display, screen) && screen.x >= 0.f &&
        screen.y >= 0.f && screen.x <= display.x && screen.y <= display.y)
        return screen;
    const float target_yaw = std::atan2(world.y - frame.local_origin.y,
        world.x - frame.local_origin.x) * (180.f / pi);
    float delta = frame.view_angles.y - target_yaw;
    while (delta > 180.f)
        delta -= 360.f;
    while (delta < -180.f)
        delta += 360.f;
    const float angle = (delta - 90.f) * (pi / 180.f);
    return ImVec2(display.x * 0.5f + std::cos(angle) * display.x * 0.5f * 0.9f,
        display.y * 0.5f + std::sin(angle) * display.y * 0.5f * 0.9f);
}

// The 2D arc in the source's style: a plain line in the trajectory colour, a white
// dot with a dark halo on every bounce and a red dot where the grenade ends up.
// Points behind the camera break the line instead of being bridged.
void draw_path(ImDrawList* draw, const visuals_source::frame_data& frame, const ImVec2& display,
               const visuals_source::grenade_prediction& prediction, const c_color& ink,
               const float thickness, const float dot_radius) {
    if (prediction.path_count < 2)
        return;
    const vector3* path = frame.grenade_points.data() + prediction.path_begin;
    const vector3* bounces = frame.grenade_points.data() + prediction.bounce_begin;

    static thread_local std::vector<ImVec2> run;
    run.clear();
    const auto flush = [&] {
        if (run.size() >= 2)
            draw->AddPolyline(run.data(), static_cast<int>(run.size()), color(ink),
                ImDrawFlags_None, thickness);
        run.clear();
    };
    for (int i = 0; i < prediction.path_count; ++i) {
        ImVec2 screen{};
        if (world_to_screen(frame.view_matrix, path[i], display, screen))
            run.push_back(screen);
        else
            flush();
    }
    flush();

    const float radius = std::max(1.5f, dot_radius);
    const auto dot = [&](const vector3& position, const ImU32 fill) {
        ImVec2 screen{};
        if (!world_to_screen(frame.view_matrix, position, display, screen))
            return;
        draw->AddCircleFilled(screen, radius + 1.f, rgba(0.f, 0.f, 0.f, 60.f * ink.a), 16);
        draw->AddCircleFilled(screen, radius, fill, 16);
    };
    const bool has_end = prediction.landed || prediction.detonated;
    for (int i = 0; i < prediction.bounce_count; ++i) {
        // The final collision is the red one; do not stack a white dot under it.
        if (has_end && length_sqr(sub(bounces[i], prediction.end)) < 4.f)
            continue;
        dot(bounces[i], rgba(255.f, 255.f, 255.f, 200.f * ink.a));
    }
    if (has_end)
        dot(prediction.end, rgba(255.f, 0.f, 40.f, 200.f * ink.a));
}

// Per-object animation state of the warning. Present thread only.
struct warning_anim {
    float near_alpha = 0.f;  // local player within 300 units of the landing spot
    float alive_alpha = 0.f; // the fuse has not run out yet
    float danger = 0.f;      // white -> red blend
    bool seen = false;
};

std::unordered_map<int, warning_anim>& warning_anims() {
    static thread_local std::unordered_map<int, warning_anim> anims;
    return anims;
}

constexpr int inferno_anim_key = 0x10000; // entity indices stay below 2048

// The widget itself: gradient ring, grenade glyph, and the rounded timer pill
// underneath (with the "-N HP" line sliding out for an HE that would hurt).
void draw_warning_widget(ImDrawList* draw, const ImVec2& anchor, const bool molotov,
                         const float alpha, const float danger, const bool pulse,
                         const float seconds, const float local_damage) {
    if (alpha <= 0.01f)
        return;
    ImFont* text_font = warning_text_font();
    ImFont* icon_font = warning_icon_font();
    const ImVec2 pos(std::floor(anchor.x), std::floor(anchor.y));

    float ring_alpha = alpha;
    if (pulse) {
        const float wave = (std::sin(static_cast<float>(ImGui::GetTime()) * 6.f) + 1.f) * 0.5f;
        ring_alpha = (0.55f + 0.45f * wave) * alpha;
    }
    // color(255, 200):lerp(color(255, 0, 0, 255), blend)
    const float green_blue = 255.f * (1.f - danger);
    const float ring_base = 200.f + 55.f * danger;
    circle_gradient(draw, pos, 35.f, rgba(0.f, 0.f, 0.f, 0.f),
        rgba(255.f, green_blue, green_blue, ring_base * ring_alpha));

    constexpr float icon_size = 24.f;
    constexpr float icon_height = 30.f; // layout height of the source's panorama icon
    const char* glyph = molotov ? glyph_fire : glyph_bomb;
    text_centered(draw, icon_font, icon_size, pos + ImVec2(1.f, 0.f), rgba(0.f, 0.f, 0.f, 200.f * alpha), glyph);
    text_centered(draw, icon_font, icon_size, pos - ImVec2(0.f, 1.f), rgba(255.f, 255.f, 255.f, 255.f * alpha), glyph);

    char timer_text[16];
    std::snprintf(timer_text, sizeof(timer_text), "%.1fs", std::max(0.f, seconds));
    ImVec2 timer_extent = text_size(text_font, warning_text_size, timer_text);
    timer_extent.x = std::floor(timer_extent.x);

    char damage_text[16];
    std::snprintf(damage_text, sizeof(damage_text), "-%.0f HP", local_damage);

    const ImVec2 timer_pos = pos + ImVec2(0.f, icon_height + 16.f);
    ImVec2 a{}, b{};
    if (molotov) {
        a = timer_pos - ImVec2(timer_extent.x + 7.f, 15.f);
        b = timer_pos + ImVec2(timer_extent.x + 2.f, 15.f);
    } else {
        const float damage_width = text_size(text_font, warning_text_size, damage_text).x;
        const float base_half = timer_extent.x * 0.5f + 15.f;
        const float wide_half = std::max(timer_extent.x, damage_width) * 0.5f + 15.f;
        const float half = base_half + (wide_half - base_half) * danger;
        a = timer_pos - ImVec2(half, 15.f);
        b = timer_pos + ImVec2(half, 15.f + 14.f * danger);
    }
    constexpr float rounding = 13.f;
    const ImVec2 one(1.f, 1.f);
    soft_shadow(draw, a - one, b + one, rounding, alpha);
    draw->AddRect(a - one, b + one, rgba(16.f, 16.f, 16.f, 195.f * alpha), rounding,
        ImDrawFlags_None, 2.f);
    draw->AddRectFilled(a, b, rgba(16.f, 16.f, 16.f, 185.f * alpha), rounding);

    const float text_alpha = (molotov ? 225.f : 255.f) * alpha;
    text_centered(draw, icon_font, warning_text_size,
        timer_pos - ImVec2(timer_extent.x * 0.5f + 3.f, 0.f), rgba(255.f, 255.f, 255.f, text_alpha),
        glyph_timer);
    text_centered(draw, text_font, warning_text_size, timer_pos + ImVec2(7.f, 0.f),
        rgba(255.f, 255.f, 255.f, text_alpha), timer_text);

    if (!molotov) {
        const float damage_alpha = 225.f * alpha * danger;
        if (damage_alpha >= 1.f)
            text_centered(draw, text_font, warning_text_size,
                timer_pos + ImVec2(0.f, danger * 17.f), rgba(255.f, 0.f, 0.f, damage_alpha),
                damage_text);
    }
}

// The widget hangs 90 px above its anchor; keep the whole thing on screen.
ImVec2 widget_position(const ImVec2& anchor, const ImVec2& display) {
    return ImVec2(std::clamp(anchor.x, 60.f, std::max(60.f, display.x - 60.f)),
        std::clamp(anchor.y - 90.f, 45.f, std::max(45.f, display.y - 85.f)));
}

} // namespace

// -----------------------------------------------------------------------------
//  Held grenade: the arc you are about to throw
// -----------------------------------------------------------------------------
void render_held_trajectory(ImDrawList* draw, const visuals_source::frame_data& frame,
                            const ImVec2& display) {
    const auto& settings = player_visuals::world_settings();
    const auto* prediction = prediction_at(frame, frame.held_grenade);
    if (!prediction || frame.grenade_engine_beams)
        return; // Engine Beams: the debug overlay draws this arc in world space

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
    // everyone else's grenades are the Grenade Warning's business.
    if (entity.owner_index < 0 || entity.owner_index != frame.local_index)
        return;
    if (!settings.grenade_trajectory || !settings.trajectory_thrown_grenades)
        return;
    if (frame.grenade_engine_beams)
        return;

    const auto* prediction = prediction_at(frame, entity.prediction);
    if (!prediction || prediction->path_count < 2)
        return;
    // An HE / molotov under the Grenade Warning gets its arc from there (it can
    // turn green), so it is not drawn twice.
    if (settings.grenade_proximity_warnings && prediction->warn)
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
//  Grenade Warning — the Neverlose Lua's draw_warning, for HE grenades and
//  molotovs in flight plus burning infernos:
//    * far away: the remaining arc (green once somebody takes half their health)
//      and a "N dmg" / "N m" line per player the detonation reaches;
//    * within 300 units of the landing spot: the ring + glyph + timer pill, red
//      and pulsing when the local player is the one who gets hurt;
//    * off screen the widget rides a 0.9x screen ellipse towards the grenade.
// -----------------------------------------------------------------------------
void render_grenade_warning(ImDrawList* draw, const visuals_source::frame_data& frame,
                            const ImVec2& display) {
    const auto& settings = player_visuals::world_settings();
    auto& anims = warning_anims();
    if (!settings.grenade_proximity_warnings || !frame.in_game || !frame.has_view_matrix) {
        anims.clear();
        return;
    }
    for (auto& entry : anims)
        entry.second.seen = false;

    const float now = frame.game_time;
    const float delta_time = std::clamp(ImGui::GetIO().DeltaTime, 0.f, 0.1f);
    const float thickness = std::clamp(settings.trajectory_thickness, 0.5f, 5.f);
    const float dot_radius = std::clamp(settings.trajectory_points_thickness, 1.5f, 6.f) * 0.5f;
    const int victim_total = static_cast<int>(frame.grenade_victims.size());
    ImFont* text_font = warning_text_font();

    for (const auto& entity : frame.world) {
        if (entity.kind != visuals_source::world_projectile)
            continue;
        const bool molotov = kind_of(entity.weapon_id) == grenade_kind::molotov;
        if (!molotov && entity.weapon_id != weapon_hegrenade)
            continue;
        const auto* sim = prediction_at(frame, entity.prediction);
        if (!sim || !sim->warn || sim->path_count < 2)
            continue;
        if (sim->victim_begin < 0 || sim->victim_count < 0 ||
            sim->victim_begin + sim->victim_count > victim_total)
            continue;
        const auto* victims = frame.grenade_victims.data() + sim->victim_begin;

        warning_anim& anim = anims[entity.index];
        anim.seen = true;

        const float remaining = sim->expire_time - now;
        const float local_distance = length(sub(sim->end, frame.local_origin));
        const float near_alpha = approach(anim.near_alpha, local_distance < 300.f ? 1.f : 0.f,
            delta_time);
        const float alive_alpha = approach(anim.alive_alpha, remaining > 0.f ? 1.f : 0.f,
            delta_time);

        const float local_damage = sim->local_in_range ? sim->local_damage : 0.f;
        bool hurts_local = false;
        if (molotov)
            hurts_local = sim->local_in_range && local_damage <= 12.5f;
        else
            hurts_local = local_damage > 10.f ||
                (local_damage > 0.f && local_damage >= static_cast<float>(frame.local_health));
        const float danger = approach(anim.danger, hurts_local ? 1.f : 0.f, delta_time);

        const float warning_alpha = near_alpha * alive_alpha;
        const float label_alpha = 1.f - near_alpha;

        // Per-player forecast, stacked above the landing spot.
        ImVec2 end_screen{};
        const bool end_visible = world_to_screen(frame.view_matrix, sim->end, display, end_screen);
        bool dangerous = false;
        float stack = 0.f;
        for (int i = 0; i < sim->victim_count; ++i) {
            const auto& victim = victims[i];
            char text[24];
            ImU32 ink = 0;
            if (victim.damage > 0) {
                const bool heavy = victim.damage * 2 >= victim.health;
                dangerous = dangerous || heavy;
                std::snprintf(text, sizeof(text), "%d dmg", victim.damage);
                ink = heavy ? rgba(150.f, 200.f, 60.f, 200.f * label_alpha)
                            : rgba(255.f, 255.f, 255.f, 200.f * label_alpha);
            } else if (victim.distance > 0.f && victim.distance < 4.f) {
                const bool close = victim.distance < 3.f;
                dangerous = dangerous || close;
                std::snprintf(text, sizeof(text), "%.0f m", victim.distance);
                ink = close ? rgba(150.f, 200.f, 60.f, 200.f * label_alpha)
                            : rgba(255.f, 255.f, 255.f, 225.f * label_alpha);
            } else {
                continue;
            }
            if (end_visible && label_alpha > 0.01f)
                text_glow(draw, text_font, end_screen - ImVec2(0.f, 30.f + stack), ink,
                    label_alpha, text);
            stack -= 15.f;
        }

        // The remaining arc. A local grenade under Engine Beams is already drawn in
        // world space by the debug overlay.
        const bool local_owned = entity.owner_index >= 0 && entity.owner_index == frame.local_index;
        const bool overlay_arc = frame.grenade_engine_beams && local_owned &&
            settings.trajectory_thrown_grenades;
        if (!overlay_arc) {
            const c_color ink = dangerous
                ? c_color{ 150.f / 255.f, 200.f / 255.f, 60.f / 255.f, 200.f / 255.f }
                : settings.trajectory_color;
            draw_path(draw, frame, display, *sim, ink, thickness, dot_radius);
        }

        if (alive_alpha > 0.f && warning_alpha > 0.01f) {
            const bool pulse = molotov ? hurts_local : local_damage > 0.f;
            draw_warning_widget(draw,
                widget_position(screen_or_edge(frame, display, sim->end), display), molotov,
                warning_alpha, danger, pulse, remaining, local_damage);
        }
    }

    // Burning infernos: the same widget on the nearest edge of the fire, shown
    // within 40 ft, red and pulsing within 5 ft.
    for (const auto& entity : frame.world) {
        if (entity.kind != visuals_source::world_inferno || entity.time_remaining <= 0.f)
            continue;
        // is_visible_owner(): enemy fire, our own fire, or anyone's with friendly fire.
        const bool local_owned = entity.owner_index >= 0 && entity.owner_index == frame.local_index;
        if (!frame.friendly_fire && !local_owned && entity.owner_teammate)
            continue;

        // Nearest point of the fire's footprint to the local player. The source pads
        // the outer flames by 60 units; `range` already holds the outer flame radius.
        const float reach = entity.range + 45.f;
        vector3 to_local = sub(frame.local_origin, entity.origin);
        to_local.z = 0.f;
        const float flat = length(to_local);
        vector3 nearest = entity.origin;
        if (flat > 0.001f)
            nearest = add(entity.origin, scale(to_local, std::min(flat, reach) / flat));

        const float feet = length(sub(frame.local_origin, nearest)) / 12.f;
        warning_anim& anim = anims[inferno_anim_key + entity.index];
        anim.seen = true;
        const float alpha = approach(anim.near_alpha, feet < 40.f ? 1.f : 0.f, delta_time);
        if (alpha < 0.01f)
            continue;
        const bool burning = feet < 5.f;
        const float danger = approach(anim.danger, burning ? 1.f : 0.f, delta_time);
        draw_warning_widget(draw, widget_position(screen_or_edge(frame, display, nearest), display),
            true, alpha, danger, burning, entity.time_remaining, 0.f);
    }

    for (auto it = anims.begin(); it != anims.end();) {
        if (!it->second.seen)
            it = anims.erase(it);
        else
            ++it;
    }
}

} // namespace world_visuals
