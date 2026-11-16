#pragma once
#include "structs.hpp"
#include <cstdint>
// Engine CUtlVector ABI: memory, allocation count, grow size, size, elements.
// Do not reinterpret it as the old convenience CUtlVector in datatypes.hpp.
class IGlowObjectManager {
    struct VectorABI {CGlowObjectDefinition* memory;int allocated,grow,size;CGlowObjectDefinition* elements;};
public:
    CGlowObjectDefinition* Objects() const {
        const auto* v=reinterpret_cast<const VectorABI*>(this);
        if(v->size<0 || v->size>4096 || v->allocated<v->size || v->allocated>65536)return nullptr;
        return v->memory;
    }
    int GetGlowObjectCount() const {return Objects() ? reinterpret_cast<const VectorABI*>(this)->size : 0;}
    CGlowObjectDefinition* GetGlowObject(int index) {return index>=0 && index<GetGlowObjectCount() ? Objects()+index : nullptr;}
    CGlowObjectDefinition* FindGlowObject(void* entity) {
        const int count=GetGlowObjectCount();auto* items=Objects();
        for(int i=0;i<count;++i)if(items[i].m_nNextFreeSlot==-2 && items[i].m_pEntity==entity)return items+i;
        return nullptr;
    }
};
