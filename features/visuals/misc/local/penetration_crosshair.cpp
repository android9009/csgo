#include "penetration_crosshair.hpp"
#include "../../../../context.hpp"
#include "../../../../sdk/interfaces/interfaces.hpp"
#include "../../../../sdk/entity/player.hpp"
#include "../../../../sdk/trace_ray.hpp"
#include "../../../../utils/math/math.hpp"
#include <mutex>

namespace PenetrationCrosshair {
namespace {
Settings g_snapshot;
std::mutex g_mutex;
} // namespace

void Publish(const Settings& s) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_snapshot = s;
}

Settings Snapshot() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_snapshot;
}

void Contribute(VisualRuntime::FxFrame& frame) {
    if (!Snapshot().enabled || !ctx.in_game || !ctx.local || !Interfaces::Engine || !Interfaces::EngineTrace) return;
    if (frame.line_count + 4 > static_cast<int>(frame.lines.size())) return;

    // The crosshair ray, the same one the tracers and the on-shot chams trace.
    QAngle angles;
    Interfaces::Engine->GetViewAngles(angles);
    const Vector direction = Math::angle_vector(angles);
    const Vector eye = ctx.local->GetEyePosition();
    Ray_t ray;
    ray.Init(eye, eye + direction * 8192.f);
    CTraceFilterSimple filter(ctx.local);
    CGameTrace trace;
    Interfaces::EngineTrace->TraceRay(ray, 0x4600400Bu, &filter, &trace);

    // Only a world surface directly in front counts; a player under the crosshair is
    // not a wall. No green cross on stone/concrete/glass - the silence is the answer.
    if (trace.fraction >= 1.f || trace.m_pEnt) return;
    if (!Detail::IsPenetrable(trace.surface.surfaceProps)) return;

    for (const auto& line : Detail::Arms())
        frame.lines[frame.line_count++] = line;
}
} // namespace PenetrationCrosshair
