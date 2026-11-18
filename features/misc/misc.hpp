#pragma once
// Misc runtime root. Each tab lives in its own folder under features/misc/ and each
// feature in that folder has its own translation unit; this file only owns the
// game-thread entry point and the failure policy shared by all of them.
//
// Everything below runs on the game thread from FrameStageNotify. No engine call is
// made from the D3D thread and nothing is allocated per frame.
namespace Misc {
// `stage` uses the client frame-stage enum: 0 FRAME_START, 4 NET_UPDATE_END,
// 5 RENDER_START. Unknown stages are ignored.
void FrameStage(int stage);

// Releases what the modules still own (currently the clan tag). Called once during
// unload, outside the game thread, so it is guarded and may do nothing.
void Shutdown();
} // namespace Misc
