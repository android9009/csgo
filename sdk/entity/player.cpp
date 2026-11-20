#include "player.hpp"
#include "../interfaces/interfaces.hpp"
CBaseWeapon* CCSPlayer::GetActiveWeapon() const {
    // m_hActiveWeapon is inherited from DT_BaseCombatCharacter.
    static const auto offset = []() { const auto found = PropManager::Get().GetOffset("DT_BaseCombatCharacter", "m_hActiveWeapon"); return found ? found : offsets::netvars::m_hActiveWeapon; }();
    const auto handle = *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<std::uintptr_t>(this) + offset);
    return Interfaces::EntityList
        ? static_cast<CBaseWeapon*>(Interfaces::EntityList->GetClientEntityFromHandle(handle)) : nullptr;
}

const char* CCSPlayer::LastPlaceName() const {
    // A char array, so it is read through the plain prop offset rather than a NETVAR
    // reference: nothing is written and the string is only copied by the caller.
    static const std::ptrdiff_t offset = []() {
        const auto found = PropManager::Get().GetOffset("DT_CSPlayer", "m_szLastPlaceName");
        return found ? found : static_cast<std::ptrdiff_t>(0);
    }();
    if (offset == 0) return "";
    return reinterpret_cast<const char*>(reinterpret_cast<std::uintptr_t>(this) + offset);
}
