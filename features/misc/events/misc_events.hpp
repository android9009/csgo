#pragma once
// Global game-event listener for the Misc tab. Registered once with
// IGameEventManager2::AddListenerGlobal and dispatches the handful of events the misc
// features consume:
//   round_start    -> buy bot latch, movement/clantag/ping state reset
//   player_hurt    -> hurt / naded / hit logs, hit sound
//   player_death   -> naded / kill log + kill sound
//   item_purchase  -> buy log
// One header for the events/ folder.
namespace Misc::Events {
bool Init();   // returns whether the global listener was registered
void Shutdown();
} // namespace Misc::Events
