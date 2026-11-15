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
matrix3x4_t* CBaseEntity::GetBoneMatrix() const {
    // m_dwBoneMatrix has no pattern; the hazedumper fallback is the source of truth here.
    return *reinterpret_cast<matrix3x4_t* const*>(
        reinterpret_cast<std::uintptr_t>(this) + offsets::netvars::m_dwBoneMatrix);
}
bool CBaseEntity::GetBonePosition(int bone_index, Vector& out) const {
    auto* bones = GetBoneMatrix();
    if (!bones || bone_index < 0) return false;
    out = bones[bone_index].GetOrigin();
    return out.IsValid();
}
void* CBaseEntity::GetModel() const {
    if (!Interfaces::ModelInfo) return nullptr;
    return Interfaces::ModelInfo->GetModel(m_nModelIndex());
}
const char* CBaseEntity::GetModelName() const {
    if (!Interfaces::ModelInfo) return nullptr;
    if (auto* model = GetModel()) return Interfaces::ModelInfo->GetModelName(model);
    return nullptr;
}
studiohdr_t* CBaseEntity::GetStudioHdr() const {
    if (Interfaces::ModelInfo) {
        if (auto* model = GetModel()) {
            if (auto* hdr = Interfaces::ModelInfo->GetStudioModel(model)) return hdr;
        }
    }
    auto offset = offsets::Get("m_pStudioHdr");
    if (!offset) offset = offsets::fallback::m_pStudioHdr;
    if (offset && offset < 0x100000)
        return *reinterpret_cast<studiohdr_t* const*>(
            reinterpret_cast<std::uintptr_t>(this) + offset);
    return nullptr;
}
int CBaseEntity::GetHitboxSetCount() const {
    auto* hdr = GetStudioHdr();
    return hdr ? hdr->hitbox_sets : 0;
}
int CBaseEntity::GetHitboxCount(int set) const {
    auto* hdr = GetStudioHdr();
    if (!hdr || set < 0 || set >= hdr->hitbox_sets) return 0;
    auto* hitbox_set = hdr->hitbox_set(set);
    return hitbox_set ? hitbox_set->num_hitboxes : 0;
}
mstudiobbox_t* CBaseEntity::GetHitbox(int set, int index) const {
    auto* hdr = GetStudioHdr();
    if (!hdr || set < 0 || set >= hdr->hitbox_sets) return nullptr;
    auto* hitbox_set = hdr->hitbox_set(set);
    if (!hitbox_set || index < 0 || index >= hitbox_set->num_hitboxes) return nullptr;
    return hitbox_set->hitbox(index);
}
bool CBaseEntity::GetHitboxPosition(int set, int index, Vector& out) const {
    auto* box = GetHitbox(set, index);
    auto* bones = GetBoneMatrix();
    auto* hdr = GetStudioHdr();
    if (!box || !bones || !hdr || box->bone < 0 || box->bone >= hdr->bones) return false;
    const Vector center = (box->min + box->max) * 0.5f;
    const auto& bone = bones[box->bone];
    out = Vector(
        bone[0][0] * center.x + bone[0][1] * center.y + bone[0][2] * center.z + bone[0][3],
        bone[1][0] * center.x + bone[1][1] * center.y + bone[1][2] * center.z + bone[1][3],
        bone[2][0] * center.x + bone[2][1] * center.y + bone[2][2] * center.z + bone[2][3]);
    return out.IsValid();
}
