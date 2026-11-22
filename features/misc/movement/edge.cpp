#include "movement.hpp"
#include "../../../sdk/datatypes/c_user_cmd.hpp"
#include "../../../sdk/interfaces/interfaces.hpp"
#include "../../../sdk/trace_ray.hpp"
#include "../../../utils/math/math.hpp"
#include <cmath>

namespace {
#define DEG2RAD(x) ((x) * (3.14159265358979323846f / 180.f))
// MASK_PLAYERSOLID: solid world + players + windows (Source bspflags.h).
constexpr unsigned int kMaskPlayersSolid = 0x200400B;
constexpr float kProbeAhead = 24.f;   // how far ahead of the feet the probe lands
constexpr float kEdgeDrop = 16.f;     // a step down deeper than this is an edge
constexpr float kMinSpeed = 90.f;     // slower than this is walking off, not jumping

struct EdgeProbe {
    bool hit_ground{};
    float drop{};
    float speed{};
};

// `main` c_movement::edge_jump is an empty stub, so there is no reference to port: this is
// this build's own forecast. A short trace along the direction of travel answers the one
// question both items need - "is the ground under my feet ending right here?"
EdgeProbe Probe(const Tick& tick) {
    EdgeProbe probe;
    probe.speed = tick.velocity.Length2D();
    if (!Interfaces::EngineTrace || probe.speed <= 1.f) return probe;

    const float yaw = std::atan2(tick.velocity.y, tick.velocity.x);
    const Vector ahead{tick.origin.x + std::cos(yaw) * kProbeAhead,
                       tick.origin.y + std::sin(yaw) * kProbeAhead,
                       tick.origin.z + 4.f};
    const Vector below{ahead.x, ahead.y, ahead.z - 128.f};

    Ray_t ray;
    ray.Init(ahead, below);
    CTraceFilterSimple filter(nullptr);
    CGameTrace trace;
    Interfaces::EngineTrace->TraceRay(ray, kMaskPlayersSolid, &filter, &trace);

    if (trace.fraction >= 1.f || trace.startsolid) {
        probe.hit_ground = trace.startsolid;
        probe.drop = probe.hit_ground ? 0.f : kEdgeDrop + 1.f;
        return probe;
    }
    probe.hit_ground = true;
    probe.drop = ahead.z - trace.endpos.z;
    return probe;
}

bool IsEdge(const EdgeProbe& probe) { return !probe.hit_ground || probe.drop > kEdgeDrop; }
} // namespace

namespace Misc::Movement::Detail {
void Edge(const Tick& tick, State& state) {
    const Settings& s = Published();
    if (tick.on_ladder || tick.noclip) return;

    const EdgeProbe probe = Probe(tick);

    if (s.edge_jump && tick.on_ground && IsEdge(probe) && probe.speed > kMinSpeed) {
        // A jump is only accepted on the ground, which is exactly where the probe is taken.
        tick.cmd->ForceButton(IN_JUMP);
    }

    if (s.edge_stop && IsEdge(probe) && probe.speed > 15.f) {
        // Stay on this side of the edge: the full counter-strafe keeps the feet on the floor.
        InstantStop(tick);
    }
    (void)state;
}
} // namespace Misc::Movement::Detail
