#include "hooks.hpp"
#include "../../menu/preview/model_preview.hpp"
#include "../visual_runtime_state.hpp"
#include "../../features/skins/skin_changer.hpp"
#include "../../features/skins/local_inventory.hpp"
#include "../../features/misc/misc.hpp"
#include "../../features/visuals/misc/effects.hpp"
#include "../../features/visuals/misc/misc.hpp"
#include "../../features/visuals/removals/removals.hpp"
#include "../../features/visuals/model_transparency.hpp"
#include "../../features/visuals/world_fog.hpp"
#include "../../features/visuals/world_modulation.hpp"
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
            if (!ctx.in_game) { ctx.reset(); ctx.frame_stage = stage; ModelTransparency::Purge(); WorldFog::Restore();
                // Tracers/impacts/hitmarkers belong to a match: leaving it drops them.
                VisualsMisc::Effects::Reset(); }
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
            // Radar, clan tag and flash removal act on the entity list, so they run
            // after the context refresh above, still inside this SEH frame.
            Misc::FrameStage(stage);
            Removals::FrameStage(stage);
            // Visuals -> Misc: the fov changer writes the player's own fov fields and the
            // third person camera writes CInput, both at FRAME_RENDER_START - before the
            // engine builds the view for this frame.
            VisualsMisc::FrameStage(stage);
            if (stage == 5) VisualsMisc::ApplyThirdPerson();
            // Visuals -> Misc -> Other: the event-driven effects project their live records
            // once per frame, with the matrix the renderer is about to use.
            if (stage == 5) VisualsMisc::Effects::Frame();
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}
}
