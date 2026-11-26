#pragma once
// Compat shim: the ported menu tree resolves local/other players through the
// old project's entity layer. Airflow accesses the same client entities by
// raw pointer + netvar offsets, so the shim exposes that shape under the old
// class names. Offsets are the Source 1 (csgo) ones the old code was written
// against; they match airflow's own usage of m_iTeamNum.
#include <cstdint>
class CBaseEntity {
public:
    CBaseEntity* next() const { return const_cast<CBaseEntity*>(*reinterpret_cast<const CBaseEntity* const*>(this)); }
    // model_t* m_Model (CS:GO x86 C_BaseEntity offset 0x29C8); model_t holds
    // the .mdl path in name[260] at byte 4.
    const char* GetModelName() const {
        const auto* model = *reinterpret_cast<const void* const*>(reinterpret_cast<const uint8_t*>(this) + 0x29C8);
        return model ? reinterpret_cast<const char*>(static_cast<const uint8_t*>(model) + 4) : nullptr;
    }
    int index() const { return *reinterpret_cast<const int*>(reinterpret_cast<const uint8_t*>(this) + 0x8); }
    int m_iHealth() const { return *reinterpret_cast<const int*>(reinterpret_cast<const uint8_t*>(this) + 0x100); }
    int m_iArmor() const { return *reinterpret_cast<const int*>(reinterpret_cast<const uint8_t*>(this) + 0x120); }
    int m_iTeamNum() const { return *reinterpret_cast<const int*>(reinterpret_cast<const uint8_t*>(this) + 0xA0); }
    float m_flHealthDecayTime() const { return *reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(this) + 0x108); }
    bool m_bDormant() const { return *reinterpret_cast<const uint8_t*>(reinterpret_cast<const uint8_t*>(this) + 0x10) != 0; }
    int m_Flags() const { return *reinterpret_cast<const int*>(reinterpret_cast<const uint8_t*>(this) + 0x104); }
    const float* m_vecOrigin() const { return reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(this) + 0x138); }
    int m_iAccount() const { return *reinterpret_cast<const int*>(reinterpret_cast<const uint8_t*>(this) + 0x2F78); }
};
using CBasePlayer = CBaseEntity;
using CCSPlayer = CBaseEntity;
