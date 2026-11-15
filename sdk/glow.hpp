#pragma once
// Glow object manager access. Interfaces::GlowManager points at the
// CGlowObjectManager object (dereferenced dwGlowObjectManager); the glow
// object list is the CUtlVector at offset 0 of that object, matching the
// legacy sdk/game/interfaces.hpp c_glow_object_manager layout.
#include "structs.hpp"
#include "datatypes/datatypes.hpp"
#include <cstdint>

class IGlowObjectManager {
public:
    static constexpr std::uintptr_t kGlowObjectsOffset = 0;

    CGlowObjectDefinition* GetGlowObject(int index) {
        auto* objects = GetGlowObjects();
        if (!objects || index < 0 || index >= objects->m_Size) return nullptr;
        return &objects->m_pMemory[index];
    }
    int GetGlowObjectCount() const {
        auto* objects = GetGlowObjects();
        return objects ? objects->m_Size : 0;
    }
    CGlowObjectDefinition* FindGlowObject(void* entity) {
        auto* objects = GetGlowObjects();
        if (!objects || !entity) return nullptr;
        for (int i = 0; i < objects->m_Size; ++i)
            if (objects->m_pMemory[i].m_pEntity == entity) return &objects->m_pMemory[i];
        return nullptr;
    }
private:
    CUtlVector<CGlowObjectDefinition>* GetGlowObjects() const {
        return reinterpret_cast<CUtlVector<CGlowObjectDefinition>*>(
            reinterpret_cast<std::uintptr_t>(this) + kGlowObjectsOffset);
    }
};
