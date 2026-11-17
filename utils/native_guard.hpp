#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
namespace NativeCall {
// Some Source entrypoints inspect their native caller before doing any work.
// A normal C++ call from a VMT replacement is not a compatible calling path.
// Detect the checked wrapper structurally; never patch or skip its validation.
inline bool CallerChecked(const void* address){
    if(!address)return true;
    __try{
        const auto* code=static_cast<const unsigned char*>(address);
        for(int i=0;i<40;++i){
            if(code[i]!=0xe8)continue;
            const auto* target=code+i+5+*reinterpret_cast<const std::int32_t*>(code+i+1);
            if(target[0]==0x55 && target[1]==0x8b && target[2]==0xec && target[3]==0x51 && target[4]==0x89 && target[5]==0x4d && target[6]==0xfc && target[7]==0x6a && target[9]==0x6a && target[11]==0x8b && target[12]==0x45 && target[13]==0xfc && target[14]==0x50 && target[15]==0xe8)return true;
        }
        return false;
    }__except(EXCEPTION_EXECUTE_HANDLER){return true;}
}
}
