#include "hooks.hpp"
#include "../../features/visuals/misc_runtime.hpp"
#include "../../context.hpp"
#include "../interfaces/interfaces.hpp"
#include "../entity/player.hpp"
#include "../../utils/offsets/offsets.hpp"
#include "../../utils/math/math.hpp"
#include "../../utils/memory.hpp"
#include "../../utils/detourhook.hpp"
#include <cstring>
namespace HooksInternal {
void __fastcall PaintTraverse(void* ecx, void* /*edx*/, unsigned int panel, bool force_repaint, bool allow_force) {
    __try{const char* name=Interfaces::Panel ? Interfaces::Panel->GetName(panel) : nullptr;if(VisualMisc::HideScopePanel(name))return;}
    __except(EXCEPTION_EXECUTE_HANDLER){}
    if (auto original = Hooks::GetPaintTraverseOriginal()) original(ecx, panel, force_repaint, allow_force);
    // SEH: panel/entity pointers come from the game and may be transient; the
    // raw view-matrix read must never crash the process from a hook.
    __try {
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
        if (!ctx.in_game) { ctx.reset(); return; }
        const int local_index = Interfaces::Engine->GetLocalPlayer();
        ctx.local = static_cast<CCSPlayer*>(Interfaces::EntityList->GetClientEntity(local_index));
        if (ctx.local) ctx.weapon = ctx.local->GetActiveWeapon();
        ctx.curtime = Interfaces::Globals->curtime;
        ctx.frametime = Interfaces::Globals->frametime;
        auto client_base = reinterpret_cast<std::uintptr_t>(MEM::GetModuleBase("client.dll"));
        const auto matrix_offset = offsets::Get("dwViewMatrix");
        // TODO: features/visuals entry points go here
        if (client_base && matrix_offset)
            std::memcpy(&ctx.view_matrix, reinterpret_cast<const void*>(client_base + matrix_offset), sizeof(ctx.view_matrix));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}
}
