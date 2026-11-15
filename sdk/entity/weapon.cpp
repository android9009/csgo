#include "weapon.hpp"
#include "../interfaces/interfaces.hpp"

CCSWeaponData* CBaseWeapon::GetWeaponData() const {
    if (!Interfaces::WeaponSystem) return nullptr;
    return Interfaces::WeaponSystem->GetWeaponData(
        static_cast<std::uint32_t>(m_iItemDefinitionIndex()));
}
const char* CBaseWeapon::GetWeaponName() const {
    auto* data = GetWeaponData();
    return data ? data->weapon_name : nullptr;
}
