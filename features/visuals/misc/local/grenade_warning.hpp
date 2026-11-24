#pragma once
// Visuals -> Misc -> LOCAL: "Grenade prediction" + "Grenade proximity warning".
//
// A port of the Lua grenade script (grenade.txt): the game thread simulates every
// grenade projectile (HE / flash / molotov) from its current position and velocity
// (same physics: 0.4 * sv_gravity, 0.45 elasticity, sleep rules, breakables, the
// 1.5 s / 2.0 s fuses) and paints the future path; projectiles and inferno fires
// within 350 units of the local player raise an animated proximity warning (glow
// disc, icon, remaining-fuse time, predicted damage labels) that clamps to the
// screen edge with an arrow when off screen.
//
// The game thread contributes into one VisualRuntime::FxFrame at FRAME_RENDER_START
// (trajectory lines, trajectory markers, warning discs, off-screen arrows, labels);
// menu/overlays/fx_overlay.hpp draws them. Everything that is pure math lives in
// Detail so the host tests can check it (tests/grenade_warning_test.cpp).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>

#include "../../../../sdk/visual_runtime_state.hpp"

namespace GrenadeWarning {

struct Settings {
    bool warning{};    // "Grenade proximity warning" (cfg.misc.grenade_helper)
    std::uint32_t warning_color{0xFFC1C451u}; // 0xAABBGGRR, the Lua script's yellow
    bool prediction{}; // "Grenade prediction" (cfg.misc.grenade_tracer)
    std::uint32_t prediction_hit_color{0xFFFFFFFFu};   // "hit color" (cfg.misc.tracer_color)
    std::uint32_t prediction_miss_color{0xFFFFFFFFu};  // "miss color" (cfg.misc.box_color)
};

namespace Detail {

// Projectile kinds, mirroring the Lua's "he" / "flash" / "molotov" / "fire".
inline constexpr int KindHe = 1;
inline constexpr int KindFlash = 2;
inline constexpr int KindMolotov = 3;
inline constexpr int KindFire = 4;

// Physics constants, grenade.txt as-is.
inline constexpr float kGrenadeSize = 2.f;       // hull half-extent
inline constexpr float kGravityScale = 0.4f;     // x sv_gravity
inline constexpr float kElasticity = 0.45f;      // bounce restitution
inline constexpr float kSleepSpeedSqr = 400.f;   // 20^2
inline constexpr float kStopEpsilon = 0.1f;      // velocity components below this zero out
inline constexpr int kMaxBounces = 20;
inline constexpr int kMaxIgnored = 32;
inline constexpr float kThinkInterval = 0.2f;    // fuse check period
inline constexpr float kFuseMolotov = 2.0f;
inline constexpr float kFuseTimed = 1.5f;
inline constexpr float kDetonateSlope = 0.8660254f; // cos(30 deg): steep enough to ignite
inline constexpr float kBreakDamage = 10.f;      // impact damage against breakables
inline constexpr float kBreakSlowdown = 0.4f;    // velocity kept when breaking through
inline constexpr float kTouchDamage = 1.f;       // flash touch damage
inline constexpr float kTouchSpeed = 100.f;      // min speed for flash touch damage
inline constexpr float kTouchInterval = 1.0f;    // flash touch cooldown

// Trace masks (grenade.txt).
inline constexpr unsigned int kMaskGrenade = 0x4208608Bu;
inline constexpr unsigned int kMaskSolid = 0x0200400Bu;
inline constexpr unsigned int kMaskLine = 0x0008400Bu;
inline constexpr unsigned int kMaskShot = 0x46004003u;
inline constexpr int kSolidNotStandable = 0x10;  // FSOLID_NOT_STANDABLE
inline constexpr unsigned short kSurfSky = 0x4;  // SURF_SKY

// Warning constants, grenade.txt as-is.
inline constexpr float kRange = 350.f;
inline constexpr int kMaxWarnings = 6;
inline constexpr float kDamageInterval = 0.05f;
inline constexpr float kFadeTime = 0.30f;
inline constexpr float kHeDamage = 99.f;
inline constexpr float kHeRadius = 350.f;
inline constexpr float kHeArmorRatio = 1.2f;
inline constexpr float kHeMinRadius = 100.f;
inline constexpr float kPlayerHeight = 72.f;
inline constexpr float kPlayerHalfWidth = 16.f;
inline constexpr float kInfernoMaxRange = 150.f;

struct Rgb {
    int r{}, g{}, b{};
    bool operator==(const Rgb& o) const { return r == o.r && g == o.g && b == o.b; }
};
inline constexpr Rgb kYellow{193, 196, 81};
inline constexpr Rgb kRed{255, 48, 48};
inline constexpr Rgb kGreen{163, 207, 104};

inline float Saturate(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
inline float Smoothstep(float v) { v = Saturate(v); return v * v * (3.f - 2.f * v); }
inline float EaseOutCubic(float v) { v = Saturate(v) - 1.f; return v * v * v + 1.f; }
inline int MixChannel(int a, int b, float amount) {
    return static_cast<int>(a + (b - a) * Saturate(amount) + 0.5f);
}

// The warning colour ramp: the user's "warning color" at zero risk to red when the
// danger is critical (the Lua script ramps yellow -> red).
inline Rgb MixColor(float amount, Rgb from) {
    amount = Smoothstep(amount);
    return {MixChannel(from.r, kRed.r, amount), MixChannel(from.g, kRed.g, amount),
            MixChannel(from.b, kRed.b, amount)};
}

// Exponential approach (the Lua's approach(current, target, speed, dt)).
inline float Approach(float current, float target, float speed, float dt) {
    if (dt > 0.1f) dt = 0.1f;
    return current + (target - current) * (1.f - std::exp(-speed * dt));
}

// Fuse in seconds for a kind (molotov 2.0, he/flash 1.5).
inline float Fuse(int kind) { return kind == KindMolotov ? kFuseMolotov : kFuseTimed; }

struct Vec3 {
    float x{}, y{}, z{};
    bool operator==(const Vec3& o) const { return x == o.x && y == o.y && z == o.z; }
    bool IsValid() const { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }
};

// One sweep result the simulation needs, filled by the game-thread trace.
// `end` is the stop point (the trace endpos, or start + delta when nothing is hit).
struct SweepHit {
    bool hit{};         // fraction < 1
    float fraction{1.f};
    Vec3 end{};
    Vec3 normal{0.f, 0.f, 1.f};
    int entindex{};     // <= 0 for the world
    bool standable{true};
    bool sky{};
};

// Persistent per-entity breakable state for one simulation pass (the Lua's
// `touched` table + club() + breaks_through()).
struct BreakTarget {
    int kind{};         // 0 none, 1 glass, 2 brush, 3 dynamic, 4 physics
    bool broken{};
    bool damageable{};
    float health{};
    float club{1.f};
    float minimum{};
    bool melee_immune{};

    // The Lua's club(target, damage).
    void TakeDamage(float damage) {
        if (broken || kind == 0) return;
        if (kind == 1) { // glass shatters on any touch
            health = 0.f;
            broken = true;
            return;
        }
        if (!damageable || damage < minimum) return;
        damage *= club;
        if (kind != 2 && (damage < minimum || melee_immune)) return;
        health -= damage;
        // The Lua's `ceil(health) or floor(health)`: ceil() rounds towards +inf.
        health = health < 0.f ? std::ceil(health) : std::floor(health);
        broken = health <= 0.f;
    }

    // The Lua's breaks_through(target): does the grenade keep going through it?
    bool BreaksThrough() {
        if (kind == 4 && broken) return false;
        if ((kind == 3 || kind == 4) && !damageable) return false;
        TakeDamage(kBreakDamage);
        return health <= 0.f;
    }
};

// The Lua's bounce: reflect about the normal, zero sub-epsilon components, then
// scale everything by the elasticity.
inline Vec3 Bounce(const Vec3& v, const Vec3& n) {
    const float back = (v.x * n.x + v.y * n.y + v.z * n.z) * 2.f;
    Vec3 out{v.x - n.x * back, v.y - n.y * back, v.z - n.z * back};
    out.x = std::abs(out.x) < kStopEpsilon ? 0.f : out.x;
    out.y = std::abs(out.y) < kStopEpsilon ? 0.f : out.y;
    out.z = std::abs(out.z) < kStopEpsilon ? 0.f : out.z;
    out.x *= kElasticity;
    out.y *= kElasticity;
    out.z *= kElasticity;
    return out;
}

inline float SpeedSqr(const Vec3& v) { return v.x * v.x + v.y * v.y + v.z * v.z; }

// The Lua's sleep test: a floor-like surface (nz > 0.7, or nz > 0.1 while slow)
// with speed under 20 puts the grenade to sleep.
inline bool Sleeps(const Vec3& v, float nz, bool on_solid) {
    const float ss = SpeedSqr(v);
    const bool floor_like = nz > 0.7f || (nz > 0.1f && ss < kSleepSpeedSqr);
    return on_solid && floor_like && ss < kSleepSpeedSqr;
}

// The Lua's high-speed floor rule: after a bounce off a floor-like surface while
// moving over sqrt(96000) units/s, keep (1 - along) + 0.5 of the velocity when the
// motion is mostly into the surface (along > 0.5).
inline Vec3 FloorCap(const Vec3& v, const Vec3& n) {
    const float ss = SpeedSqr(v);
    if (ss <= 96000.f) return v;
    const float along = (v.x * n.x + v.y * n.y + v.z * n.z) / std::sqrt(ss);
    if (along <= 0.5f) return v;
    const float keep = (1.f - along) + 0.5f;
    return {v.x * keep, v.y * keep, v.z * keep};
}

// Warning geometry (the Lua's warning_risk).
struct Risk { float value{}; bool critical{}; };
// `touching` is the Lua's warning.touching for fire: a pool cell within 60 u
// horizontally and 70 u vertically of the player (critical while you stand in it).
inline Risk WarningRisk(int kind, float damage, float distance, float fire_reach,
                        bool touching = false) {
    if (kind == KindHe) {
        if (damage > 8.f) return {1.f, true};
        return {Saturate(damage / 8.f), false};
    }
    if (kind == KindFire) {
        return {1.f - Saturate(distance / 200.f), touching};
    }
    if (kind == KindMolotov) {
        const float risk = 1.f - Saturate((distance - fire_reach) / std::max(kRange - fire_reach, 1.f));
        return {risk, distance <= fire_reach};
    }
    return {0.f, false};
}

// The Lua's pulse: period 0.28 s when critical, else mix(105, 38, risk) / 100 s.
struct Pulse { float value{}; float period{}; };
inline Pulse WarningPulse(float now, bool critical, float risk) {
    const float period = critical ? 0.28f : MixChannel(105, 38, risk) / 100.f;
    const float raw = 0.5f + 0.5f * std::sin(now * 3.14159265f * 2.f / period);
    return {Smoothstep(raw), period};
}

// The Lua's disc radii (screen pixels at state scale 1).
inline float DiscRadius(float risk, float pulse, float scale) {
    return (25.f + 6.f * risk + 3.f * pulse * risk) * scale;
}
inline float GlowRadius(float risk, float pulse, float scale) {
    return (44.f + 2.f * pulse * risk) * scale;
}

// The Lua's appear/fade timeline (stateless: `now` is realtime seconds).
struct AlphaState {
    float alpha{};      // 0..1
    float scale{1.f};
    // active=true: ease in over 0.24 s from visible_since.
    // active=false: fade from fade_alpha over kFadeTime from fade_started.
    static float Appear(float now, float visible_since) {
        return EaseOutCubic((now - visible_since) / 0.24f);
    }
    static float Fade(float now, float fade_started, float fade_alpha) {
        const float fading = Smoothstep((now - fade_started) / kFadeTime);
        return fade_alpha * (1.f - fading);
    }
};

// Trajectory line alpha: the 0.08 s fade behind the projectile head (the Lua's
// smoothstep((time - elapsed) / 0.08)).
inline float TrajectoryFade(float time, float elapsed) {
    return Smoothstep((time - elapsed) / 0.08f);
}

// The Lua's travelling wave: 0.55 + 0.45 * (0.5 + 0.5 * sin((progress * 3 - phase) * 2 pi)),
// phase = now * 1.8.
inline float TrajectoryWave(float progress, float now) {
    const float phase = now * 1.8f;
    return 0.55f + 0.45f * (0.5f + 0.5f * std::sin((progress * 3.f - phase) * 6.2831853f));
}

// HE falloff: exp(-dist^2 / (2 sigma^2)) with sigma = radius / 3.
inline float BlastFalloff(float distance2, float radius) {
    const float sigma = radius / 3.f;
    return std::exp(-distance2 / (2.f * sigma * sigma));
}

// The Lua's after_armor(player, damage, scale): heavy armor, then the armor split.
// `armor` is the vest points, `heavy` the kevl upgrade, `scale` the friendly-fire
// scale. Returns the damage left to deal to health.
inline float AfterArmor(float damage, float scale, bool heavy, int armor) {
    damage *= scale;
    if (heavy) damage *= 0.85f;
    if (armor <= 0 || damage <= 0.f) return damage;
    float ratio = 0.5f * kHeArmorRatio;
    float bonus = 0.5f;
    float heavy_bonus = 1.f;
    if (heavy) {
        ratio *= 0.5f;
        bonus = 0.33f;
        heavy_bonus = 0.33f;
    }
    float to_health = damage * ratio;
    float to_armor = (damage - to_health) * (bonus * heavy_bonus);
    if (to_armor > static_cast<float>(armor))
        to_health = damage - static_cast<float>(armor) / bonus;
    return to_health;
}

// Screen edge placement (the Lua's screen_position): walk from the centre towards
// the normalized target until the (margin-inset) viewport is reached and keep
// points that are already inside. `mx`/`my` are the margins as fractions of the
// screen width / height (the Lua uses 90 px on each axis). Returns the clamped
// normalized position and whether the point was outside.
struct EdgePos {
    float x{}, y{};
    bool offscreen{};
};
inline EdgePos ScreenEdge(float nx, float ny, float mx, float my) {
    EdgePos p;
    const float half_w = std::max(0.5f - mx, 0.001f);
    const float half_h = std::max(0.5f - my, 0.001f);
    const float dx = nx - 0.5f;
    const float dy = ny - 0.5f;
    const float reach = std::hypot(dx / half_w, dy / half_h);
    if (reach > 1.f) {
        p.x = 0.5f + dx / reach;
        p.y = 0.5f + dy / reach;
        p.offscreen = true;
    } else {
        p.x = nx;
        p.y = ny;
    }
    return p;
}

// Units -> metres (39.37 units per metre).
inline float ToMetres(float units) { return units / 39.37f; }

// Label texts (exact Lua formats).
inline int TimeLabel(float seconds, char* out, int capacity) {
    // "%.1f s" with the value rounded up to a tenth.
    char buffer[16];
    int n = 0;
    const float v = std::ceil(seconds * 10.f + 1e-6f) / 10.f;
    const int whole = static_cast<int>(v);
    const int tenth = static_cast<int>((v - static_cast<float>(whole)) * 10.f + 0.5f);
    if (whole == 0) {
        buffer[n++] = '0';
    } else {
        char tmp[8];
        int t = 0;
        int w = whole;
        do { tmp[t++] = static_cast<char>('0' + w % 10); w /= 10; } while (w > 0);
        while (t > 0) buffer[n++] = tmp[--t];
    }
    buffer[n++] = '.';
    buffer[n++] = static_cast<char>('0' + tenth);
    buffer[n++] = ' ';
    buffer[n++] = 's';
    if (n + 1 > capacity) n = capacity - 1;
    std::memcpy(out, buffer, static_cast<size_t>(n));
    out[n] = 0;
    return n;
}
inline int DamageLabel(int damage, char* out, int capacity) {
    // "-%d HP"
    int i = 0;
    if (i < capacity - 1) out[i++] = '-';
    int n = damage < 0 ? -damage : damage;
    char tmp[12];
    int t = 0;
    if (n == 0) tmp[t++] = '0';
    while (n > 0) { tmp[t++] = static_cast<char>('0' + n % 10); n /= 10; }
    while (t > 0 && i < capacity - 4) out[i++] = tmp[--t];
    if (i < capacity - 1) out[i++] = ' ';
    if (i < capacity - 1) out[i++] = 'H';
    if (i < capacity - 1) out[i++] = 'P';
    out[i < capacity ? i : capacity - 1] = 0;
    return i;
}
inline int MetresLabel(float metres, char* out, int capacity) {
    // "%.1f m"
    const int v = static_cast<int>(metres * 10.f + 0.5f);
    const int whole = v / 10;
    const int tenth = v % 10;
    int i = 0;
    if (i < capacity - 1) out[i++] = static_cast<char>('0' + whole);
    if (i < capacity - 1) out[i++] = '.';
    if (i < capacity - 1) out[i++] = static_cast<char>('0' + tenth);
    if (i < capacity - 1) out[i++] = ' ';
    if (i < capacity - 1) out[i++] = 'm';
    out[i < capacity ? i : capacity - 1] = 0;
    return i;
}

// Entities broken through by the simulation are skipped by later sweeps (the Lua's
// `ignore_entity` pointer list, bounded at 32).
struct IgnoreList {
    int index[kMaxIgnored]{};
    int count{};
    void Reset() { count = 0; }
    bool Add(int i) {
        if (count >= kMaxIgnored) return false;
        index[count++] = i;
        return true;
    }
};

// The simulation itself is a pure function of the sweep callback so the physics
// can be checked on the host. `sweep(pos, delta, out, ignored)` reports the first
// collision of the grenade hull moved by `delta` (delta = velocity * dt, the
// half-extent baked into the trace); it returns false only on a hard trace
// failure (the simulation degrades to the straight step), otherwise it always
// fills out.end (the stop point, or pos + delta on a miss) and out.hit. `ignored`
// is the shared skip list: the sweep may add pass-through players to it (the
// Lua's trace_solid() retraces through a hit player and remembers it), and it
// must skip everything already in it. `burn(pos)` resolves the molotov ground
// point (a downward ground trace); `classify(entindex)` returns the breakable
// state for a hit entity (kind 0 means "solid, not breakable" - the Lua's nil
// target).
struct SimConfig {
    Vec3 pos{};
    Vec3 vel{};
    float dt{0.015625f};
    float gravity{320.f};        // kGravityScale * sv_gravity
    float speed_limit{3000.f};   // sv_maxvelocity
    int kind{KindHe};
    int age_ticks{0};
};
struct SimResult {
    Vec3 end{};
    float end_time{};
    float path[1024][4]{};    // x, y, z, time (relative to now)
    int path_count{};
    float collisions[128][4]{}; // x, y, z, time
    int collision_count{};
};

inline float ClampTo(float v, float limit) {
    return v < -limit ? -limit : (v > limit ? limit : v);
}

// The Lua's simulate() - fuse timer, gravity, hull sweep, bounce/sleep, breakables,
// flash touch damage, molotov ignition and the max-bounce detonation, with the path
// and collision points recorded for drawing.
template <class SweepFn, class BurnFn, class ClassifyFn>
inline SimResult Simulate(const SimConfig& cfg, SweepFn& sweep, BurnFn burn, ClassifyFn classify) {
    SimResult r;
    const bool molotov = cfg.kind == KindMolotov;
    const float dt = cfg.dt;
    const float fuse = Fuse(cfg.kind);
    const int think_ticks = std::max(1, static_cast<int>(std::floor(0.5f + kThinkInterval / dt)));
    const int last_tick = static_cast<int>(std::ceil((fuse + 1.f) / dt));
    Vec3 pos = cfg.pos;
    Vec3 vel = cfg.vel;
    bool asleep = false;
    int bounces = 0;
    std::unordered_map<int, BreakTarget> touched;
    float next_touch = 0.f;
    IgnoreList ignored;

    auto record = [&](float time, bool collision) {
        if (r.path_count < 1024) {
            float* p = r.path[r.path_count++];
            p[0] = pos.x; p[1] = pos.y; p[2] = pos.z; p[3] = time;
        }
        if (collision && r.collision_count < 128) {
            float* p = r.collisions[r.collision_count++];
            p[0] = pos.x; p[1] = pos.y; p[2] = pos.z; p[3] = time;
        }
    };
    auto breakable_at = [&](int entindex) -> BreakTarget* {
        if (entindex <= 0) return nullptr;
        auto it = touched.find(entindex);
        if (it == touched.end()) it = touched.emplace(entindex, classify(entindex)).first;
        if (it->second.kind == 0) return nullptr; // solid: the Lua's nil target
        return &it->second;
    };
    auto ignites = [&](const SweepHit& hit, BreakTarget* target) {
        if (!molotov || hit.normal.z < kDetonateSlope || hit.sky) return false;
        return !(target && (target->kind == 1 || target->kind == 2));
    };
    auto finish = [&](float time) {
        record(time, false);
        r.end = molotov ? burn(pos) : pos;
        r.end_time = time;
    };
    // The Lua's ignite(time, entindex): igniting against an entity burns at the
    // ground point, against the world it stops where it is.
    auto ignite = [&](float time, int entindex) {
        if (entindex > 0) { finish(time); return; }
        record(time, true);
        r.end = pos;
        r.end_time = time;
    };

    for (int tick = 0; tick <= last_tick; ++tick) {
        const int elapsed = cfg.age_ticks + tick;
        if (elapsed % think_ticks == 0 && elapsed * dt > fuse) { finish(tick * dt); return r; }
        record(tick * dt, false);
        if (asleep) continue;

        const float falling = vel.z - cfg.gravity * dt;
        const Vec3 delta{vel.x * dt, vel.y * dt, (vel.z + falling) / 2.f * dt};
        vel.x = ClampTo(vel.x, cfg.speed_limit);
        vel.y = ClampTo(vel.y, cfg.speed_limit);
        vel.z = ClampTo(falling, cfg.speed_limit);

        SweepHit hit;
        if (!sweep(pos, delta, hit, ignored)) {
            // Hard trace failure: degrade to the straight step (the Lua's fallback).
            pos.x += delta.x; pos.y += delta.y; pos.z += delta.z;
            continue;
        }
        pos = hit.end;
        if (!hit.hit) continue;
        record((tick + hit.fraction) * dt, true);
        BreakTarget* target = breakable_at(hit.entindex);

        if (ignites(hit, target)) { ignite((tick + hit.fraction) * dt, hit.entindex); return r; }
        if (cfg.kind == KindFlash && target && !target->broken &&
            (target->kind != 2 || target->damageable) &&
            tick * dt >= next_touch && SpeedSqr(vel) > kTouchSpeed * kTouchSpeed) {
            target->TakeDamage(kTouchDamage);
            next_touch = tick * dt + kTouchInterval;
        }

        const bool through = target != nullptr && target->BreaksThrough();
        if (target && target->broken) ignored.Add(hit.entindex);

        if (through) {
            vel.x *= kBreakSlowdown;
            vel.y *= kBreakSlowdown;
            vel.z *= kBreakSlowdown;
        } else {
            vel = Bounce(vel, hit.normal);
            const float speed_sqr = SpeedSqr(vel);
            const bool floor_like = hit.normal.z > 0.7f ||
                                    (hit.normal.z > 0.1f && speed_sqr < kSleepSpeedSqr);
            if (floor_like && (hit.entindex <= 0 || hit.standable)) {
                if (speed_sqr < kSleepSpeedSqr) {
                    vel = Vec3{};
                    asleep = true;
                } else if (speed_sqr > 96000.f) {
                    vel = FloorCap(vel, hit.normal);
                }
            }
            if (!asleep) {
                const float rest = (1.f - hit.fraction) * dt;
                const Vec3 rest_delta{vel.x * rest, vel.y * rest, vel.z * rest};
                SweepHit rest_hit;
                if (!sweep(pos, rest_delta, rest_hit, ignored)) {
                    pos.x += rest_delta.x; pos.y += rest_delta.y; pos.z += rest_delta.z;
                } else {
                    pos = rest_hit.end;
                    if (rest_hit.hit) {
                        record((tick + 1) * dt, true);
                        if (ignites(rest_hit, breakable_at(rest_hit.entindex))) {
                            ignite((tick + 1) * dt, rest_hit.entindex);
                            return r;
                        }
                    }
                }
            }
        }

        bounces += 1;
        if (bounces > kMaxBounces) { finish(tick * dt); return r; }
    }
    finish(last_tick * dt);
    return r;
}

} // namespace Detail

void Publish(const Settings& s);
Settings Snapshot();
// Register the detonation listeners (hegrenade_detonate / molotov_detonate /
// inferno_startburn) so finished grenades fade out promptly.
void Initialize();
void Shutdown();
// Per frame, from the game thread at FRAME_RENDER_START: scan, simulate and
// contribute the trajectory + warning drawing into the frame buffer.
void Contribute(VisualRuntime::FxFrame& frame);
// Drop all tracked state (map change / disconnect / feature disabled).
void Reset();
} // namespace GrenadeWarning
