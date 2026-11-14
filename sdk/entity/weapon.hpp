#pragma once
#include "entity.hpp"
#include "../../offsets/offsets.hpp"
#include <cstdint>
class CCSWeaponData {
public:
    int max_clip = 0;
    int max_reserved_ammo = 0;
    const char* hud_name = nullptr;
    const char* weapon_name = nullptr;
    int weapon_type = 0;
    int price = 0;
    float cycle_time = 0.f;
    float damage = 0.f;
    float armor_ratio = 0.f;
    int bullets = 0;
    float penetration = 0.f;
    float range = 0.f;
    float range_modifier = 0.f;
    float throw_velocity = 0.f;
};
class CBaseWeapon : public CBaseEntity {
public:
    NETVAR_FALLBACK(int, m_iClip1, "DT_BaseCombatWeapon", "m_iClip1", offsets::netvars::m_iClip1)
    NETVAR_FALLBACK(float, m_flNextPrimaryAttack, "DT_BaseCombatWeapon", "m_flNextPrimaryAttack", offsets::netvars::m_flNextPrimaryAttack)
    NETVAR(float, m_flNextSecondaryAttack, "DT_BaseCombatWeapon", "m_flNextSecondaryAttack")
    NETVAR_FALLBACK(float, m_fAccuracyPenalty, "DT_WeaponCSBase", "m_fAccuracyPenalty", offsets::netvars::m_fAccuracyPenalty)
    NETVAR_FALLBACK(bool, m_bInReload, "DT_BaseCombatWeapon", "m_bInReload", offsets::netvars::m_bInReload)
    NETVAR_FALLBACK(short, m_iItemDefinitionIndex, "DT_BaseAttributableItem", "m_iItemDefinitionIndex", offsets::netvars::m_iItemDefinitionIndex)
    NETVAR_FALLBACK(std::uint32_t, m_hOwnerEntity, "DT_BaseEntity", "m_hOwnerEntity", offsets::netvars::m_hOwnerEntity)
    bool IsGun() const { return m_iClip1() >= 0; }
};
