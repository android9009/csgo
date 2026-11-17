#pragma once
#include "entity/entity.hpp"
#include <windows.h>
#include <cstring>
namespace VisualRead {
struct Module {std::uintptr_t begin{},end{};bool Contains(const void* p,std::size_t n=1)const{const auto a=reinterpret_cast<std::uintptr_t>(p);return begin && a>=begin && a<end && n<=end-a;}};
inline Module ClientModule(){auto* base=reinterpret_cast<unsigned char*>(GetModuleHandleA("client.dll"));if(!base)return {};auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);auto* nt=reinterpret_cast<IMAGE_NT_HEADERS*>(base+dos->e_lfanew);return {reinterpret_cast<std::uintptr_t>(base),reinterpret_cast<std::uintptr_t>(base)+nt->OptionalHeader.SizeOfImage};}
inline bool Readable(const void* p,std::size_t size){MEMORY_BASIC_INFORMATION info{};if(!p || !VirtualQuery(p,&info,sizeof info) || info.State!=MEM_COMMIT || (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;const auto a=reinterpret_cast<std::uintptr_t>(p),b=reinterpret_cast<std::uintptr_t>(info.BaseAddress);return a>=b && a-b<info.RegionSize && size<=info.RegionSize-(a-b);}
// Entities live on the heap, NOT inside client.dll. Validate their networkable
// vtable, getter code, ClientClass and name against the client image instead.
// Caller supplies the SEH boundary; no locks, lazy statics or allocations here.
inline ClientClass* Class(CBaseEntity* entity,const Module& module){
    if(!Readable(entity,12))return nullptr;
    auto** table=*reinterpret_cast<unsigned char***>(reinterpret_cast<unsigned char*>(entity)+8);
    if(!module.Contains(table,3*sizeof(void*)))return nullptr;
    auto* code=table[2];
    for(int i=0;i<2;++i){if(!module.Contains(code,6))return nullptr;if(code[0]!=0xe9)break;code+=5+*reinterpret_cast<std::int32_t*>(code+1);}
    if(!module.Contains(code,6) || code[0]!=0xb8 || code[5]!=0xc3)return nullptr;
    auto* cls=*reinterpret_cast<ClientClass**>(code+1);
    if(!module.Contains(cls,sizeof(ClientClass)) || !module.Contains(cls->m_pNetworkName,128) || !std::memchr(cls->m_pNetworkName,0,128))return nullptr;
    return cls;
}
}
