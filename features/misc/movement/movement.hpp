#pragma once
// The MOVEMENT group box of the Misc tab (id misc>movement): auto strafe, bhop,
// quick stop, edge jump and edge stop.
//
// The group's game-side logic (the CreateMove corrections) has been removed:
// the menu and the config keep the controls and the settings, and the menu
// bridge keeps publishing them here, but nothing in the game consumes them
// anymore. `Publish` is a no-op on purpose so the bridge and the config layer
// stay untouched.
#include <cstdint>

namespace Misc::Movement {
struct Settings {
    bool autostrafe{};
    // Bits of menu/tabs/tab_misc.cpp's "Auto strafe" combo box.
    bool strafe_viewangles{};        // "Viewangles": the look direction drives the strafe
    bool strafe_movement_keys{};     // "Movement keys": the pressed keys drive it
    bool strafe_easy{};              // "Easy strafe": ease the wish towards the view
    bool strafe_jump_release{};      // "Jump release (hold)": only while jump is held
    bool strafe_jump_release_after{}; // "Jump release (release)": only after a real release
    int smoothing{};      // 0..100, the "Smoothing" slider
    bool avoid_walls{};

    bool bhop{};
    bool quick_stop{};    // "Quick stop" - cfg.legit.quick_stop, the menu item is in this group
    bool edge_jump{};
    bool edge_stop{};
};

// Called from the menu bridge on the render thread. The feature logic is gone,
// so the values are accepted and dropped.
inline void Publish(const Settings&) {}
} // namespace Misc::Movement
