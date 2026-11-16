#pragma once
namespace SkinChanger {
// Runs after FRAME_NET_UPDATE_POSTDATAUPDATE_START, only for local handles.
void FrameStage();
void Reset();
void Shutdown();
}
