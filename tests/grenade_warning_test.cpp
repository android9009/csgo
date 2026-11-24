// Portable checks for the grenade prediction / proximity warning port:
//   - the physics Detail (fuse, bounce, sleep, floor cap, breakables)
//   - the full Simulate() pass against a synthetic sweep (free fall, floor
//     bounces to sleep, the 20-bounce detonation, molotov ignition, glass
//     break-through with the flash touch damage)
//   - the warning maths (risk ramp, colour mix, pulse, disc radii, timeline,
//     blast falloff, the armor split, the edge placement, the label formats)
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "features/visuals/misc/local/grenade_warning.hpp"

namespace {
constexpr float kEps = 1e-3f;
bool Near(float a, float b, float eps = kEps) { return std::fabs(a - b) < eps; }

using namespace GrenadeWarning::Detail;

// A synthetic world: an optional vertical wall (entity `wall_index`) at x = wall_x
// and an optional floor at z = 0. The sweep reports the hull (half-extent 2) hit.
struct World {
    float wall_x{10.f};
    bool has_wall{true};
    int wall_index{5};
    bool has_floor{true};
    Vec3 wall_normal{-1.f, 0.f, 0.f};
    int ignored_at_last_call{0};
    int calls{0};
    bool operator()(const Vec3& pos, const Vec3& delta, SweepHit& out, const IgnoreList& ignored) {
        calls += 1;
        ignored_at_last_call = ignored.count;
        out = SweepHit{};
        out.end = {pos.x + delta.x, pos.y + delta.y, pos.z + delta.z};
        float best = 1.f;
        const auto consider = [&](float fraction, Vec3 end, Vec3 normal, int index, bool standable) {
            if (fraction < best) {
                best = fraction;
                out.hit = true;
                out.fraction = fraction;
                out.end = end;
                out.normal = normal;
                out.entindex = index;
                out.standable = standable;
            }
        };
        if (has_wall) {
            bool ignored_entity = false;
            for (int i = 0; i < ignored.count; ++i)
                if (ignored.index[i] == wall_index) ignored_entity = true;
            if (!ignored_entity) {
                const float front = pos.x + 2.f;
                const float travel = delta.x;
                if (travel > 0.f && front < wall_x && front + travel >= wall_x)
                    consider((wall_x - front) / travel, {wall_x, pos.y + delta.y * ((wall_x - front) / travel), pos.z + delta.z * ((wall_x - front) / travel)}, wall_normal, wall_index, true);
            }
        }
        if (has_floor) {
            const float bottom = pos.z - 2.f;
            const float travel = delta.z;
            if (travel < 0.f && bottom > 0.f && bottom + travel <= 0.f) {
                const float fraction = -bottom / travel;
                consider(fraction, {pos.x + delta.x * fraction, pos.y + delta.y * fraction, 2.f},
                         {0.f, 0.f, 1.f}, 0, true);
            }
        }
        return true;
    }
};

BreakTarget Glass() { return {1, false, true, 1.f, 1.f, 0.f, false}; }

// A synthetic surface that always hits at fraction 0.5 with a configurable
// normal / entity (for the ignition and the max-bounce detonation cases).
struct SlopeWorld {
    float nz{0.9f};
    float nx{0.f};
    int entindex{0};
    int ignored_at_last_call{0};
    bool operator()(const Vec3& pos, const Vec3& delta, SweepHit& out, const IgnoreList& ignored) {
        (void)pos;
        (void)delta;
        ignored_at_last_call = ignored.count;
        out = SweepHit{};
        out.end = {pos.x, pos.y, 2.f};
        out.hit = true;
        out.fraction = 0.5f;
        out.normal = {nx, 0.f, nz};
        out.entindex = entindex;
        out.standable = true;
        return true;
    }
};
} // namespace

