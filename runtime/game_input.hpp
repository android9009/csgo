#pragma once

// Portable ownership state for the game's VGUI mouse-input flag.
//
// The old runtime wrote `false` into FocusOverlayPanel once while the cheat
// menu was closed. That is not a restore: the game may legitimately need mouse
// input enabled in the main menu / team-select UI, and the panel can be rebuilt
// when a map loads. This state machine only writes while the menu owns input,
// captures the game's current value before enabling it, and restores that value
// on close. It never touches a panel while the menu is closed and unowned.
namespace game_input {

class mouse_input_state {
public:
    struct action {
        bool write = false;
        bool enabled = false;
    };

    // `panel` is the current FocusOverlayPanel id, `current_enabled` is the
    // value read from IPanel::IsMouseInputEnabled, and `menu_open` is the cheat
    // menu state. The returned action is the only write the caller should make.
    action update(unsigned int panel, bool current_enabled, bool menu_open) {
        if (panel == 0) {
            reset();
            return {};
        }

        // A new panel id means the old cached restore value is stale. Drop
        // ownership instead of writing to a panel that may no longer exist.
        if (panel != panel_) {
            panel_ = panel;
            owned_ = false;
            original_enabled_ = false;
            expected_enabled_ = false;
        }

        if (menu_open) {
            if (!owned_) {
                // The game already has mouse input enabled (main menu / team
                // select). Do not take ownership or schedule a restore: we have
                // not changed anything, so closing the menu must be a no-op.
                if (current_enabled)
                    return {};

                original_enabled_ = false;
                owned_ = true;
                expected_enabled_ = true;
                return { true, true };
            }

            if (current_enabled != expected_enabled_) {
                // The game changed the flag underneath us (map load, team
                // select, console, etc.). Adopt that as the new restore value.
                original_enabled_ = current_enabled;
            }

            expected_enabled_ = true;
            if (!current_enabled)
                return { true, true };
            return {};
        }

        if (owned_) {
            owned_ = false;
            // Only restore if the flag still holds the value we last wrote.
            // If the game already changed it, leave the game's value alone.
            if (current_enabled == expected_enabled_ && original_enabled_ != current_enabled)
                return { true, original_enabled_ };
        }

        return {};
    }

    void reset() {
        panel_ = 0;
        owned_ = false;
        original_enabled_ = false;
        expected_enabled_ = false;
    }

    bool owns(unsigned int panel) const {
        return owned_ && panel_ == panel;
    }

private:
    unsigned int panel_ = 0;
    bool owned_ = false;
    bool original_enabled_ = false;
    bool expected_enabled_ = false;
};

} // namespace game_input
