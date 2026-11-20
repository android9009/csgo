// Portable checks for the Visuals tab's Misc group: the third-person plan (camera distance,
// "Disable on grenade", "Force when dead"), the trace clamp, the fov policies, "Remove zoom"
// / "Force second zoom" and the aspect ratio override.
#include "features/visuals/misc/misc.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

using namespace VisualsMisc;

namespace {
bool Near(float a, float b, float epsilon = 0.01f) { return std::fabs(a - b) <= epsilon; }
} // namespace

int main() {
    const float kInf = std::numeric_limits<float>::infinity();
    const float kNan = std::numeric_limits<float>::quiet_NaN();

    // Defaults: everything off, nothing is touched.
    {
        Settings s;
        const auto plan = Detail::Plan(s, true, true, false);
        assert(!plan.active && !plan.dead_observer);
        assert(Detail::TargetFov(s, true, false) == 0);
        assert(Detail::TargetViewModelFov(s, 68.f) == 68.f);
    }

    // Third person: only for a live player in a running game.
    {
        Settings s; s.thirdperson = true;
        assert(!Detail::Plan(s, false, true, false).active);      // main menu / demo
        const auto alive = Detail::Plan(s, true, true, false);
        assert(alive.active);
        assert(alive.distance == 100.f);                          // the menu default
        assert(!alive.dead_observer);
    }

    // "Disable on grenade" keeps the first-person camera while a grenade is held.
    {
        Settings s; s.thirdperson = true;
        s.thirdperson_grenade = true;
        // The caller passes "holds a grenade AND the checkbox is on"; with the checkbox off
        // the same grenade in hand keeps third person.
        assert(!Detail::Plan(s, true, true, true).active);
        s.thirdperson_grenade = false;
        assert(Detail::Plan(s, true, true, false).active);
    }

    // Dead: no camera maths, only the in-eye -> chase observer flip, and only when asked.
    {
        Settings s; s.thirdperson = true;
        const auto plan = Detail::Plan(s, true, false, false);
        assert(!plan.active);
        assert(!plan.dead_observer);
        s.thirdperson_dead = true;
        const auto chase = Detail::Plan(s, true, false, false);
        assert(!chase.active);
        assert(chase.dead_observer);
        // Without the main switch nothing happens, dead or alive.
        s.thirdperson = false;
        assert(!Detail::Plan(s, true, false, false).dead_observer);
    }

    // The distance slider is clamped to something a camera can use.
    {
        Settings s; s.thirdperson = true;
        s.thirdperson_dist = 1.f;
        assert(Detail::Plan(s, true, true, false).distance == Detail::kDistanceMin);
        s.thirdperson_dist = 4000.f;
        assert(Detail::Plan(s, true, true, false).distance == Detail::kDistanceMax);
        s.thirdperson_dist = kNan;
        assert(!Detail::Plan(s, true, true, false).active);
    }

    // The trace fraction shortens the camera distance and is clamped into [0, 1].
    {
        assert(Detail::ClampedDistance(100.f, 1.f) == 100.f);
        assert(Detail::ClampedDistance(100.f, 0.25f) == 25.f);
        assert(Detail::ClampedDistance(100.f, 0.f) == 0.f);
        assert(Detail::ClampedDistance(100.f, 2.f) == 100.f);     // fraction above one
        assert(Detail::ClampedDistance(100.f, -1.f) == 0.f);      // startsolid
        assert(Detail::ClampedDistance(100.f, kNan) == 0.f);
        assert(Detail::ClampedDistance(-5.f, 1.f) == 0.f);
        assert(Detail::ClampedDistance(kNan, 1.f) == 0.f);
    }

    // Fov changer: absolute world fov, only for a live, unscoped player.
    {
        Settings s; s.fov_enabled = true; s.fov = 110.f;
        assert(Detail::TargetFov(s, true, false) == 110);
        assert(Detail::TargetFov(s, false, false) == 0);          // dead
        assert(Detail::TargetFov(s, true, true) == 0);            // scoped: the weapon owns it
        s.fov = 10.f;
        assert(Detail::TargetFov(s, true, false) == 60);          // slider floor
        s.fov = 500.f;
        assert(Detail::TargetFov(s, true, false) == 140);         // slider ceiling
        s.fov = kNan;
        assert(Detail::TargetFov(s, true, false) == 0);
        s.fov = 100.f; s.fov_enabled = false;
        assert(Detail::TargetFov(s, true, false) == 0);           // checkbox off
    }

    // Viewmodel fov is absolute and only replaces the engine value when enabled.
    {
        Settings s;
        assert(Detail::TargetViewModelFov(s, 68.f) == 68.f);
        s.vm_fov_enabled = true;
        s.vm_fov = 95.f;
        assert(Detail::TargetViewModelFov(s, 68.f) == 95.f);
        s.vm_fov = 5.f;
        assert(Detail::TargetViewModelFov(s, 68.f) == 60.f);
        s.vm_fov = 1000.f;
        assert(Detail::TargetViewModelFov(s, 68.f) == 140.f);
        s.vm_fov = kNan;
        assert(Detail::TargetViewModelFov(s, 68.f) == 68.f);
    }


    // Aspect ratio: `main`'s GetScreenAspectRatio detour. 0 keeps the engine's value, any
    // other percentage is the ratio itself (178 -> 16:9).
    {
        Settings s;
        assert(Near(Detail::TargetAspectRatio(s, 1.777f), 1.777f));       // off
        s.aspect = 133.f;
        assert(Near(Detail::TargetAspectRatio(s, 1.777f), 1.33f));        // 4:3
        s.aspect = 5.f;
        assert(Near(Detail::TargetAspectRatio(s, 1.777f), Detail::kAspectMin * 0.01f));
        s.aspect = 1000.f;
        assert(Near(Detail::TargetAspectRatio(s, 1.777f), Detail::kAspectMax * 0.01f));
        s.aspect = kNan;
        assert(Near(Detail::TargetAspectRatio(s, 1.777f), 1.777f));
        // A broken engine value is never replaced: there is nothing to scale from.
        s.aspect = 133.f;
        assert(Near(Detail::TargetAspectRatio(s, 0.f), 0.f));
        // A NaN engine value stays NaN: the override never invents a ratio.
        const float nan_input = Detail::TargetAspectRatio(s, kNan);
        assert(!(nan_input == nan_input));
    }

    // "Remove zoom": 0% keeps the weapon's zoom, 100% drops it and shows the world fov.
    {
        Settings s;
        assert(Detail::RemoveZoomFov(s, 40, 90) == 40);                   // slider at zero
        s.fov_scope = 100.f;
        assert(Detail::RemoveZoomFov(s, 40, 90) == 90);                   // zoom removed
        s.fov_scope = 50.f;
        assert(Detail::RemoveZoomFov(s, 40, 90) == 65);                   // halfway
        s.fov_scope = 500.f;
        assert(Detail::RemoveZoomFov(s, 40, 90) == 90);                   // clamped to 100%
        s.fov_scope = kNan;
        assert(Detail::RemoveZoomFov(s, 40, 90) == 40);
        // No reference from the game yet: hand the world fov back rather than divide by zero.
        assert(Detail::RemoveZoomFov(s, 0, 90) == 90);
    }

    // "Force second zoom": anything narrower than the session's first level is replaced by
    // it, which is what keeps a sniper on the first zoom.
    {
        Settings s; s.force_second_zoom = true;
        assert(Detail::ForcedFirstZoomFov(s, 40, 40) == 40);
        assert(Detail::ForcedFirstZoomFov(s, 40, 15) == 40);              // second level
        assert(Detail::ForcedFirstZoomFov(s, 40, 0) == 0);                // nothing observed
        assert(Detail::ForcedFirstZoomFov(s, 0, 15) == 15);               // first sample
        s.force_second_zoom = false;
        assert(Detail::ForcedFirstZoomFov(s, 40, 15) == 15);              // checkbox off
    }

    // The world fov the zoom maths uses: the changer's value, or the engine's 90.
    {
        Settings s;
        assert(Detail::WorldFov(s) == 90);
        s.fov_enabled = true; s.fov = 110.f;
        assert(Detail::WorldFov(s) == 110);
    }

    std::puts("visuals_misc_test: ok");
    return 0;
}
