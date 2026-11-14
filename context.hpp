#pragma once
// Глобальный контекст — по образцу Havoc HAVOCCTX (base/context.hpp) + Fatality globals.
// Один объект на весь чит, обновляется в хуках. Никаких разбросанных глобалок.
#include "math/vector.hpp"
#include "math/qangle.hpp"
#include "math/matrix.hpp"
#include "sdk/entity/player.hpp"
#include "sdk/entity/weapon.hpp"

struct cheat_ctx_t {
    // entities
    CCSPlayer* local = nullptr;
    CBaseWeapon* weapon = nullptr;
    CCSWeaponData* weapon_data = nullptr;

    // screen / view
    Vector2D screen_size{};
    VMatrix view_matrix{};
    Vector eye_pos{};
    QAngle view_angles{};

    // state
    bool in_game = false;
    bool unload = false;
    bool send_packet = true;
    bool in_createmove = false;
    int tickbase = 0;
    int command_number = 0;
    float curtime = 0.f;
    float frametime = 0.f;
    float lerp_time = 0.f;
    float max_unlag = 0.f;
    int max_choke = 14;
    int tick_rate = 64;

    // prediction backup (Havoc LocalData_t)
    struct pred_backup_t {
        int move_type = 0, flags = 0, tickbase = 0;
        Vector origin{}, velocity{};
        float duck_amount = 0.f;
    } pred{};

    void reset() {
        local = nullptr; weapon = nullptr; weapon_data = nullptr;
        in_game = false; send_packet = true; in_createmove = false;
        tickbase = 0; command_number = 0;
    }
    void save_pred() {
        if (!local) return;
        pred.move_type = local->m_MoveType();
        pred.flags = local->m_fFlags();
        pred.tickbase = local->m_nTickBase();
        pred.origin = local->m_vecOrigin();
        pred.velocity = local->m_vecVelocity();
        pred.duck_amount = local->m_flDuckAmount();
    }
    void restore_pred() {
        if (!local) return;
        local->m_MoveType() = pred.move_type;
        local->m_fFlags() = pred.flags;
        local->m_vecOrigin() = pred.origin;
        local->m_vecVelocity() = pred.velocity;
        local->m_flDuckAmount() = pred.duck_amount;
    }
};

inline cheat_ctx_t ctx;
