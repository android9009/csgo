#include "hooks.hpp"
#include "../../features/visuals/misc/world/world_modulation.hpp"
#include "../../features/visuals/misc/local/fov.hpp"
#include "../../context.hpp"
#include "../../sdk/entity/player.hpp"
#include "../../sdk/entity/weapon.hpp"
#include "../../sdk/visual_runtime_state.hpp"
#include "../../utils/detourhook.hpp"
#include "../../utils/log.hpp"
#include "../../utils/memory.hpp"
#include "../../utils/patterns/patterns.hpp"

namespace HooksInternal {
namespace {
// Visuals -> World: the material colour path (Nightmode / Sky tints).
Detour_Hook g_color_modulation_hook;
void* g_color_modulation_original{};
bool g_color_modulation_failed{};

// Visuals -> Misc -> LOCAL "Fov changer": the CCSPlayer::CalcView detour.
Detour_Hook g_calc_view_hook;
void* g_calc_view_original{};

// Visuals -> Misc -> LOCAL "Remove scope": the scope's post-screen shader pass.
Detour_Hook g_postscreen_hook;
void* g_postscreen_original{};

// The fov parameter of CBasePlayer::CalcView arrives in XMM0. Captured as the very first
// thing the detour does (the MSVC x86 prologue does not touch XMM0), because the snapshot
// and the netvar accessors make calls that clobber it before the original is invoked.
__forceinline float CaptureXmm0() {
    float value;
    __asm {
        movss xmm1, xmm0
        movss value, xmm1
    }
    return value;
}

// IEngineTrace... no: IMaterial::GetColorModulation (materialsystem.dll, `main`'s
// `get_color_modulation`). Verified in the supplied materialsystem.dll: `55 8B EC 83 EC ? 56 8B
// F1 8A 46` matches once at RVA 0xF90, the body writes the three floats through the three stack
// pointers and the epilogue is `ret 0xC`, so the thunk declares all three.
void __fastcall GetColorModulation(void* material, void* /*edx*/, float* r, float* g, float* b) {
    using Fn = void(__thiscall*)(void*, float*, float*, float*);
    const auto original = reinterpret_cast<Fn>(g_color_modulation_original);
    if (!original) return;
    original(material, r, g, b);
    if (g_color_modulation_failed) return;
    __try {
        WorldModulation::Tint(material, r, g, b);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_color_modulation_failed = true;
        LOG_ERROR("visuals world: exception in the GetColorModulation detour 0x%08lX; the world "
                  "tint stays off", GetExceptionCode());
    }
}

// The catalogue publishes module-relative addresses for these two, so the base has to be added.
std::uintptr_t ModuleFunction(const char* module, const char* name) {
    const auto rva = Patterns::Get(name);
    if (!rva) return 0;
    const auto base = reinterpret_cast<std::uintptr_t>(MEM::GetModuleBase(module));
    return base ? base + rva : 0;
}

// CCSPlayer::CalcView(CCalcViewContext*, int zoomLevel, float fov). The engine reads the
// player's m_iFOV/m_iFOVStart/m_iDefaultFOV inside this call to build the frame view, so
// the override has to land in those fields BEFORE the original runs (the `main` branch's
// calc_view detour does exactly this). Only the live local player is touched; everyone
// else goes straight to the original.
void __fastcall CalcView(void* ecx, void* /*edx*/, void* context, int zoom_level) {
    const float incoming_fov = CaptureXmm0();
    __try {
        if (ctx.local && ecx == ctx.local) {
            const auto snapshot = FovChanger::Snapshot();
            if (snapshot.enabled) {
                auto* player = static_cast<CCSPlayer*>(ecx);
                const int fov = FovChanger::Detail::ClampFov(snapshot.fov);
                player->m_iDefaultFOV() = fov;
                player->m_iFOVStart() = fov;
                player->m_iFOV() = fov;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // A bad local pointer drops this frame's override; the game's own view stands.
    }
    using Fn = void(__thiscall*)(void*, void*, int, float);
    const auto original = reinterpret_cast<Fn>(g_calc_view_original);
    if (original) original(ecx, context, zoom_level, incoming_fov);
}

// CViewRender::UpdatePostScreenEffects(CCSPlayer* local): the frame pass that applies the
// scope's post-screen shader (lens + vignette) while the local player's m_bIsScoped is set.
// The pattern's body loads its one stack argument (the local player) and calls it, so the
// thunk declares it as a `__fastcall` stack arg and forwards it to the original untouched.
//
// Visuals -> Misc -> LOCAL "Remove scope": while a sniper rifle is held and the menu flag
// is on, the netvar is held to false for the duration of the original call (so the shader
// pass skips the scope look) and restored to the saved value right after - the `main`
// branch's remove_post_screen_effects trick. The VGUI zoom overlay (the HudZoom panel) is
// dropped separately by the PaintTraverse hook. When the object being processed is not the
// local player the hand is skipped and the call forwarded untouched, so a mismatched
// pointer fails closed instead of corrupting state.
void __fastcall UpdatePostscreenEffects(void* ecx, void* /*edx*/, void* local_player) {
    bool restore = false;
    __try {
        if (ctx.local && local_player == ctx.local && ctx.local->IsAlive() &&
            VisualRuntime::scope_removal.load(std::memory_order_relaxed) &&
            ctx.local->GetActiveWeapon() && ctx.local->GetActiveWeapon()->IsSniper())
        {
            restore = ctx.local->m_bIsScoped();
            ctx.local->m_bIsScoped() = false;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        restore = false;
    }
    using Fn = void(__thiscall*)(void*, void*);
    const auto original = reinterpret_cast<Fn>(g_postscreen_original);
    if (original) original(ecx, local_player);
    if (restore) {
        __try {
            if (ctx.local) ctx.local->m_bIsScoped() = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
}
} // namespace

void InstallFovDetour() {
    const auto calc_view = ModuleFunction("client.dll", "calc_view");
    if (!calc_view) {
        LOG_WARN("visuals local: calc_view did not resolve; the fov override stays on the "
                 "framestage path only");
    } else if (g_calc_view_hook.attach(reinterpret_cast<void*>(calc_view),
                                       reinterpret_cast<void*>(&CalcView),
                                       &g_calc_view_original)) {
        LOG_INFO("visuals local: calc_view detour attached at 0x%08lX",
                 static_cast<unsigned long>(calc_view));
    } else {
        LOG_WARN("visuals local: the calc_view detour could not be created");
    }
}

void RemoveFovDetour() {
    g_calc_view_hook.detach();
}

void InstallRemoveScopeDetour() {
    // update_postscreen_effects is relative=false in the catalogue, so Get() already
    // returns the absolute address (no base add, unlike calc_view above).
    const auto postscreen = Patterns::Get("update_postscreen_effects");
    if (!postscreen) {
        LOG_WARN("visuals local: update_postscreen_effects did not resolve; the scope mask stays on");
    } else if (g_postscreen_hook.attach(reinterpret_cast<void*>(postscreen),
                                       reinterpret_cast<void*>(&UpdatePostscreenEffects),
                                       &g_postscreen_original)) {
        LOG_INFO("visuals local: update_postscreen_effects detour attached at 0x%08lX",
                 static_cast<unsigned long>(postscreen));
    } else {
        LOG_WARN("visuals local: the update_postscreen_effects detour could not be created");
    }
}

void RemoveRemoveScopeDetour() {
    g_postscreen_hook.detach();
}

void InstallWorldDetours() {
    const auto color_modulation = ModuleFunction("materialsystem.dll", "get_color_modulation");
    if (!color_modulation) {
        LOG_WARN("visuals world: get_color_modulation did not resolve; Nightmode and Sky stay "
                 "inactive");
    } else if (g_color_modulation_hook.attach(reinterpret_cast<void*>(color_modulation),
                                              reinterpret_cast<void*>(&GetColorModulation),
                                              &g_color_modulation_original)) {
        LOG_INFO("visuals world: colour-modulation detour attached at 0x%08lX",
                 static_cast<unsigned long>(color_modulation));
    } else {
        LOG_WARN("visuals world: the colour-modulation detour could not be created");
    }
}

void RemoveWorldDetours() {
    g_color_modulation_hook.detach();
}
} // namespace HooksInternal
