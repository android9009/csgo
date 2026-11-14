#pragma once
#include "entity.hpp"
#include "weapon.hpp"
#include "../../offsets/offsets.hpp"
class CCSPlayer : public CBaseEntity {
public:
    NETVAR_FALLBACK(float, m_flFlashDuration, "DT_CSPlayer", "m_flFlashDuration", offsets::netvars::m_flFlashDuration)
    NETVAR_FALLBACK(float, m_flFlashMaxAlpha, "DT_CSPlayer", "m_flFlashMaxAlpha", offsets::netvars::m_flFlashMaxAlpha)
    NETVAR_FALLBACK(float, m_flLowerBodyYawTarget, "DT_CSPlayer", "m_flLowerBodyYawTarget", offsets::netvars::m_flLowerBodyYawTarget)
    NETVAR_FALLBACK(Vector, m_aimPunchAngle, "DT_BasePlayer", "m_aimPunchAngle", offsets::netvars::m_aimPunchAngle)
    NETVAR_FALLBACK(Vector, m_viewPunchAngle, "DT_BasePlayer", "m_viewPunchAngle", offsets::netvars::m_viewPunchAngle)
    NETVAR_FALLBACK(int, m_iShotsFired, "DT_CSPlayer", "m_iShotsFired", offsets::netvars::m_iShotsFired)
    NETVAR_FALLBACK(bool, m_bIsScoped, "DT_CSPlayer", "m_bIsScoped", offsets::netvars::m_bIsScoped)
    NETVAR_FALLBACK(bool, m_bIsDefusing, "DT_CSPlayer", "m_bIsDefusing", offsets::netvars::m_bIsDefusing)
    NETVAR_FALLBACK(bool, m_bHasHelmet, "DT_CSPlayer", "m_bHasHelmet", offsets::netvars::m_bHasHelmet)
    NETVAR_FALLBACK(bool, m_bHasDefuser, "DT_CSPlayer", "m_bHasDefuser", offsets::netvars::m_bHasDefuser)
    NETVAR_FALLBACK(int, m_ArmorValue, "DT_CSPlayer", "m_ArmorValue", offsets::netvars::m_ArmorValue)
    CBaseWeapon* GetActiveWeapon() const;
};
