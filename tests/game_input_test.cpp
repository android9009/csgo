#include "../runtime/game_input.hpp"

#include <cassert>
#include <iostream>

namespace {

struct fake_panel {
    unsigned int id = 1;
    bool mouse_enabled = false;
    int writes = 0;

    void apply(game_input::mouse_input_state& state, bool menu_open) {
        const auto action = state.update(id, mouse_enabled, menu_open);
        if (action.write) {
            mouse_enabled = action.enabled;
            ++writes;
        }
    }
};

void closed_menu_never_writes() {
    game_input::mouse_input_state state;
    fake_panel panel{ 1, true, 0 };

    panel.apply(state, false);
    panel.apply(state, false);
    assert(panel.mouse_enabled);
    assert(panel.writes == 0);
    assert(!state.owns(panel.id));

    panel.mouse_enabled = false;
    panel.apply(state, false);
    assert(!panel.mouse_enabled);
    assert(panel.writes == 0);
}

void inject_in_main_menu_then_load_map() {
    game_input::mouse_input_state state;
    fake_panel panel{ 1, true, 0 };

    // Injected while the game already owns the cursor in the main menu.
    panel.apply(state, false);
    assert(panel.mouse_enabled);
    assert(panel.writes == 0);

    // Map load rebuilds the panel. The old id must not be cached forever.
    panel.id = 2;
    panel.mouse_enabled = false;
    panel.apply(state, false);
    assert(!panel.mouse_enabled);
    assert(panel.writes == 0);

    // The game's team-select UI enables mouse input. A closed cheat menu must
    // not disable it again.
    panel.mouse_enabled = true;
    panel.apply(state, false);
    assert(panel.mouse_enabled);
    assert(panel.writes == 0);
}

void menu_open_close_restores_game_value() {
    game_input::mouse_input_state state;
    fake_panel panel{ 1, false, 0 };

    panel.apply(state, true);
    assert(panel.mouse_enabled);
    assert(panel.writes == 1);
    assert(state.owns(panel.id));

    panel.apply(state, true);
    assert(panel.writes == 1);

    panel.apply(state, false);
    assert(!panel.mouse_enabled);
    assert(panel.writes == 2);
    assert(!state.owns(panel.id));

    panel.apply(state, false);
    assert(panel.writes == 2);
}

void menu_open_in_main_menu_preserves_enabled_input() {
    game_input::mouse_input_state state;
    fake_panel panel{ 1, true, 0 };

    panel.apply(state, true);
    assert(panel.mouse_enabled);
    assert(panel.writes == 0);
    // The game already owned the cursor; the menu must not schedule a restore.
    assert(!state.owns(panel.id));

    panel.apply(state, false);
    assert(panel.mouse_enabled);
    assert(panel.writes == 0);
    assert(!state.owns(panel.id));
}

void game_change_while_menu_open_is_adopted() {
    game_input::mouse_input_state state;
    fake_panel panel{ 1, false, 0 };

    panel.apply(state, true);
    assert(panel.mouse_enabled);
    assert(panel.writes == 1);

    // The game disables input underneath the open menu (map load / UI reset).
    panel.mouse_enabled = false;
    panel.apply(state, true);
    assert(panel.mouse_enabled);
    assert(panel.writes == 2);

    panel.apply(state, false);
    assert(!panel.mouse_enabled);
    assert(panel.writes == 3);
}

void game_change_before_close_is_not_overwritten() {
    game_input::mouse_input_state state;
    fake_panel panel{ 1, false, 0 };

    panel.apply(state, true);
    assert(panel.mouse_enabled);

    // The game already changed the flag before the next PaintTraverse.
    panel.mouse_enabled = false;
    panel.apply(state, false);
    assert(!panel.mouse_enabled);
    assert(panel.writes == 1);
    assert(!state.owns(panel.id));
}

void panel_change_while_open_does_not_restore_stale_id() {
    game_input::mouse_input_state state;
    fake_panel panel{ 1, false, 0 };

    panel.apply(state, true);
    assert(panel.mouse_enabled);
    assert(state.owns(1));

    panel.id = 2;
    panel.mouse_enabled = false;
    panel.apply(state, true);
    assert(panel.mouse_enabled);
    assert(state.owns(2));
    assert(!state.owns(1));

    panel.apply(state, false);
    assert(!panel.mouse_enabled);
    assert(panel.writes == 3);
}

void zero_panel_resets_state() {
    game_input::mouse_input_state state;
    fake_panel panel{ 1, false, 0 };

    panel.apply(state, true);
    assert(state.owns(1));

    panel.id = 0;
    panel.apply(state, true);
    assert(!state.owns(0));
    assert(panel.writes == 1);
}

} // namespace

int main() {
    closed_menu_never_writes();
    inject_in_main_menu_then_load_map();
    menu_open_close_restores_game_value();
    menu_open_in_main_menu_preserves_enabled_input();
    game_change_while_menu_open_is_adopted();
    game_change_before_close_is_not_overwritten();
    panel_change_while_open_does_not_restore_stale_id();
    zero_panel_resets_state();
    std::cout << "game_input: all regression tests passed\n";
    return 0;
}