int main() {
    // ── Fuses. ──
    assert(Near(Fuse(KindHe), 1.5f) && Near(Fuse(KindFlash), 1.5f) && Near(Fuse(KindMolotov), 2.0f));

    // ── Bounce: reflect, zero sub-epsilon components, 0.45 restitution. ──
    {
        const Vec3 v = Bounce({0.f, 0.f, -100.f}, {0.f, 0.f, 1.f});
        assert(Near(v.x, 0.f) && Near(v.y, 0.f) && Near(v.z, 45.f, 1e-2f));
        const Vec3 e = Bounce({0.05f, 0.f, -10.f}, {0.f, 0.f, 1.f});
        assert(e.x == 0.f && Near(e.y, 0.f) && Near(e.z, 4.5f, 1e-2f));
    }

    // ── Sleep: floor-like surface (nz > 0.7, or slow and nz > 0.1) and speed < 20. ──
    assert(Sleeps({10.f, 0.f, 0.f}, 0.7f, true));
    assert(!Sleeps({10.f, 0.f, 0.f}, 0.0f, true));
    assert(!Sleeps({20.f, 0.f, 0.f}, 0.7f, true));   // 400 is not < 400
    assert(!Sleeps({10.f, 0.f, 0.f}, 0.7f, false));  // not standable
    assert(Sleeps({10.f, 0.f, 0.f}, 0.2f, true));    // slow + nz > 0.1

    // ── Floor cap: over sqrt(96000) into the surface keeps (1 - along) + 0.5. ──
    {
        const Vec3 straight = FloorCap({0.f, 0.f, 400.f}, {0.f, 0.f, 1.f});
        assert(Near(straight.z, 200.f, 1e-2f)); // along = 1 -> keep 0.5
        const Vec3 grazing = FloorCap({400.f, 0.f, 100.f}, {0.f, 0.f, 1.f});
        assert(Near(grazing.x, 400.f) && Near(grazing.z, 100.f)); // along < 0.5
        const Vec3 slow = FloorCap({0.f, 0.f, 100.f}, {0.f, 0.f, 1.f});
        assert(Near(slow.z, 100.f)); // under the 96000 threshold
    }

    // ── Breakables. ──
    {
        BreakTarget glass = Glass();
        glass.TakeDamage(1.f);
        assert(glass.broken && glass.health == 0.f);
        assert(glass.BreaksThrough()); // already broken: glass shatters

        BreakTarget brush = {2, false, false, 1.f, 1.5f, 0.f, false};
        assert(!brush.BreaksThrough()); // undamageable: solid

        BreakTarget dynamic = {3, false, false, 0.f, 1.f, 0.f, false};
        assert(!dynamic.BreaksThrough());

        BreakTarget prop = {3, false, true, 5.f, 1.f, 0.f, false};
        assert(prop.BreaksThrough()); // 5 - 10 < 0
        assert(Near(prop.health, -5.f));

        BreakTarget resistant = {3, false, true, 50.f, 2.f, 0.f, false};
        assert(!resistant.BreaksThrough()); // 50 - 20 = 30
        assert(Near(resistant.health, 30.f));

        BreakTarget melee_immune = {3, false, true, 50.f, 1.f, 0.f, true};
        assert(!melee_immune.BreaksThrough());
        assert(Near(melee_immune.health, 50.f));

        // The Lua's ceil(-x) rounds toward +inf: -5.3 -> -5.
        BreakTarget frac = {3, false, true, 1.f, 1.f, 0.f, false};
        frac.health = 1.f;
        frac.TakeDamage(6.3f); // 1 - 6.3 = -5.3 -> ceil -> -5
        assert(Near(frac.health, -5.f));
    }

    // ── Simulate: free fall, no world. ──
    {
        World world;
        world.has_wall = false;
        world.has_floor = false;
        SimConfig cfg;
        cfg.pos = {0.f, 0.f, 100.f};
        cfg.vel = {0.f, 0.f, 0.f};
        cfg.dt = 1.f / 64.f;
        cfg.gravity = 320.f; // 0.4 * 800
        cfg.speed_limit = 3000.f;
        cfg.kind = KindHe;
        cfg.age_ticks = 0;
        const auto result = Simulate(cfg, world, [](const Vec3& p) { return p; }, [](int) { return BreakTarget{}; });
        // think = 13 ticks; the first multiple of 13 whose time is over the 1.5 s
        // fuse is tick 104 (91/64 s is still under it).
        assert(Near(result.end_time, 104.f / 64.f, 1e-3f));
        assert(result.path_count > 97);
        // Midpoint integration: z_n = z0 - g*dt^2*n^2/2 after n half steps.
        assert(Near(result.end.z, 100.f - 320.f * (1.f / 64.f) * (1.f / 64.f) * (104.f * 104.f) / 2.f, 0.1f));
        assert(result.end.x == 0.f && result.end.y == 0.f);
    }

    // ── Simulate: floor bounces until the grenade sleeps. ──
    {
        World world;
        world.has_wall = false;
        SimConfig cfg;
        cfg.pos = {0.f, 0.f, 10.f};
        cfg.vel = {0.f, 0.f, -100.f};
        cfg.dt = 1.f / 64.f;
        cfg.gravity = 320.f;
        cfg.speed_limit = 3000.f;
        cfg.kind = KindHe;
        cfg.age_ticks = 0;
        const auto result = Simulate(cfg, world, [](const Vec3& p) { return p; }, [](int) { return BreakTarget{}; });
        assert(result.collision_count >= 1);
        // The first collision sits the hull on the floor (z = 2).
        assert(Near(result.collisions[0][2], 2.f, 1e-2f));
        // It ends asleep on the floor: the final path point is the resting one.
        assert(Near(result.path[result.path_count - 1][2], 2.f, 1e-2f));
        assert(result.end_time > 0.f);
    }

    // ── Simulate: a non-floor-like surface never sleeps it -> 20 bounces ->
    //     the max-bounce detonation. ──
    {
        SlopeWorld wall; // always hits, normal (-1, 0, 0): never floor-like
        wall.nx = -1.f;
        wall.nz = 0.f;
        SimConfig cfg;
        cfg.pos = {0.f, 0.f, 0.f};
        cfg.vel = {1000.f, 0.f, 0.f};
        cfg.dt = 1.f / 64.f;
        cfg.gravity = 0.f;
        cfg.speed_limit = 3000.f;
        cfg.kind = KindHe;
        cfg.age_ticks = 0;
        const auto result = Simulate(cfg, wall, [](const Vec3& p) { return p; }, [](int) { return BreakTarget{}; });
        assert(result.collision_count >= 20);
        // One bounce per tick; bounces > 20 fires on tick 20.
        assert(Near(result.end_time, 20.f / 64.f, 1e-3f));
    }

    // ── Simulate: molotov on a steep world surface stops at the contact
    //     point (the Lua's ignite() without an entity: no burn trace). ──
    {
        SlopeWorld slope;
        slope.nz = 0.9f;
        slope.entindex = 0;
        SimConfig cfg;
        cfg.pos = {0.f, 0.f, 10.f};
        cfg.vel = {0.f, 0.f, -100.f};
        cfg.dt = 1.f / 64.f;
        cfg.gravity = 320.f;
        cfg.speed_limit = 3000.f;
        cfg.kind = KindMolotov;
        cfg.age_ticks = 0;
        const auto result = Simulate(cfg, slope, [](const Vec3& p) { return p; }, [](int) { return BreakTarget{}; });
        assert(result.end.x == 0.f && result.end.y == 0.f && Near(result.end.z, 2.f));
        assert(Near(result.end_time, 0.5f / 64.f, 1e-4f));
        assert(result.collision_count == 2); // the contact + the ignite record
    }

    // ── Simulate: molotov igniting against an entity burns at the
    //     find_burn_point() ground point. ──
    {
        SlopeWorld slope;
        slope.nz = 0.9f;
        slope.entindex = 7;
        SimConfig cfg;
        cfg.pos = {0.f, 0.f, 10.f};
        cfg.vel = {0.f, 0.f, -100.f};
        cfg.dt = 1.f / 64.f;
        cfg.gravity = 320.f;
        cfg.speed_limit = 3000.f;
        cfg.kind = KindMolotov;
        cfg.age_ticks = 0;
        const Vec3 marker{999.f, 999.f, 999.f};
        const auto result = Simulate(cfg, slope, [marker](const Vec3&) { return marker; }, [](int) { return BreakTarget{}; });
        assert(result.end == marker);
        assert(Near(result.end_time, 0.5f / 64.f, 1e-4f));
    }

    // ── Simulate: molotov does not ignite on a shallow surface (nz < cos 30)
    //     and keeps bouncing until the max-bounce detonation. ──
    {
        SlopeWorld slope;
        slope.nz = 0.5f;
        slope.entindex = 0;
        SimConfig cfg;
        cfg.pos = {0.f, 0.f, 10.f};
        cfg.vel = {0.f, 0.f, -100.f};
        cfg.dt = 1.f / 64.f;
        cfg.gravity = 320.f;
        cfg.speed_limit = 3000.f;
        cfg.kind = KindMolotov;
        cfg.age_ticks = 0;
        const auto result = Simulate(cfg, slope, [](const Vec3& p) { return p; }, [](int) { return BreakTarget{}; });
        // No ignition on contact: it bounces, sleeps on the shallow surface, and
        // ends at the fuse - the first multiple of the 13-tick think over 2.0 s
        // (128 ticks) is tick 130.
        assert(result.path_count > 20);
        assert(Near(result.end_time, 130.f / 64.f, 1e-3f));
    }

    // ── Simulate: glass shatters, the flash touch-damages it, the grenade
    //     breaks through (0.4 slowdown) and the broken entity is ignored. ──
    {
        World world; // wall at x = 10 is the glass
        world.has_floor = false;
        SimConfig cfg;
        cfg.pos = {0.f, 0.f, 0.f};
        cfg.vel = {400.f, 0.f, 0.f};
        cfg.dt = 1.f / 64.f;
        cfg.gravity = 0.f;
        cfg.speed_limit = 3000.f;
        cfg.kind = KindFlash; // touch damage needs the flash kind
        cfg.age_ticks = 0;
        const auto result = Simulate(cfg, world, [](const Vec3& p) { return p; }, [](int) { return Glass(); });
        // Broken glass is ignored, so the grenade passes the wall and keeps going.
        assert(world.ignored_at_last_call == 1);
        assert(result.end.x > 14.f);
        assert(result.collision_count >= 1);
    }

    // ── Warning risk (the Lua's warning_risk). ──
    {
        const auto he_deadly = WarningRisk(KindHe, 9.f, 0.f, 150.f);
        assert(he_deadly.critical && Near(he_deadly.value, 1.f));
        const auto he_half = WarningRisk(KindHe, 4.f, 0.f, 150.f);
        assert(!he_half.critical && Near(he_half.value, 0.5f));
        const auto fire = WarningRisk(KindFire, 0.f, 100.f, 150.f);
        assert(!fire.critical && Near(fire.value, 0.5f));
        // Critical only while the player stands in the pool (the Lua's touching flag).
        const auto fire_touch = WarningRisk(KindFire, 0.f, 10.f, 150.f, true);
        assert(fire_touch.critical && Near(fire_touch.value, 0.95f));
        const auto fire_near = WarningRisk(KindFire, 0.f, 10.f, 150.f, false);
        assert(!fire_near.critical && Near(fire_near.value, 0.95f));
        const auto molotov = WarningRisk(KindMolotov, 0.f, 150.f, 150.f);
        assert(molotov.critical && Near(molotov.value, 1.f));
        const auto molotov_far = WarningRisk(KindMolotov, 0.f, 350.f, 150.f);
        assert(!molotov_far.critical && Near(molotov_far.value, 0.f, 1e-2f));
    }

    // ── Colour ramp and pulse. ──
    {
        const auto yellow = MixColor(0.f, kYellow);
        assert(yellow == kYellow);
        const auto red = MixColor(1.f, kYellow);
        assert(red == kRed);
        const auto mid = MixColor(0.5f, kYellow);
        assert(mid.r > kYellow.r && mid.r < kRed.r);

        const auto critical = WarningPulse(0.f, true, 0.f);
        assert(Near(critical.period, 0.28f));
        assert(critical.value >= 0.f && critical.value <= 1.f);
        const auto calm = WarningPulse(0.f, false, 0.f);
        assert(Near(calm.period, 1.05f));
        assert(calm.value >= 0.f && calm.value <= 1.f);

        assert(Near(DiscRadius(0.f, 0.f, 1.f), 25.f));
        assert(Near(GlowRadius(0.f, 0.f, 1.f), 44.f));
        assert(Near(DiscRadius(1.f, 1.f, 1.f), 34.f));
        assert(Near(DiscRadius(1.f, 1.f, 0.5f), 17.f));
    }

    // ── Appear / fade timeline. ──
    {
        assert(Near(AlphaState::Appear(1.f, 1.f), 0.f));
        assert(Near(AlphaState::Appear(1.24f, 1.f), 1.f));
        const float mid = AlphaState::Appear(1.12f, 1.f);
        assert(mid > 0.f && mid < 1.f);
        assert(Near(AlphaState::Fade(5.f, 5.f, 1.f), 1.f));
        assert(Near(AlphaState::Fade(5.3f, 5.f, 1.f), 0.f));
        assert(Near(AlphaState::Fade(5.15f, 5.f, 0.8f), 0.4f, 1e-2f));
    }

    // ── Trajectory wave / fade. ──
    {
        assert(Near(TrajectoryFade(0.f, 0.f), 0.f));
        assert(Near(TrajectoryFade(0.08f, 0.f), 1.f));
        const float wave = TrajectoryWave(1.f, 0.f);
        assert(wave >= 0.55f - 1e-3f && wave <= 1.f + 1e-3f);
        const float wave2 = TrajectoryWave(2.f, 1.f);
        assert(wave2 >= 0.55f - 1e-3f && wave2 <= 1.f + 1e-3f);
    }

    // ── Blast falloff and the armor split. ──
    {
        assert(Near(BlastFalloff(0.f, 350.f), 1.f));
        assert(Near(BlastFalloff(350.f * 350.f, 350.f), std::exp(-4.5f), 1e-4f));

        // Plain vest: 100 * (0.5 * 1.2) = 60 to health.
        assert(Near(AfterArmor(100.f, 1.f, false, 100), 60.f, 1e-3f));
        // No vest: full damage.
        assert(Near(AfterArmor(100.f, 1.f, false, 0), 100.f, 1e-3f));
        // Heavy: *0.85, ratio halved, 0.33/0.33 split.
        const float heavy = AfterArmor(100.f, 1.f, true, 100);
        assert(Near(heavy, 25.5f, 1e-3f));
        // Friendly-fire scale applies first.
        assert(Near(AfterArmor(100.f, 0.5f, false, 0), 50.f, 1e-3f));
    }

    // ── Screen edge placement. ──
    {
        const auto inside = ScreenEdge(0.7f, 0.5f, 0.f, 0.f);
        assert(!inside.offscreen && Near(inside.x, 0.7f));
        const auto right = ScreenEdge(1.5f, 0.5f, 0.f, 0.f);
        assert(right.offscreen && Near(right.x, 1.f) && Near(right.y, 0.5f));
        // The Lua walks the raw offset back by `reach`: a 45-degree corner lands
        // at 0.5 + 1 / (2 * sqrt(2)).
        const auto corner = ScreenEdge(1.5f, 1.5f, 0.f, 0.f);
        const float corner_edge = 0.5f + 1.f / (2.f * std::sqrt(2.f));
        assert(corner.offscreen && Near(corner.x, corner_edge) && Near(corner.y, corner_edge));
        const auto margined = ScreenEdge(1.2f, 0.5f, 0.1f, 0.1f);
        assert(margined.offscreen && Near(margined.x, 0.9f, 1e-2f));
    }

    // ── Units and label formats. ──
    {
        assert(Near(ToMetres(39.37f), 1.f, 1e-4f));
        char buffer[32];
        TimeLabel(2.34f, buffer, sizeof(buffer));
        assert(std::strcmp(buffer, "2.4 s") == 0);
        TimeLabel(0.05f, buffer, sizeof(buffer));
        assert(std::strcmp(buffer, "0.1 s") == 0);
        TimeLabel(10.f, buffer, sizeof(buffer));
        assert(std::strcmp(buffer, "10.0 s") == 0);
        DamageLabel(65, buffer, sizeof(buffer));
        assert(std::strcmp(buffer, "-65 HP") == 0);
        MetresLabel(3.27f, buffer, sizeof(buffer));
        assert(std::strcmp(buffer, "3.3 m") == 0);
    }

    // ── Easing / approach helpers. ──
    {
        assert(Near(Saturate(-1.f), 0.f) && Near(Saturate(2.f), 1.f));
        assert(Near(Smoothstep(0.5f), 0.5f));
        assert(Near(EaseOutCubic(0.f), 0.f) && Near(EaseOutCubic(1.f), 1.f));
        const float step = Approach(0.f, 1.f, 10.f, 0.1f);
        assert(step > 0.f && step < 1.f);
        assert(Near(Approach(0.f, 1.f, 10.f, 0.5f), step, 1e-6f)); // dt clamps at 0.1
    }

    std::printf("grenade_warning_test: OK\n");
    return 0;
}
