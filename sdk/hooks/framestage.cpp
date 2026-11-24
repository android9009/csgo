#include "hooks.hpp"
#include "../../menu/preview/model_preview.hpp"
#include "../visual_runtime_state.hpp"
#include "../../features/skins/skin_changer.hpp"
#include "../../features/skins/local_inventory.hpp"
#include "../../features/visuals/misc/other/removals.hpp"
#include "../../features/visuals/misc/other/model_transparency.hpp"
#include "../../features/visuals/misc/world/world_fog.hpp"
#include "../../features/visuals/misc/world/world_modulation.hpp"
#include "../../features/visuals/misc/world/world_postfx.hpp"
#include "../../features/visuals/misc/local/thirdperson.hpp"
#include "../../features/visuals/misc/local/fov.hpp"
#include "../../features/visuals/misc/local/viewmodel.hpp"
#include "../../features/visuals/misc/local/force_crosshair.hpp"
#include "../../features/visuals/misc/local/penetration_crosshair.hpp"
#include "../../features/visuals/misc/local/grenade_warning.hpp"
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
    auto original = Hooks::GetFrameStageNotifyOriginal();
    // Visuals -> World: the fog override and the tonemap/postfx writes must land in the
    // cvars and the env_tonemap_controller entity BEFORE the original framestage runs.
    // CClientMode::FrameStageNotification(FRAME_RENDER_START) samples the fog cvars and
    // reads the tonemap entity while it builds the frame, so a write after original is a
    // frame late and the map's own state wins. The `main` branch (airflow) does exactly
    // this: POST_PROCESSING->remove() is called above original(stage). With no game
    // running the cvars are handed straight back.
    if (stage == 5) {
        const bool in_game = Interfaces::Engine &&
            Interfaces::Engine->IsInGame() && Interfaces::Engine->IsConnected();
        const auto effects = VisualRuntime::GetEffects();
        if (in_game) {
            __try { WorldFog::Apply(effects.world_fog, effects.world_fog_colour); }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                WorldFog::Restore();
            }
            __try { WorldPostfx::Frame(); }
            __except (EXCEPTION_EXECUTE_HANDLER) {
            }
            // The viewmodel's own cvars are read while RenderView lays out the viewmodel,
            // i.e. inside the original that follows: asserting them before the original is
            // what makes the override visible on this frame instead of one frame late.
            __try { ViewModelChanger::FrameStage(); }
            __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        } else {
            // Invalidate (not just Restore): a map transition runs through the not-in-game
            // state, and the new level writes its own fog family on load. Dropping the
            // capture makes the next in-game frame re-capture the new map's start/end/
            // density instead of overwriting them with the previous map's.
            __try { WorldFog::Invalidate(); }
            __except (EXCEPTION_EXECUTE_HANDLER) {
            }
            __try { WorldPostfx::ResetState(); }
            __except (EXCEPTION_EXECUTE_HANDLER) {
            }
            __try { ViewModelChanger::Restore(); }
            __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
    }
    if (original) original(ecx, stage);
    // The D3D device may not have existed at hook-setup time; re-attach the EndScene
    // hook (idempotent no-op once attached) so the menu can render.
    Hooks::EnsureDeviceHooks();
    if (stage == 0) {if(VisualRuntime::glow_closing.load())VisualRuntime::UpdateGlow();LocalInventory::Frame();}
    if (stage == 5) VisualRuntime::UpdateGlow();
    // Visuals -> World: "Fullbright" rides the engine's own mat_fullbright switch, kept in step
    // here (the tint half runs inside the GetColorModulation detour). Fullbright and
    // Sunlight already work in game post-original, so they stay where they are.
    if (stage == 5) {
        __try { WorldModulation::Frame(); }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            static bool logged = false;
            if (!logged) { logged = true; LOG_WARN("visuals world: fullbright cvar write raised an exception"); }
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
            if (!ctx.in_game) { ctx.reset(); ctx.frame_stage = stage; ModelTransparency::Purge(); WorldFog::Invalidate(); WorldPostfx::ResetState(); ViewModelChanger::Restore(); }
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
            // "Viewmodel changer" asserts its cvars ABOVE the original (the viewmodel reads
            // them during this frame's RenderView); the not-in-game reset line below still
            // calls ViewModelChanger::Restore().
            // Live state for the scope/spread overlay (menu/overlays/screen_overlay.hpp):
            // whether the local player is scoped, the fov the view is drawn with and the
            // weapon's current inaccuracy (base * live penalty).
            if (stage == 5 && ctx.local) {
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
            // tracers, impacts, hitmarkers, the two crosshairs and the grenade
            // prediction / proximity warning (traces + damage snapshots, so it runs
            // last before the snapshot is published). One snapshot per frame; empty
            // when none of them fire, which the overlay draws as nothing.
            if (stage == 5) {
                VisualRuntime::FxFrame fx;
                OutOfFov::Contribute(fx);
                Tracers::Contribute(fx);
                Impacts::Contribute(fx);
                Hitmarker::Contribute(fx);
                ForceCrosshair::Contribute(fx);
                PenetrationCrosshair::Contribute(fx);
                GrenadeWarning::Contribute(fx);
                VisualRuntime::PublishFx(fx);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}
}
