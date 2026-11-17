#pragma once
#include "../netvars.hpp"
#include "../clientclass.hpp"
#include "../studiohdr.hpp"
#include "../../utils/math/vector.hpp"
#include "../../utils/math/matrix.hpp"
#include "../../utils/offsets/offsets.hpp"
#include <cstdint>

class CBaseEntity {
public:
    NETVAR_FALLBACK(int, m_iTeamNum, "DT_BaseEntity", "m_iTeamNum", offsets::netvars::m_iTeamNum)
    NETVAR_FALLBACK(Vector, m_vecOrigin, "DT_BaseEntity", "m_vecOrigin", offsets::netvars::m_vecOrigin)
    NETVAR_FALLBACK(int, m_MoveType, "DT_BaseEntity", "m_MoveType", offsets::netvars::m_MoveType)
    NETVAR_FALLBACK(int, m_iHealth, "DT_BasePlayer", "m_iHealth", offsets::netvars::m_iHealth)
    NETVAR_FALLBACK(int, m_fFlags, "DT_BasePlayer", "m_fFlags", offsets::netvars::m_fFlags)
    NETVAR_FALLBACK(int, m_lifeState, "DT_BasePlayer", "m_lifeState", offsets::netvars::m_lifeState)
    NETVAR_FALLBACK(Vector, m_vecVelocity, "DT_BasePlayer", "m_vecVelocity[0]", offsets::netvars::m_vecVelocity)
    NETVAR_FALLBACK(Vector, m_vecViewOffset, "DT_BasePlayer", "m_vecViewOffset[0]", offsets::netvars::m_vecViewOffset)
    NETVAR_FALLBACK(int, m_nTickBase, "DT_BasePlayer", "m_nTickBase", offsets::netvars::m_nTickBase)
    NETVAR_FALLBACK(float, m_flSimulationTime, "DT_BaseEntity", "m_flSimulationTime", offsets::netvars::m_flSimulationTime)
    NETVAR(float, m_flDuckAmount, "DT_CSPlayer", "m_flDuckAmount")
    NETVAR_FALLBACK(int, m_nModelIndex, "DT_BaseEntity", "m_nModelIndex", offsets::netvars::m_nModelIndex)

    bool IsDormant() const;
    bool IsAlive() const { return m_lifeState() == 0 && m_iHealth() > 0; }
    bool IsPlayer() const;
    Vector GetEyePosition() const { return m_vecOrigin() + m_vecViewOffset(); }
    ClientClass* GetClientClass();
    int GetIndex() const;

    // Bones / model / hitboxes (studiohdr via IVModelInfo, bone cache via m_dwBoneMatrix).
    matrix3x4_t* GetBoneMatrix() const;
    bool GetBonePosition(int bone_index, Vector& out) const;
    void* GetModel() const;
    const char* GetModelName() const;
    studiohdr_t* GetStudioHdr() const;
    int GetHitboxSetCount() const;
    int GetHitboxCount(int set) const;
    mstudiobbox_t* GetHitbox(int set, int index) const;
    bool GetHitboxPosition(int set, int index, Vector& out) const;
};
