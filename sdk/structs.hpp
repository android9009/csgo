#pragma once
#include "datatypes/datatypes.hpp"
#include <cstdint>

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
struct CVerifiedUserCmd { CUserCmd cmd{}; unsigned long crc = 0; };
struct CGlowObjectDefinition {
    void* m_pEntity = nullptr;
    float r = 0.f, g = 0.f, b = 0.f, a = 0.f;
    unsigned char pad[16]{};
    bool m_bRenderWhenOccluded = false;
    bool m_bRenderWhenUnoccluded = false;
    bool m_bFullBloom = false;
    unsigned char pad2[5]{};
    int m_nNextFreeSlot = -2;
};
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
