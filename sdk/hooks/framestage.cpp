#include "hooks.hpp"
#include "../../menu/preview/model_preview.hpp"
#include "../visual_runtime_state.hpp"
#include "../../features/skins/skin_changer.hpp"
#include "../../features/skins/local_inventory.hpp"
#include "../../features/visuals/misc/other/removals.hpp"
#include "../../features/visuals/misc/other/model_transparency.hpp"
#include "../../features/visuals/misc/world/world_fog.hpp"
#include "../../features/visuals/misc/world/world_modulation.hpp"
#include "../../features/visuals/misc/local/thirdperson.hpp"
#include "../../features/visuals/misc/local/fov.hpp"
#include "../../features/visuals/misc/local/viewmodel.hpp"
#include "../../features/visuals/misc/local/force_crosshair.hpp"
#include "../../features/visuals/misc/local/penetration_crosshair.hpp"
#include "../../features/visuals/misc/other/out_of_fov.hpp"
#include "../../features/visuals/misc/other/tracers.hpp"
#include "../../features/visuals/misc/other/impacts.hpp"
#include "../../features/visuals/misc/other/hitmarker.hpp"
#include "../../features/misc/misc.hpp"
#include "../../context.hpp"
#include "../interfaces/interfaces.hpp"
#include "../entity/player.hpp"
#include "../../utils/log.hpp"
namespace HooksInternal {
// ClientFrameStageNotify_t: FRAME_START=0, FRAME_NET_UPDATE_START=1,
// FRAME_NET_UPDATE_POSTDATAUPDATE_START=2, FRAME_NET_UPDATE_POSTDATAUPDATE_END=3,
// FRAME_NET_UPDATE_END=4, FRAME_RENDER_START=5, FRAME_RENDER_END=6.
void __fastcall FrameStageNotify(void* ecx, void* /*edx*/, int stage) {
    if (auto original = Hooks::GetFrameStageNotifyOriginal()) original(ecx, stage);
    // The D3D device may not have existed at hook-setup time; re-attach the EndScene
    // hook (idempotent no-op once attached) so the menu can render.
    Hooks::EnsureDeviceHooks();
    if (stage == 0) {if(VisualRuntime::glow_closing.load())VisualRuntime::UpdateGlow();LocalInventory::Frame();}
    if (stage == 5) VisualRuntime::UpdateGlow();
    // Visuals -> World -> "Fog": the client reads fog_override/fog_color while it builds the
    // frame's fog, so the override is asserted at render start. With no game running (main
    // menu, disconnect) the cvars are handed straight back.
    // Visuals -> World: "Fullbright" rides the engine's own mat_fullbright switch, kept in step
    // here (the tint half runs inside the GetColorModulation detour).
    if (stage == 5) {
        __try { WorldModulation::Frame(); }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            static bool logged = false;
            if (!logged) { logged = true; LOG_WARN("visuals world: fullbright cvar write raised an exception"); }
        }
    }
    if (stage == 5) {
        const auto effects = VisualRuntime::GetEffects();
        __try { WorldFog::Apply(effects.world_fog, effects.world_fog_colour); }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            WorldFog::Restore();
        }
    }
    // Prop / texture transparency: bring the recorded models' own materials to the
    // requested alpha before the engine builds its render lists, so props are queued
    // as translucent. Values are written only when they change.
    if (stage == 5) {
        const auto effects = VisualRuntime::GetEffects();
        if (effects.texture_transparency > 0.f || effects.prop_transparency > 0.f) {
            __try { ModelTransparency::Sync(effects.texture_transparency, effects.prop_transparency); }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                static bool logged = false;
                if (!logged) { logged = true; LOG_WARN("transparency: material pass raised an exception"); }
            }
        }
    }
    if(stage==6)VisualRuntime::RestoreGlow();
    if (stage == 6) {
        VisualRuntime::CapturePlayers();
        ModelPreview::Paint();
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
        if (!Interfaces::Engine || !Interfaces::EntityList || !Interfaces::Globals) { ctx.reset(); }
        // Refresh the entity context once the client has applied network updates
        // (and at render start, when the local weapon may have changed).
        if (Interfaces::Engine && Interfaces::EntityList && Interfaces::Globals && (stage == 0 || stage == 4 || stage == 5)) {
            ctx.in_game = Interfaces::Engine->IsInGame() && Interfaces::Engine->IsConnected();
            if (!ctx.in_game) { ctx.reset(); ctx.frame_stage = stage; ModelTransparency::Purge(); WorldFog::Restore(); ViewModelChanger::Restore(); }
            else {
            const int local_index = Interfaces::Engine->GetLocalPlayer();
            ctx.local = static_cast<CCSPlayer*>(Interfaces::EntityList->GetClientEntity(local_index));
            ctx.weapon = ctx.local ? ctx.local->GetActiveWeapon() : nullptr;
            ctx.weapon_data = ctx.weapon ? ctx.weapon->GetWeaponData() : nullptr;
            ctx.tickbase = ctx.local ? ctx.local->m_nTickBase() : 0;
            ctx.curtime = Interfaces::Globals->curtime;
            ctx.frametime = Interfaces::Globals->frametime;
            if (ctx.local) ctx.eye_pos = ctx.local->GetEyePosition();
            }
            // TODO: features/visuals (glow/chams state), features/rage (lagcomp) entry points go here
            // Misc -> General/Other: radar, clan tag, chat and ping spike act on the entity
            // list and the network path, so they run after the context refresh above, still
            // inside this SEH frame. Movement and the buy bot ride the CreateMove hook.
            Misc::FrameStage(stage);
            Removals::FrameStage(stage);
            // Visuals -> Misc -> LOCAL: the third-person chase camera is set once the local
            // entity is fresh (render start) and cleared when the feature is off.
            if (stage == 5) ThirdPerson::FrameStage();
            if (stage == 5) FovChanger::FrameStage();
            // Visuals -> Misc -> LOCAL: "Viewmodel changer" asserts the vm_offset_* /
            // v_viewmodel_fov cvars (the viewmodel's position/rotation/fov).
            if (stage == 5) ViewModelChanger::FrameStage();
            // Live state for the scope/spread overlay (menu/overlays/screen_overlay.hpp):
            // whether the local player is scoped, the fov the view is drawn with and the
            // weapon's current inaccuracy (base * live penalty).
            if (stage == 5) {
                VisualRuntime::ScreenState ss;
                ss.scoped = ctx.local->m_bIsScoped();
                ss.fov = static_cast<float>(ctx.local->m_iFOV());
                int width = 0, height = 0;
                if (Interfaces::Engine) Interfaces::Engine->GetScreenSize(width, height);
                ss.width = width;
                ss.height = height;
                ss.inaccuracy = FovChanger::Detail::Inaccuracy(
                    ctx.weapon_data ? ctx.weapon_data->inaccuracy_stand : 0.f,
                    ctx.weapon ? ctx.weapon->m_fAccuracyPenalty() : 0.f);
                VisualRuntime::PublishScreen(ss);
            }
            // Visuals -> Misc: the per-frame screen effects - Out of FOV arrows, bullet
            // tracers, impacts, hitmarkers and the two crosshairs. One snapshot per
            // frame; empty when none of them fire, which the overlay draws as nothing.
            if (stage == 5) {
                VisualRuntime::FxFrame fx;
                OutOfFov::Contribute(fx);
                Tracers::Contribute(fx);
                Impacts::Contribute(fx);
                Hitmarker::Contribute(fx);
                ForceCrosshair::Contribute(fx);
                PenetrationCrosshair::Contribute(fx);
                VisualRuntime::PublishFx(fx);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}
}
