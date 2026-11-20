#include "movement.hpp"
#include "../../../sdk/datatypes/c_user_cmd.hpp"
#include "../../../sdk/interfaces/interfaces.hpp"
#include "../../../sdk/trace_ray.hpp"

namespace Misc::Movement::Detail {
namespace {
// CONTENTS/MASK from the Source SDK (bspflags.h): solid world, players, windows.
constexpr unsigned int kMaskPlayersSolid = 0x200400B;

// The probe: from the feet, ahead along the direction of travel, then straight down. If that
// downward ray finds nothing (or a floor far below), the ground under the feet is ending.
EdgeProbe Probe(const Tick& tick) {
    EdgeProbe probe;
    probe.speed = tick.speed;
    if (!Interfaces::EngineTrace || tick.speed <= 1.f) return probe;

    const float yaw = YawOfVelocity(tick.velocity_x, tick.velocity_y);
    const float cos_yaw = std::cos(yaw * kDegToRad);
    const float sin_yaw = std::sin(yaw * kDegToRad);
    const Vector ahead{tick.origin.x + cos_yaw * kEdgeProbeDistance,
                       tick.origin.y + sin_yaw * kEdgeProbeDistance,
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
} // namespace

void RunEdge(const Tick& tick) {
    const auto& runtime = GetRuntime();
    const bool jump = runtime.edge_jump.load();
    const bool stop = runtime.edge_stop.load();
    if (!jump && !stop) return;
    if (tick.on_ladder || tick.noclip) return;

    const EdgeProbe probe = Probe(tick);

    if (jump && tick.on_ground && ShouldEdgeJump(true, probe)) {
        // A jump is only accepted on the ground, which is exactly where the probe is taken.
        tick.cmd->ForceButton(IN_JUMP);
    }

    if (ShouldEdgeStop(stop, probe)) {
        // Stay on this side of the edge: the full counter-strafe, so the feet never leave
        // the floor this tick.
        const StopWish wish = InstantStopWish(tick.velocity_x, tick.velocity_y, tick.view_yaw);
        if (wish.valid) {
            tick.cmd->forwardmove = wish.forward;
            tick.cmd->sidemove = wish.side;
        }
    }
}
} // namespace Misc::Movement::Detail
