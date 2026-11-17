#pragma once
#include "entity.hpp"
#include "../../utils/offsets/offsets.hpp"
#include <cstdint>

// CCSWeaponInfo layout as returned by IWeaponSystem::GetWeaponData.
// Field offsets follow the legacy sdk/game/structs.hpp weapon_info_t
// (non-legacy branch); unnamed regions are padding.
class CCSWeaponData {
public:
    char pad_0[20]{};
    std::uint32_t max_clip1 = 0;
    char pad_1[12]{};
    std::uint32_t max_clip2 = 0;
    char pad_2[84]{};
    char* world_model = nullptr;
    char pad_3[8]{};
    char* hud_name = nullptr;
    char* weapon_name = nullptr;
    char pad_4[56]{};
    std::uint32_t weapon_type = 0;
    char pad_5[36]{};
    std::uint32_t damage = 0;
    float crosshair_delta_distance = 0.f;
    float armor_ratio = 0.f;
    std::uint32_t bullets = 0;
    float penetration = 0.f;
    float flinch_velocity_modifier_large = 0.f;
    float flinch_velocity_modifier_small = 0.f;
    float range = 0.f;
    float range_modifier = 0.f;
    float throw_velocity = 0.f;
    char pad_6[20]{};
    std::uint32_t crosshair_delta_dist = 0;
    std::uint32_t crosshair_min_dist = 0;
    float max_speed = 0.f;
    float max_speed_alt = 0.f;
    char pad_7[12]{};
    float inaccuracy_crouch = 0.f;
    float inaccuracy_crouch_alt = 0.f;
    float inaccuracy_stand = 0.f;
    float inaccuracy_stand_alt = 0.f;
    float inaccuracy_jump = 0.f;
    float inaccuracy_jump_alt = 0.f;
    float inaccuracy_land = 0.f;
    float inaccuracy_land_alt = 0.f;
    char pad_8[96]{};
    bool unk = false;
    char pad_9[4]{};
    bool hide_viewmodel_in_zoom = false;
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
    CCSWeaponData* GetWeaponData() const;
    const char* GetWeaponName() const;
};
