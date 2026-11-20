#pragma once
// Visuals -> Misc -> Other -> "Bullet tracers", "Show impacts", "Hitmarker".
//
// The events arrive through utils/event_listener (one listener for the whole module), the
// projection happens once per frame from FRAME_RENDER_START, and the drawing is
// menu/overlays/fx_overlay.hpp. The portable half of the pipeline is features/visuals/misc/
// fx.hpp.
class IGameEvent;

namespace VisualsMisc {
namespace Effects {
    // Registers the handler with the shared game-event listener. Safe to call more than once.
    bool Setup();
    // Called by the listener for every event; ignores everything it does not own.
    void OnEvent(IGameEvent* event);
    // FRAME_RENDER_START: prunes the expired records, projects the live ones and publishes
    // them. Without a game (menu, disconnect) it publishes an empty frame.
    void Frame();
    // Drops every record: map change, disconnect, unload.
    void Reset();
} // namespace Effects
} // namespace VisualsMisc
