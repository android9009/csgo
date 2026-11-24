#include "viewmodel.hpp"
#include "../../../../sdk/convars.hpp"
#include "../../../../sdk/interfaces/interfaces.hpp"
#include "../../../../utils/log.hpp"
#include <array>
#include <mutex>

namespace ViewModelChanger {
namespace {
Settings g_snapshot;
std::mutex g_mutex;

struct CvarSlot {
    const char* name;
    ConVars::ConVar* var{};
    float saved{};
    bool found{};
};

// The 2023-10-11 client registers these under their plain names (verified in the dump:
// `viewmodel_fov`, `viewmodel_offset_x/y/z` are referenced; the classic `vm_offset_*` /
// `v_viewmodel_fov` spellings are NOT in the binary, which is what kept the feature dead).
// There are no rotation cvars in this build (no `viewmodel_pitch/yaw/roll` strings), so
// the three rotation slots stay "not found" and that part of the feature degrades to
// nothing.
struct State {
    bool attempted{};
    bool ready{};
    bool active{};
    CvarSlot x{"viewmodel_offset_x"};
    CvarSlot y{"viewmodel_offset_y"};
    CvarSlot z{"viewmodel_offset_z"};
    CvarSlot roll{"viewmodel_roll"};
    CvarSlot yaw{"viewmodel_yaw"};
    CvarSlot pitch{"viewmodel_pitch"};
    CvarSlot fov{"viewmodel_fov"};
    std::array<CvarSlot*, 7> slots{};
};

State& StateRef() {
    static State state;
    return state;
}

bool Ensure() {
    State& state = StateRef();
    if (state.ready) return true;
    if (!Interfaces::ConVar || state.attempted) return false;
    state.attempted = true;
    auto capture = [](CvarSlot& slot) {
        slot.var = ConVars::Find(slot.name);
        if (slot.var) {
            slot.saved = slot.var->GetFloat();
            slot.found = true;
        }
        return slot.found;
    };
    // The three offsets are the core: without them the rest of the feature is moot.
    // A missing rotation or fov cvar (an older client build) just leaves that part
    // off.
    const bool core = capture(state.x) && capture(state.y) && capture(state.z);
    capture(state.roll);
    capture(state.yaw);
    capture(state.pitch);
    capture(state.fov);
    state.slots = {&state.x, &state.y, &state.z, &state.roll, &state.yaw, &state.pitch, &state.fov};
    if (!core) {
        LOG_WARN("visuals viewmodel: vm_offset_x/y/z cvars not found; the Viewmodel changer stays inactive");
        return false;
    }
    state.ready = true;
    LOG_INFO("visuals viewmodel: cvars captured (x=%.1f y=%.1f z=%.1f fov=%s)", state.x.saved, state.y.saved,
             state.z.saved, state.fov.found ? "ok" : "missing");
    return true;
}

void Write(CvarSlot& slot, float want) {
    if (!slot.found || !slot.var) return;
    if (!Detail::Changed(slot.var->GetFloat(), want)) return;
    ConVars::SetFloat(slot.var, want);
}

void DoRestore() {
    State& state = StateRef();
    if (!state.active || !state.ready) return;
    for (auto* slot : state.slots)
        if (slot->found) ConVars::SetFloat(slot->var, slot->saved);
    state.active = false;
    LOG_INFO("visuals viewmodel: cvars handed back to the game");
}
} // namespace

void Publish(const Settings& s) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_snapshot = s;
}

Settings Snapshot() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_snapshot;
}

void FrameStage() {
    const auto s = Snapshot();
    if (!s.enabled) {
        DoRestore();
        return;
    }
    if (!Ensure()) return;
    State& state = StateRef();
    Write(state.x, s.x);
    Write(state.y, s.y);
    Write(state.z, s.z);
    Write(state.roll, s.roll);
    Write(state.yaw, s.yaw);
    Write(state.pitch, s.pitch);
    Write(state.fov, s.fov);
    if (!state.active) {
        state.active = true;
        LOG_INFO("visuals viewmodel: asserting (fov=%.0f x=%.1f y=%.1f z=%.1f roll=%.0f yaw=%.0f pitch=%.0f)", s.fov,
                 s.x, s.y, s.z, s.roll, s.yaw, s.pitch);
    }
}

void Restore() { DoRestore(); }
} // namespace ViewModelChanger
