// Menu thread -> game thread publication for the visuals misc block (third person, fov,
// aspect ratio, viewmodel fov, remove/force zoom). Named publish.cpp rather than misc.cpp
// because features/misc/misc.cpp already owns that file name and MSBuild refuses two
// translation units with the same name in one project (MSB8027: both .objs would be
// called misc.obj, so a stale object could be linked into the other module).
#include "misc.hpp"
#include "effects.hpp"

namespace VisualsMisc {
namespace {
// Each field is its own atomic so a hook can never observe a half-updated selection: the
// menu thread publishes while the game thread is inside CreateMove or FrameStageNotify.
std::atomic<bool> g_thirdperson{false};
std::atomic<float> g_thirdperson_dist{100.f};
std::atomic<bool> g_thirdperson_dead{false};
std::atomic<bool> g_thirdperson_grenade{false};
std::atomic<float> g_thirdperson_transparency{0.f};
std::atomic<float> g_thirdperson_transparency_nade{0.f};
std::atomic<bool> g_fov_enabled{false};
std::atomic<float> g_fov{90.f};
std::atomic<float> g_aspect{0.f};
std::atomic<float> g_fov_scope{0.f};
std::atomic<bool> g_force_second_zoom{false};
std::atomic<bool> g_force_viewmodel_scope{false};
std::atomic<bool> g_vm_fov_enabled{false};
std::atomic<float> g_vm_fov{90.f};
std::atomic<float> g_vm_x{0.f};
std::atomic<float> g_vm_y{0.f};
std::atomic<float> g_vm_z{0.f};
std::atomic<float> g_vm_roll{0.f};
std::atomic<float> g_vm_yaw{0.f};
std::atomic<float> g_vm_pitch{0.f};
std::atomic<int> g_remove_scope{0};
std::atomic<bool> g_spread_circle{false};
std::atomic<bool> g_force_crosshair{false};
std::atomic<bool> g_disable_bob{false};
} // namespace

void Publish(const Settings& settings) {
    g_thirdperson.store(settings.thirdperson, std::memory_order_relaxed);
    g_thirdperson_dist.store(settings.thirdperson_dist, std::memory_order_relaxed);
    g_thirdperson_dead.store(settings.thirdperson_dead, std::memory_order_relaxed);
    g_thirdperson_grenade.store(settings.thirdperson_grenade, std::memory_order_relaxed);
    g_thirdperson_transparency.store(settings.thirdperson_transparency, std::memory_order_relaxed);
    g_thirdperson_transparency_nade.store(settings.thirdperson_transparency_nade,
                                          std::memory_order_relaxed);
    g_fov_enabled.store(settings.fov_enabled, std::memory_order_relaxed);
    g_fov.store(settings.fov, std::memory_order_relaxed);
    g_aspect.store(settings.aspect, std::memory_order_relaxed);
    g_fov_scope.store(settings.fov_scope, std::memory_order_relaxed);
    g_force_second_zoom.store(settings.force_second_zoom, std::memory_order_relaxed);
    g_force_viewmodel_scope.store(settings.force_viewmodel_scope, std::memory_order_relaxed);
    g_vm_fov_enabled.store(settings.vm_fov_enabled, std::memory_order_relaxed);
    g_vm_fov.store(settings.vm_fov, std::memory_order_relaxed);
    g_vm_x.store(settings.vm_x, std::memory_order_relaxed);
    g_vm_y.store(settings.vm_y, std::memory_order_relaxed);
    g_vm_z.store(settings.vm_z, std::memory_order_relaxed);
    g_vm_roll.store(settings.vm_roll, std::memory_order_relaxed);
    g_vm_yaw.store(settings.vm_yaw, std::memory_order_relaxed);
    g_vm_pitch.store(settings.vm_pitch, std::memory_order_relaxed);
    g_remove_scope.store(settings.remove_scope, std::memory_order_relaxed);
    g_spread_circle.store(settings.spread_circle, std::memory_order_relaxed);
    g_force_crosshair.store(settings.force_crosshair, std::memory_order_relaxed);
    g_disable_bob.store(settings.disable_landing_bob, std::memory_order_relaxed);
}

bool ForceCrosshairActive() { return g_force_crosshair.load(std::memory_order_relaxed); }

bool RemoveScopeActive() {
    return Detail::ScopeLines(g_remove_scope.load(std::memory_order_relaxed));
}

void Shutdown() {
    ResetThirdPerson();
    ResetFov();
    // The event records (tracers, impacts, hitmarkers) have no engine state to hand back, but
    // they do own the published frame: clear it so nothing is drawn after an unload.
    Effects::Reset();
}

namespace Detail {
Settings Snapshot() {
    Settings settings;
    settings.thirdperson = g_thirdperson.load(std::memory_order_relaxed);
    settings.thirdperson_dist = g_thirdperson_dist.load(std::memory_order_relaxed);
    settings.thirdperson_dead = g_thirdperson_dead.load(std::memory_order_relaxed);
    settings.thirdperson_grenade = g_thirdperson_grenade.load(std::memory_order_relaxed);
    settings.thirdperson_transparency = g_thirdperson_transparency.load(std::memory_order_relaxed);
    settings.thirdperson_transparency_nade =
        g_thirdperson_transparency_nade.load(std::memory_order_relaxed);
    settings.fov_enabled = g_fov_enabled.load(std::memory_order_relaxed);
    settings.fov = g_fov.load(std::memory_order_relaxed);
    settings.aspect = g_aspect.load(std::memory_order_relaxed);
    settings.fov_scope = g_fov_scope.load(std::memory_order_relaxed);
    settings.force_second_zoom = g_force_second_zoom.load(std::memory_order_relaxed);
    settings.force_viewmodel_scope = g_force_viewmodel_scope.load(std::memory_order_relaxed);
    settings.vm_fov_enabled = g_vm_fov_enabled.load(std::memory_order_relaxed);
    settings.vm_fov = g_vm_fov.load(std::memory_order_relaxed);
    settings.vm_x = g_vm_x.load(std::memory_order_relaxed);
    settings.vm_y = g_vm_y.load(std::memory_order_relaxed);
    settings.vm_z = g_vm_z.load(std::memory_order_relaxed);
    settings.vm_roll = g_vm_roll.load(std::memory_order_relaxed);
    settings.vm_yaw = g_vm_yaw.load(std::memory_order_relaxed);
    settings.vm_pitch = g_vm_pitch.load(std::memory_order_relaxed);
    settings.remove_scope = g_remove_scope.load(std::memory_order_relaxed);
    settings.spread_circle = g_spread_circle.load(std::memory_order_relaxed);
    settings.force_crosshair = g_force_crosshair.load(std::memory_order_relaxed);
    settings.disable_landing_bob = g_disable_bob.load(std::memory_order_relaxed);
    return settings;
}
} // namespace Detail
} // namespace VisualsMisc
