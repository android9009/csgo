#pragma once
// The Misc tab runtime facade. This is the single header the rest of the cheat includes:
//   - main.cpp / the frame-stage hook call Misc::Init / FrameStage / Shutdown
//   - the CreateMove hook (defined in misc.cpp) calls Misc::CreateMove
//   - the menu bridge (menu/bridges/misc_runtime.hpp) calls the per-tab Publish functions
// directly (Misc::Movement::Publish etc.). One header for the whole tab, one cpp per feature.
class CUserCmd;

namespace Misc {
// One-time setup, idempotent: registers the global game-event listener and installs the
// ping-spike network detours. Features stay off until the menu publishes settings.
bool Init();
// Dispatched from the frame-stage hook after the context refresh.
void FrameStage(int stage);
// Dispatched from the CreateMove hook after the client applied input.
void CreateMove(CUserCmd* cmd);
// Teardown on unload: removes the listener/detours and restores the clan tag.
void Shutdown();
} // namespace Misc
