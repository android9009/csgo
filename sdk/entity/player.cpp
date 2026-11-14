#include "player.hpp"
#include "../interfaces/interfaces.hpp"
CBaseWeapon* CCSPlayer::GetActiveWeapon() const {
    // m_hActiveWeapon is inherited from DT_BaseCombatCharacter.
    static const auto offset = []() { const auto found = PropManager::Get().GetOffset("DT_BaseCombatCharacter", "m_hActiveWeapon"); return found ? found : offsets::netvars::m_hActiveWeapon; }();
    const auto handle = *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<std::uintptr_t>(this) + offset);
    return Interfaces::EntityList
        ? static_cast<CBaseWeapon*>(Interfaces::EntityList->GetClientEntityFromHandle(handle)) : nullptr;
}
