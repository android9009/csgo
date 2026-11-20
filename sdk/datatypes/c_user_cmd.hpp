#pragma once
// CUserCmd - one command of local input, the object ClientMode::CreateMove receives.
//
// The layout is the one this repository's `main` branch uses (structs.hpp, `class
// c_user_cmd`), which is the same client generation this build targets. CS:GO's command
// object starts with a vtable pointer (it has a virtual destructor), which is why the
// fields begin after the virtual destructor rather than at offset 0 - a struct without it
// would read every field four bytes early.
//
// Only the fields the movement code touches are named; the tail is left as padding so the
// object is never written past what the game owns.
#include <cstdint>

#include "../../utils/math/qangle.hpp"
#include "../../utils/math/vector.hpp"

// in_buttons.h (Source SDK 2013 / CS:GO). The movement code only ever touches a handful of
// these, but the full list is kept so a log line can name the button that was set.
enum ECommandButtons : int {
    IN_ATTACK = 1 << 0,
    IN_JUMP = 1 << 1,
    IN_DUCK = 1 << 2,
    IN_FORWARD = 1 << 3,
    IN_BACK = 1 << 4,
    IN_USE = 1 << 5,
    IN_CANCEL = 1 << 6,
    IN_LEFT = 1 << 7,
    IN_RIGHT = 1 << 8,
    IN_MOVELEFT = 1 << 9,
    IN_MOVERIGHT = 1 << 10,
    IN_ATTACK2 = 1 << 11,
    IN_RUN = 1 << 12,
    IN_RELOAD = 1 << 13,
    IN_ALT1 = 1 << 14,
    IN_ALT2 = 1 << 15,
    IN_SCORE = 1 << 16,
    IN_SPEED = 1 << 17,
    IN_WALK = 1 << 18,
    IN_ZOOM = 1 << 19,
    IN_WEAPON1 = 1 << 20,
    IN_WEAPON2 = 1 << 21,
    IN_BULLRUSH = 1 << 22,
    IN_GRENADE1 = 1 << 23,
    IN_GRENADE2 = 1 << 24,
};

// The four movement buttons, i.e. "the player is steering".
constexpr int kMoveButtons = IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT;

class CUserCmd {
public:
    virtual ~CUserCmd() {}

    bool HasButton(int button) const { return (buttons & button) != 0; }
    void ForceButton(int button) { buttons |= button; }
    void RemoveButton(int button) { buttons &= ~button; }

    int command_number{};     // 0 for the command the client builds but never sends
    int tick_count{};
    QAngle view_angles{};     // the angle the server uses for movement and aim
    Vector aim_direction{};
    float forwardmove{};
    float sidemove{};
    float upmove{};
    int buttons{};
    int impulse{};
    int weapon_select{};
    int weapon_sub_type{};
    int random_seed{};
    short mouse_dx{};
    short mouse_dy{};
    bool has_been_predicted{};
    QAngle head_angles{};
};
