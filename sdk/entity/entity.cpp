#include "entity.hpp"
#include "../interfaces/interfaces.hpp"
#include "../../offsets/offsets.hpp"
#include "../../utils/memory.hpp"
#include <cstring>

bool CBaseEntity::IsDormant() const {
    const auto offset = offsets::Get("m_bDormant");
    if (offset > 0x100000) return false;
    return *reinterpret_cast<const bool*>(reinterpret_cast<std::uintptr_t>(this) + offset);
}
bool CBaseEntity::IsPlayer() const {
    auto* cls = const_cast<CBaseEntity*>(this)->GetClientClass();
    if (!cls || !cls->m_pNetworkName) return false;
    return std::strstr(cls->m_pNetworkName, "Player") != nullptr;
}
ClientClass* CBaseEntity::GetClientClass() {
    using Fn = ClientClass* (__thiscall*)(void*);
    auto fn = MEM::GetVFunc<Fn>(this, 2);
    return fn ? fn(this) : nullptr;
}
int CBaseEntity::GetIndex() const {
    if (!Interfaces::EntityList) return -1;
    const int highest = Interfaces::EntityList->GetHighestEntityIndex();
    for (int i = 0; i <= highest; ++i)
        if (Interfaces::EntityList->GetClientEntity(i) == this) return i;
    return -1;
}
