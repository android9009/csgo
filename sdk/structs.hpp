#pragma once
#include "datatypes/datatypes.hpp"
#include "datatypes/c_user_cmd.hpp"
#include <cstdint>
#include <cstddef>

// CUserCmd lives in datatypes/c_user_cmd.hpp: it is a class with a virtual destructor
// (the game's command object has one), and this header used to declare a second,
// flat `struct CUserCmd` of the same name - every translation unit that saw both
// failed with C2011. Only one definition exists now.
// CViewSetup lives in sdk/model_preview.hpp: it is the same layout (`fov` at +0xB0, `origin`
// at +0xB8, `angles` at +0xC4 - confirmed from ClientModeShared::OverrideView in the supplied
// client.dll, which writes exactly those offsets) and it is the one the preview renderer
// passes to IMatRenderContext::Push3DView. This header used to carry a second, partial copy
// under the same name; a translation unit that saw both would fail with C2011, and third
// person no longer needs it at all now that the engine owns its own camera.
struct CVerifiedUserCmd { CUserCmd cmd{}; unsigned long crc = 0; };
// Pinned 2023 (non-LEGACY) Source 1 layout: free-list link precedes entity.
struct CGlowObjectDefinition {
    int m_nNextFreeSlot = -2;
    void* m_pEntity = nullptr;
    float r = 0.f, g = 0.f, b = 0.f, a = 0.f;
    bool custom_alpha = false;
    unsigned char pad[3]{};
    float alpha_velocity{}, alpha_max{}, glow_pulse{};
    bool m_bRenderWhenOccluded = false;
    bool m_bRenderWhenUnoccluded = false;
    bool m_bFullBloom = false;
    unsigned char pad2{};
    int full_bloom_stencil{}, glow_style{}, screen_slot{};
};
#if defined(_M_IX86)
static_assert(sizeof(CGlowObjectDefinition)==0x38,"CS:GO 2023 glow entry ABI");
static_assert(offsetof(CGlowObjectDefinition,m_pEntity)==4,"CS:GO glow entity offset");
static_assert(offsetof(CGlowObjectDefinition,m_bRenderWhenOccluded)==0x28,"CS:GO glow flags offset");
#endif

struct CAnimationLayer {
    char pad[20]{};
    int m_nOrder = 0;
    int m_nSequence = 0;
    float m_flPrevCycle = 0.f;
    float m_flWeight = 0.f;
    float m_flWeightDeltaRate = 0.f;
    float m_flPlaybackRate = 0.f;
    float m_flCycle = 0.f;
    void* m_pOwner = nullptr;
    char pad2[4]{};
};
