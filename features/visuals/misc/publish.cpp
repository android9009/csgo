// Menu thread -> game thread publication for the visuals misc block (third person, FOV,
// viewmodel FOV, hide local model). Named publish.cpp rather than misc.cpp because
// features/misc/misc.cpp already owns that file name and MSBuild refuses two
// translation units with the same name in one project (MSB8027: both .objs would be
// called misc.obj, so a stale object could be linked into the other module).
#include "misc.hpp"

namespace VisualsMisc {
namespace {
std::atomic<bool> g_thirdperson{false};
std::atomic<float> g_thirdperson_dist{100.f};
std::atomic<bool> g_thirdperson_dead{false};
std::atomic<bool> g_thirdperson_grenade{false};
std::atomic<bool> g_fov_enabled{false};
std::atomic<float> g_fov{90.f};
std::atomic<bool> g_vm_fov_enabled{false};
std::atomic<float> g_vm_fov{90.f};
std::atomic<bool> g_hide_local{false};
} // namespace

void Publish(const Settings& settings) {
    g_thirdperson.store(settings.thirdperson, std::memory_order_relaxed);
    g_thirdperson_dist.store(settings.thirdperson_dist, std::memory_order_relaxed);
    g_thirdperson_dead.store(settings.thirdperson_dead, std::memory_order_relaxed);
    g_thirdperson_grenade.store(settings.thirdperson_grenade, std::memory_order_relaxed);
    g_fov_enabled.store(settings.fov_enabled, std::memory_order_relaxed);
    g_fov.store(settings.fov, std::memory_order_relaxed);
    g_vm_fov_enabled.store(settings.vm_fov_enabled, std::memory_order_relaxed);
    g_vm_fov.store(settings.vm_fov, std::memory_order_relaxed);
}

bool HideLocalModel() {
    return g_hide_local.load(std::memory_order_relaxed);
}

void Shutdown() {
    g_hide_local.store(false, std::memory_order_relaxed);
}

namespace Detail {
Settings Snapshot() {
    Settings settings;
    settings.thirdperson = g_thirdperson.load(std::memory_order_relaxed);
    settings.thirdperson_dist = g_thirdperson_dist.load(std::memory_order_relaxed);
    settings.thirdperson_dead = g_thirdperson_dead.load(std::memory_order_relaxed);
    settings.thirdperson_grenade = g_thirdperson_grenade.load(std::memory_order_relaxed);
    settings.fov_enabled = g_fov_enabled.load(std::memory_order_relaxed);
    settings.fov = g_fov.load(std::memory_order_relaxed);
    settings.vm_fov_enabled = g_vm_fov_enabled.load(std::memory_order_relaxed);
    settings.vm_fov = g_vm_fov.load(std::memory_order_relaxed);
    return settings;
}

void SetHideLocalModel(bool hide) {
    g_hide_local.store(hide, std::memory_order_relaxed);
}
} // namespace Detail
} // namespace VisualsMisc
