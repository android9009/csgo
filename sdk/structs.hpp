#pragma once
#include "datatypes/datatypes.hpp"
#include <cstdint>
#include <cstddef>

struct CUserCmd {
    void* vtable = nullptr;
    int command_number = 0;
    int tick_count = 0;
    QAngle viewangles{};
    Vector aimdirection{};
    float forwardmove = 0.f;
    float sidemove = 0.f;
    float upmove = 0.f;
    int buttons = 0;
    unsigned char impulse = 0;
    int weaponselect = 0;
    int weaponsubtype = 0;
    int random_seed = 0;
    short mousedx = 0;
    short mousedy = 0;
    bool hasbeenpredicted = false;
    QAngle headangles{};
    Vector headoffset{};
};
// CViewSetup, pinned to the layout `main` uses (c_view_setup): 176 bytes of screen,
// ortho and HUD state, then the fields ClientMode::OverrideView hands to the renderer.
// Only the tail is read or written here.
struct ViewSetup {
    unsigned char reserved[176]{};
    float fov = 0.f;            // +0xB0
    float viewmodel_fov = 0.f;  // +0xB4
    Vector origin{};            // +0xB8
    QAngle angles{};            // +0xC4
};
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
