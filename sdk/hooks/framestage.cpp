#include "hooks.hpp"
#include "../../features/visuals/misc_runtime.hpp"
#include "../../menu/preview/model_preview.hpp"
#include "../world_esp_state.hpp"
#include "../visual_runtime_state.hpp"
#include "../../features/skins/skin_changer.hpp"
#include "../../features/skins/local_inventory.hpp"
#include "../../context.hpp"
#include "../interfaces/interfaces.hpp"
#include "../entity/player.hpp"
#include "../../utils/log.hpp"
namespace HooksInternal {
// ClientFrameStageNotify_t: FRAME_START=0, FRAME_NET_UPDATE_START=1,
// FRAME_NET_UPDATE_POSTDATAUPDATE_START=2, FRAME_NET_UPDATE_POSTDATAUPDATE_END=3,
// FRAME_NET_UPDATE_END=4, FRAME_RENDER_START=5, FRAME_RENDER_END=6.
void __fastcall FrameStageNotify(void* ecx, void* /*edx*/, int stage) {
    VisualMisc::BeforeFrame(stage);
    if (auto original = Hooks::GetFrameStageNotifyOriginal()) original(ecx, stage);
    if (stage == 0) {if(VisualRuntime::glow_closing.load())VisualRuntime::UpdateGlow();LocalInventory::Frame();}
    if (stage == 5) VisualRuntime::UpdateGlow();
    if (stage == 6) {
        static bool first_capture=true;
        const bool trace_first=first_capture;first_capture=false;
        if(trace_first)LOG_INFO("frame startup: world snapshot");
        WorldEsp::Capture();
        if(trace_first)LOG_INFO("frame startup: player snapshot");
        VisualRuntime::CapturePlayers();
        if(trace_first)LOG_INFO("frame startup: preview");
        ModelPreview::Paint();
        if(trace_first)LOG_INFO("frame startup: capture complete");
    }
    static bool skins_failed = false;
    if (stage == 2 && !skins_failed) {
        __try { SkinChanger::FrameStage(); }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            skins_failed = true;
            LOG_ERROR("skins: frame-stage exception 0x%08lX; skin application disabled", GetExceptionCode());
        }
    }
    // SEH: entity/weapon pointers come from the game and may be stale for a
    // frame; never let a bad read crash the process from a hook.
    __try {
        ctx.frame_stage = stage;
        if (!Interfaces::Engine || !Interfaces::EntityList || !Interfaces::Globals) { VisualMisc::Frame(stage);return; }
        // Refresh the entity context once the client has applied network updates
        // (and at render start, when the local weapon may have changed).
        if (stage == 0 || stage == 4 || stage == 5) {
            ctx.in_game = Interfaces::Engine->IsInGame() && Interfaces::Engine->IsConnected();
            if (!ctx.in_game) { ctx.reset(); ctx.frame_stage = stage; VisualMisc::Frame(stage);return; }
            const int local_index = Interfaces::Engine->GetLocalPlayer();
            ctx.local = static_cast<CCSPlayer*>(Interfaces::EntityList->GetClientEntity(local_index));
            ctx.weapon = ctx.local ? ctx.local->GetActiveWeapon() : nullptr;
            ctx.weapon_data = ctx.weapon ? ctx.weapon->GetWeaponData() : nullptr;
            ctx.tickbase = ctx.local ? ctx.local->m_nTickBase() : 0;
            ctx.curtime = Interfaces::Globals->curtime;
            ctx.frametime = Interfaces::Globals->frametime;
            if (ctx.local) ctx.eye_pos = ctx.local->GetEyePosition();
            // TODO: features/visuals (glow/chams state), features/rage (lagcomp) entry points go here
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    VisualMisc::Frame(stage);
}
}
