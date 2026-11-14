#include "hooks.hpp"
#include "../context.hpp"
#include "../sdk/interfaces/interfaces.hpp"
#include "../sdk/entity/player.hpp"
#include "../offsets/offsets.hpp"
#include "../math/math.hpp"
#include "../utils/memory.hpp"
#include "../features/visuals/player_esp.hpp"
#include "../features/visuals/glow.hpp"
#include "../features/visuals/world.hpp"
#include "../features/menu/menu.hpp"
#include "../utils/detourhook.hpp"
#include <cstring>
namespace HooksInternal {
void __fastcall PaintTraverse(void* ecx, void* /*edx*/, unsigned int panel, bool force_repaint, bool allow_force) {
    if (auto original = Hooks::GetPaintTraverseOriginal()) original(ecx, panel, force_repaint, allow_force);
    static int last_frame = -1;
    if (!Interfaces::Engine || !Interfaces::EntityList || !Interfaces::Globals) return;
    const char* name = Interfaces::Panel ? Interfaces::Panel->GetName(panel) : nullptr;
    if (!name || std::strcmp(name, "MatSystemTopPanel") != 0) return;
    if (last_frame == Interfaces::Globals->framecount) return;
    last_frame = Interfaces::Globals->framecount;

    int width = 0, height = 0;
    Interfaces::Engine->GetScreenSize(width, height);
    ctx.screen_size = Vector2D(static_cast<float>(width), static_cast<float>(height));
    ctx.in_game = Interfaces::Engine->IsInGame() && Interfaces::Engine->IsConnected();
    if (!ctx.in_game) { ctx.reset(); Menu::Render(); return; }
    const int local_index = Interfaces::Engine->GetLocalPlayer();
    ctx.local = static_cast<CCSPlayer*>(Interfaces::EntityList->GetClientEntity(local_index));
    if (ctx.local) ctx.weapon = ctx.local->GetActiveWeapon();
    ctx.curtime = Interfaces::Globals->curtime;
    ctx.frametime = Interfaces::Globals->frametime;
    auto client_base = reinterpret_cast<std::uintptr_t>(MEM::GetModuleBase("client.dll"));
    const auto matrix_offset = offsets::Get("dwViewMatrix");
    if (client_base && matrix_offset)
        std::memcpy(&ctx.view_matrix, reinterpret_cast<const void*>(client_base + matrix_offset), sizeof(ctx.view_matrix));

    Features::Visuals::World();
    Features::Visuals::Glow();
    Features::Visuals::PlayerEsp();
    Menu::Render();
}
}
