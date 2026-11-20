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
        // Below the usable range the value blends from the engine's ratio to the floor, so a
        // slow drag off 0 is continuous; at the floor it is the floor itself.
        s.aspect = 5.f;
        const float low = Detail::TargetAspectRatio(s, 1.777f);
        assert(low < 1.777f && low > Detail::kAspectMin * 0.01f);
        s.aspect = Detail::kAspectMin;
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
        // No zoom level from the game yet: leave the picture alone (0 = write nothing)
        // instead of snapping it to the world fov for a frame.
        assert(Detail::RemoveZoomFov(s, 0, 90) == 0);
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


    // ---- the zoom pipeline, in order ------------------------------------------------
    // 40 / 90 is a sniper's first level against a 90 world fov: 0% keeps the zoom, 50% takes
    // half of it away, 100% removes it entirely.
    {
        Settings s;
        assert(Detail::ZoomFov(s, 40, 40, 90) == 40);
        s.fov_scope = 50.f;
        assert(Detail::ZoomFov(s, 40, 40, 90) == 65);
        s.fov_scope = 100.f;
        assert(Detail::ZoomFov(s, 40, 40, 90) == 90);
        s.fov_scope = 50.f;
        // The second level (15) is replaced by the first one when "Force second zoom" is on.
        s.force_second_zoom = true;
        assert(Detail::ZoomFov(s, 40, 15, 90) == 65);
        s.force_second_zoom = false;
        assert(Detail::ZoomFov(s, 40, 15, 90) == 53);   // 15 + (90-15)*0.5 = 52.5 -> 53
    }

    // 120 frames at 50%: the value must not creep. The session never takes this module's own
    // write for the game's zoom level.
    {
        Settings s; s.fov_scope = 50.f;
        Detail::ZoomSession session;
        int our_write = 0;
        int last = 0;
        for (int frame = 0; frame < 120; ++frame) {
            // The game writes its own 40 every frame (the weapon owns the field); we write 65.
            session.Observe(0x1000, 40, our_write);
            const int target = Detail::ZoomFov(s, session.first_level, session.level, 90);
            assert(target == 65);
            our_write = target;
            last = target;
        }
        assert(last == 65);
        // A weapon change starts a new session, and the first sample becomes the reference.
        session.Observe(0x2000, 30, our_write);
        assert(session.first_level == 30 && session.level == 30);
        assert(Detail::ZoomFov(s, session.first_level, session.level, 90) == 60);
    }

    // ---- "Transparency" / "+ with nades" -------------------------------------------
    {
        Settings s;
        assert(Detail::LocalModelAlpha(s, false, false) < 0);   // third person off: not ours
        assert(Detail::LocalModelAlpha(s, true, false) < 0);    // 0% = keep the model solid
        s.thirdperson_transparency = 50.f;
        assert(Detail::LocalModelAlpha(s, true, false) == 128);
        s.thirdperson_transparency_nade = 100.f;
        assert(Detail::LocalModelAlpha(s, true, true) == 0);    // "+ with nades" wins
        assert(Detail::LocalModelAlpha(s, true, false) == 128);
        s.thirdperson_transparency_nade = 0.f;
        assert(Detail::LocalModelAlpha(s, true, true) == 128);
        s.thirdperson_transparency = 500.f;                     // clamped to 100%
        assert(Detail::LocalModelAlpha(s, true, false) == 0);
    }

    // ---- the desired distance is the slider, and the trace never writes back --------
    {
        Settings s; s.thirdperson = true;
        s.thirdperson_dist = 150.f;
        assert(Near(Detail::DesiredDistance(s), 150.f));
        // Against a wall the camera comes closer ...
        assert(Near(Detail::ClampedDistance(150.f, 0.2f), 30.f));
        // ... and walking away restores the slider value, because nothing stored the clipped
        // one: the next frame asks for the desired distance again.
        assert(Near(Detail::ClampedDistance(150.f, 1.f), 150.f));
        s.thirdperson_dist = 5000.f;
        assert(Near(Detail::DesiredDistance(s), Detail::kDistanceMax));
        s.thirdperson_dist = -10.f;
        assert(Near(Detail::DesiredDistance(s), Detail::kDistanceMin));
    }

    // ---- viewmodel offsets ---------------------------------------------------------
    {
        assert(Near(Detail::ViewmodelOffset(5.f), 5.f));
        assert(Near(Detail::ViewmodelOffset(100.f), Detail::kViewmodelOffsetLimit));
        assert(Near(Detail::ViewmodelOffset(-100.f), -Detail::kViewmodelOffsetLimit));
        assert(Near(Detail::ViewmodelOffset(std::nanf("")), 0.f));
    }

    // ---- aspect ratio smoothing ----------------------------------------------------
    // A step from 1.77 to 1.78 has to arrive within a few frames and never overshoot; the
    // value handed to the renderer for the whole frame is a single number, so the engine
    // cannot see the slider's integer steps at all.
    {
        float shown = 1.77f;
        int frames = 0;
        while (shown < 1.78f - 0.0001f && frames < 60) {
            shown = Detail::SmoothAspectRatio(shown, 1.78f, 1.f / 64.f);
            ++frames;
            assert(shown <= 1.78f + 0.0001f);   // no overshoot
        }
        assert(frames < 30);                    // 40 ms time constant at 64 fps
        // A nonsense frame time leaves the value where it is instead of jumping.
        assert(Near(Detail::SmoothAspectRatio(1.7f, 1.9f, 0.f), 1.7f));
        assert(Near(Detail::SmoothAspectRatio(1.7f, 1.9f, -1.f), 1.7f));
        // Once it is close enough the target is used verbatim, so a settled slider is exact.
        assert(Near(Detail::SmoothAspectRatio(1.7799f, 1.78f, 1.f / 64.f), 1.78f, 0.0002f));
        // 178% still means 1.78: the smoothing never changes what the slider means.
        Settings s; s.aspect = 178.f;
        assert(Near(Detail::TargetAspectRatio(s, 1.333f), 1.78f));
        s.aspect = 0.f;
        assert(Near(Detail::TargetAspectRatio(s, 1.333f), 1.333f));
        // Leaving 0 is continuous: the first percent steps blend from the engine's own ratio
        // down to the floor instead of jumping straight to it.
        s.aspect = 0.5f;
        assert(Near(Detail::TargetAspectRatio(s, 1.333f), 1.333f, 0.05f));
        s.aspect = 10.f;
        const float mid = Detail::TargetAspectRatio(s, 1.333f);
        assert(mid < 1.333f && mid > 0.5f);
        s.aspect = Detail::kAspectMin;
        assert(Near(Detail::TargetAspectRatio(s, 1.333f), Detail::kAspectMin * 0.01f));
    }

    std::puts("visuals_misc_test: ok");
    return 0;
}
