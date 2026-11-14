#include "hooks.hpp"
#include "../context.hpp"
#include "../sdk/structs.hpp"
#include "../sdk/interfaces/interfaces.hpp"
#include "../features/misc/movement.hpp"
#include "../features/rage/ragebot.hpp"
#include "../features/rage/antiaim.hpp"
#include "../utils/detourhook.hpp"
namespace HooksInternal {
bool __fastcall CreateMove(void* ecx, void* /*edx*/, float sample_time, CUserCmd* cmd) {
    const bool result = Hooks::GetCreateMoveOriginal() ? Hooks::GetCreateMoveOriginal()(ecx, sample_time, cmd) : false;
    if (!cmd || !cmd->command_number || !Interfaces::Engine || !Interfaces::EntityList) return result;
    ctx.in_game = Interfaces::Engine->IsInGame() && Interfaces::Engine->IsConnected();
    ctx.command_number = cmd->command_number;
    ctx.view_angles = cmd->viewangles;
    if (!ctx.in_game) { ctx.reset(); return result; }
    const int local_index = Interfaces::Engine->GetLocalPlayer();
    ctx.local = static_cast<CCSPlayer*>(Interfaces::EntityList->GetClientEntity(local_index));
    ctx.weapon = ctx.local ? ctx.local->GetActiveWeapon() : nullptr;
    ctx.tickbase = ctx.local ? ctx.local->m_nTickBase() : 0;
    if (Interfaces::Globals) {
        ctx.curtime = Interfaces::Globals->curtime;
        ctx.frametime = Interfaces::Globals->frametime;
        ctx.tick_rate = Interfaces::Globals->interval_per_tick > 0.f
            ? static_cast<int>(1.f / Interfaces::Globals->interval_per_tick) : 64;
    }
    if (ctx.local && ctx.local->IsAlive()) {
        Features::Misc::Movement(cmd);
        Features::Rage::Ragebot(cmd);
        Features::Rage::AntiAim(cmd);
    }
    ctx.view_angles = cmd->viewangles;
    return result;
}
}
