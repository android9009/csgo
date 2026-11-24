#include "hooks.hpp"
#include "../../context.hpp"
#include "../interfaces/interfaces.hpp"
#include "../entity/player.hpp"
#include "../visual_runtime_state.hpp"
#include "../../utils/offsets/offsets.hpp"
#include "../../utils/math/math.hpp"
#include "../../utils/memory.hpp"
#include "../../utils/detourhook.hpp"
#include <cstring>
namespace HooksInternal {
void __fastcall PaintTraverse(void* ecx, void* /*edx*/, unsigned int panel, bool force_repaint, bool allow_force) {
    // Visuals -> Misc -> LOCAL "Remove scope": the HudZoom panel draws the scope's VGUI
    // overlay (the frame + vignette on top of the scoped picture). The `main` branch
    // drops its paint for a sniper while the removal is on; every other panel and every
    // other state is forwarded to the original untouched. The reads are entity-based and
    // happen before this frame's ctx.local is set (below), so they run under SEH and fail
    // closed to the original paint.
    {
        bool skip_panel = false;
        __try {
            if (VisualRuntime::scope_removal.load(std::memory_order_relaxed)) {
                const char* panel_name = Interfaces::Panel ? Interfaces::Panel->GetName(panel) : nullptr;
                skip_panel = panel_name && std::strcmp(panel_name, "HudZoom") == 0 &&
                    ctx.local && ctx.local->IsAlive() &&
                    ctx.local->GetActiveWeapon() && ctx.local->GetActiveWeapon()->IsSniper();
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            skip_panel = false;
        }
        if (skip_panel) return;
    }
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
        // The global is the 48-byte matrix3x4 the client loads as its live view matrix;
        // capture_view_matrix fills row 3 with [0,0,0,1] (see utils/math/math.cpp).
        if (client_base && matrix_offset)
            Math::capture_view_matrix(reinterpret_cast<const void*>(client_base + matrix_offset));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}
}
