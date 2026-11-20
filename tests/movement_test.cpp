// Portable checks for the MOVEMENT rules: the strafe wish, the bhop state machine, the
// counter-strafe and the edge decision. All of them are pure functions, so the parts that
// decide what is written into the game's command can be pinned on a host with no game.
//
// The strafe and bhop numbers are the ones this repository's `main` branch uses
// (movement.cpp), so the expectations below are main's behaviour, not invented ones.
#include "features/misc/movement/movement.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace Misc::Movement;

namespace {
bool Near(float a, float b, float epsilon = 0.01f) { return std::fabs(a - b) <= epsilon; }
} // namespace

int main() {
    // ---- yaw helpers --------------------------------------------------------------
    assert(Near(NormalizeYaw(190.f), -170.f));
    assert(Near(NormalizeYaw(-190.f), 170.f));
    assert(Near(NormalizeYaw(360.f), 0.f));
    assert(Near(NormalizeYaw(720.f), 0.f));
    assert(Near(NormalizeYaw(0.f), 0.f));
    assert(Near(NormalizeYaw(1000.f), -80.f));       // several turns around
    assert(Near(NormalizeYaw(std::nanf("")), 0.f));  // unreadable angle is not a direction
    // Velocity pointing along +x is yaw 0, along +y is yaw 90.
    assert(Near(YawOfVelocity(100.f, 0.f), 0.f));
    assert(Near(YawOfVelocity(0.f, 100.f), 90.f));
    assert(Near(YawOfVelocity(0.f, -100.f), -90.f));

    // ---- auto strafe --------------------------------------------------------------
    // main: forwardmove = 5850 / speed, side = the full side speed with the sign of the
    // angle between velocity and view, view rotated by -diff * smooth.
    {
        // Standing still: main pushes forward so a jump from standstill builds speed.
        const StrafeWish still = AirStrafeWish(0.f, 0.f, 0.f, 450.f, 450.f, 1.f);
        assert(!still.valid);
        assert(Near(still.forward, 450.f));
        assert(Near(still.side, 0.f));
    }
    {
        // Speed 300 towards +x, looking along +x: the angle is 0, so the side is +450 and
        // the forward term is 5850 / 300 = 19.5.
        const StrafeWish wish = AirStrafeWish(300.f, 0.f, 0.f, 450.f, 450.f, 1.f);
        assert(wish.valid);
        assert(Near(wish.forward, 19.5f));
        assert(Near(wish.side, 450.f));       // diff == 0 -> not > 0 -> +side
        assert(Near(wish.view_yaw, 0.f));     // nothing to rotate
    }
    {
        // Looking 90 degrees left of the velocity: diff is +90, so the side flips and the
        // view is rotated back by -90 * smooth.
        const StrafeWish wish = AirStrafeWish(300.f, 0.f, 90.f, 450.f, 450.f, 1.f);
        assert(Near(wish.side, -450.f));
        assert(Near(wish.view_yaw, 0.f));
        // Half smoothing only rotates half as far.
        const StrafeWish half = AirStrafeWish(300.f, 0.f, 90.f, 450.f, 450.f, 0.5f);
        assert(Near(half.view_yaw, 45.f));
    }
    {
        // The forward term never exceeds cl_forwardspeed, and a slow speed clamps to it.
        const StrafeWish slow = AirStrafeWish(10.f, 0.f, 0.f, 450.f, 450.f, 1.f);
        assert(Near(slow.forward, 450.f));
        const StrafeWish negative = AirStrafeWish(-300.f, 0.f, 0.f, 450.f, 450.f, 1.f);
        assert(negative.forward <= 450.f && negative.forward >= -450.f);
        // A non-finite velocity must not produce a wish at all.
        const StrafeWish broken = AirStrafeWish(std::nanf(""), 0.f, 0.f, 450.f, 450.f, 1.f);
        assert(!broken.valid);
    }
    // main: smooth = 1 - 0.15 * (smooth_percent * 0.01), clamped to the slider's 0..100.
    assert(Near(StrafeSmoothFactor(0), 1.f));
    assert(Near(StrafeSmoothFactor(100), 0.85f));
    assert(Near(StrafeSmoothFactor(-5), 1.f));
    assert(Near(StrafeSmoothFactor(500), 0.85f));


    // ---- rotating the wish into the command's own frame ----------------------------
    // Source reads forwardmove/sidemove in cmd->view_angles' frame. A wish computed for a
    // different yaw has to be rotated, which keeps the physical direction and leaves the
    // camera angle alone (that is the whole point: the strafer must not turn the camera).
    {
        // Same frame: nothing changes.
        const MoveVector same = RotateIntoViewFrame(200.f, -450.f, 30.f, 30.f);
        assert(Near(same.forward, 200.f));
        assert(Near(same.side, -450.f));

        // A pure forward wish in a frame 90 degrees off lands on the side axis.
        const MoveVector turned = RotateIntoViewFrame(450.f, 0.f, 90.f, 0.f);
        assert(Near(turned.forward, 0.f));
        assert(Near(turned.side, -450.f));

        // And the rotation is a rotation: the length of the vector is preserved.
        for (float wish = -180.f; wish <= 180.f; wish += 15.f) {
            const MoveVector m = RotateIntoViewFrame(300.f, -200.f, wish, -77.f);
            const float len = std::sqrt(m.forward * m.forward + m.side * m.side);
            assert(Near(len, std::sqrt(300.f * 300.f + 200.f * 200.f), 0.05f));
        }

        // Round trip: rotating back gives the original wish.
        const float wish_yaw = 42.f;
        const float view_yaw = -13.f;
        const MoveVector there = RotateIntoViewFrame(120.f, 80.f, wish_yaw, view_yaw);
        const MoveVector back = RotateIntoViewFrame(there.forward, there.side, view_yaw, wish_yaw);
        assert(Near(back.forward, 120.f));
        assert(Near(back.side, 80.f));
    }

    // ---- bhop ---------------------------------------------------------------------
    {
        // The release-then-force cycle of main's auto_jump: pressing jump on the ground
        // arms the next force, and holding it in the air is taken away again.
        BhopState state;
        BhopInput input;
        input.jump_button = true;
        input.on_ground = true;
        BhopOutput out = AdvanceBhop(state, input);
        assert(!out.force_jump && !out.remove_jump);   // on the ground: nothing to change
        assert(state.should_fake && state.last_jumped);

        input.on_ground = false;
        out = AdvanceBhop(state, input);
        assert(out.remove_jump);                        // in the air: release
        assert(!state.last_jumped);

        out = AdvanceBhop(state, input);                // next tick: force the jump again
        assert(out.force_jump);
        assert(!state.should_fake);

        // Releasing the key resets both flags.
        input.jump_button = false;
        out = AdvanceBhop(state, input);
        assert(!out.force_jump && !out.remove_jump);
        assert(!state.should_fake && !state.last_jumped);
    }
    {
        // Ladders and noclip are left alone, exactly like main.
        BhopState state;
        BhopInput input;
        input.jump_button = true;
        input.on_ground = false;
        input.on_ladder = true;
        BhopOutput out = AdvanceBhop(state, input);
        assert(!out.force_jump && !out.remove_jump);
        input.on_ladder = false;
        input.noclip = true;
        out = AdvanceBhop(state, input);
        assert(!out.force_jump && !out.remove_jump);
    }

    // ---- quick stop ---------------------------------------------------------------
    // Counter-strafing: the wish is the opposite of the velocity, scaled by its speed.
    {
        const StopWish wish = InstantStopWish(300.f, 0.f, 0.f);   // moving +x, looking +x
        assert(wish.valid);
        assert(Near(wish.forward, -300.f));
        assert(Near(wish.side, 0.f));
    }
    {
        // Moving -y while looking along +x: the counter-move is -y, and in Source's frame
        // +sidemove is "right", which for yaw 0 is -y - so the wish is a negative side.
        const StopWish wish = InstantStopWish(0.f, -200.f, 0.f);
        assert(wish.valid);
        assert(Near(wish.forward, 0.f, 0.05f));
        assert(Near(wish.side, -200.f, 0.05f));
    }
    {
        const StopWish wish = InstantStopWish(0.f, 0.f, 0.f);     // standing still
        assert(!wish.valid);
        const StopWish broken = InstantStopWish(std::nanf(""), 0.f, 0.f);
        assert(!broken.valid);
    }
    // main: only on the ground for three ticks, never while steering, never twice.
    assert(ShouldQuickStop(true, 3, false, false));
    assert(!ShouldQuickStop(true, 2, false, false));   // not settled on the ground yet
    assert(!ShouldQuickStop(true, 3, true, false));    // the player is steering
    assert(!ShouldQuickStop(true, 3, false, true));    // already stopped
    assert(!ShouldQuickStop(false, 3, false, false));  // feature off
    assert(Near(kStopSpeedThreshold, 15.f));

    // ---- edge jump / edge stop ----------------------------------------------------
    {
        EdgeProbe flat;
        flat.hit_ground = true;
        flat.drop = 0.f;
        flat.speed = 300.f;
        assert(!IsEdge(flat));
        assert(!ShouldEdgeJump(true, flat));
        assert(!ShouldEdgeStop(true, flat));
    }
    {
        EdgeProbe edge;
        edge.hit_ground = true;
        edge.drop = kEdgeDrop + 1.f;
        edge.speed = 300.f;
        assert(IsEdge(edge));
        assert(ShouldEdgeJump(true, edge));
        assert(ShouldEdgeStop(true, edge));
        // Off, or too slow to be running off the edge.
        assert(!ShouldEdgeJump(false, edge));
        assert(!ShouldEdgeStop(false, edge));
        edge.speed = kEdgeMinSpeed - 1.f;
        assert(!ShouldEdgeJump(true, edge));
        edge.speed = kStopSpeedThreshold - 1.f;
        assert(!ShouldEdgeStop(true, edge));
    }
    {
        // No floor found at all below the probe is an edge as well.
        EdgeProbe void_below;
        void_below.hit_ground = false;
        void_below.drop = 0.f;
        void_below.speed = 400.f;
        assert(IsEdge(void_below));
        assert(ShouldEdgeJump(true, void_below));
    }
    // A step down shallower than kEdgeDrop is a step, not an edge.
    {
        EdgeProbe step;
        step.hit_ground = true;
        step.drop = kEdgeDrop - 1.f;
        step.speed = 400.f;
        assert(!IsEdge(step));
    }

    // ---- the four air-strafe modes ------------------------------------------------
    // Direction comes from the look direction or from the pressed keys; the camera is only
    // ever read. Everything below is what autostrafe.cpp calls.
    {
        // "Viewangles": the base direction *is* the live look direction, so a manual mouse
        // turn is followed and never compensated.
        Detail::Tick tick; tick.view_yaw = 37.f;
        const auto keys = MovementKeyDirection(tick.view_yaw, false, false, false, false);
        assert(!keys.valid);                                   // no keys, no direction
        float steer = 0.f; bool steer_valid = false;
        auto direction = ResolveStrafeDirection(StrafeSource::Viewangles, tick, keys, false, 20.f,
                                                steer, steer_valid);
        assert(direction.valid && Near(direction.yaw, 37.f));
        tick.view_yaw = -150.f;
        direction = ResolveStrafeDirection(StrafeSource::Viewangles, tick, keys, false, 20.f,
                                           steer, steer_valid);
        assert(Near(direction.yaw, -150.f));                    // follows the player's mouse

        // "Movement keys": W+D is the forward-right diagonal, S points backwards, opposite
        // keys cancel, and the look direction does not take part.
        const auto wd = MovementKeyDirection(90.f, true, false, false, true);
        assert(wd.valid && Near(wd.yaw, 45.f));
        const auto s_only = MovementKeyDirection(90.f, false, true, false, false);
        assert(s_only.valid && Near(s_only.yaw, -90.f));
        const auto cancel = MovementKeyDirection(90.f, true, true, true, true);
        assert(!cancel.valid);
        tick.view_yaw = 0.f;
        direction = ResolveStrafeDirection(StrafeSource::MovementKeys, tick, wd, false, 20.f,
                                           steer, steer_valid);
        assert(direction.valid && Near(direction.yaw, 45.f));    // look 0, strafe 45
    }

    // "Easy strafe" eases the wish towards the look direction by at most the step, and takes
    // the short way around: 179 -> -179 is a two degree turn, not 358.
    {
        assert(Near(RotateTowardsYaw(179.f, -179.f, 2.f), -179.f));   // shortest arc
        assert(Near(RotateTowardsYaw(179.f, -179.f, 1.f), 180.f));
        assert(Near(RotateTowardsYaw(-179.f, 179.f, 1.f), -180.f));
        assert(Near(RotateTowardsYaw(0.f, 90.f, 20.f), 20.f));        // clamped to the step
        assert(Near(RotateTowardsYaw(0.f, 10.f, 20.f), 10.f));        // close enough
        assert(Near(EasyStrafeStep(0), 20.f));
        assert(Near(EasyStrafeStep(100), 2.f));

        Detail::Tick tick; tick.view_yaw = 90.f;
        float steer = 0.f; bool steer_valid = false;
        // D held: the key direction is 90 degrees right of the view (0 while the view is 90),
        // so "Easy strafe" has somewhere to ease to.
        const auto keys = MovementKeyDirection(tick.view_yaw, false, false, false, true);
        auto direction = ResolveStrafeDirection(StrafeSource::MovementKeys, tick, keys, true,
                                                EasyStrafeStep(50), steer, steer_valid);
        assert(Near(direction.yaw, 11.f));                            // one step towards the view
        for (int i = 0; i < 10; ++i)
            direction = ResolveStrafeDirection(StrafeSource::MovementKeys, tick, keys, true,
                                               EasyStrafeStep(50), steer, steer_valid);
        assert(Near(direction.yaw, 90.f));                            // and then it arrives
    }

    // "Jump release" is a permission, not a command: it decides whether the strafer runs at
    // all, and "after release" only accepts a real press-and-release in the air.
    {
        JumpGateState gate;
        // Never pressed: neither mode allows the strafe.
        gate.Update(false, false);
        assert(!gate.Allows(JumpGate::WhileHeld, false));
        assert(!gate.Allows(JumpGate::AfterRelease, false));
        assert(gate.Allows(JumpGate::Off, false));

        // Pressed and still held: "while held" runs, "after release" waits.
        gate.Update(true, false);
        assert(gate.Allows(JumpGate::WhileHeld, true));
        assert(!gate.Allows(JumpGate::AfterRelease, true));

        // Released in the air: "after release" runs, "while held" stops.
        gate.Update(false, false);
        assert(!gate.Allows(JumpGate::WhileHeld, false));
        assert(gate.Allows(JumpGate::AfterRelease, false));

        // Back on the ground the edges are forgotten, so the next jump starts clean.
        gate.Update(false, true);
        assert(!gate.Allows(JumpGate::AfterRelease, false));

        // A jump that was never let go of in the air does not enable "after release".
        JumpGateState held;
        held.Update(true, true);
        held.Update(true, false);
        assert(!held.Allows(JumpGate::AfterRelease, true));

        // The gate is chosen from the two menu items; "after release" wins if both are on.
        assert(GateFrom(false, false) == JumpGate::Off);
        assert(GateFrom(true, false) == JumpGate::WhileHeld);
        assert(GateFrom(false, true) == JumpGate::AfterRelease);
        assert(GateFrom(true, true) == JumpGate::AfterRelease);
    }

    // The move vector that reaches the command never carries a camera rotation: the wish is
    // rotated into the command's own frame instead, so the view is untouched.
    {
        Detail::Tick tick; tick.view_yaw = 120.f;
        const auto keys = MovementKeyDirection(tick.view_yaw, true, false, false, false);
        float steer = 0.f; bool steer_valid = false;
        const auto direction = ResolveStrafeDirection(StrafeSource::MovementKeys, tick, keys,
                                                      false, 0.f, steer, steer_valid);
        assert(Near(direction.yaw, 120.f));
        const auto move = RotateIntoViewFrame(100.f, 0.f, 120.f, 120.f);
        assert(Near(move.forward, 100.f));      // same frame: unchanged
        assert(Near(move.side, 0.f));
    }

    std::puts("movement_test: ok");
    return 0;
}
