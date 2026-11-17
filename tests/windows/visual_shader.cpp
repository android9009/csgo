#include "../../features/visuals/postprocess_shader.hpp"
#include <d3dcompiler.h>
#include <cstring>
#include <cstdio>
#include "../../utils/native_guard.hpp"
int main(){
    unsigned char entry[128];std::memset(entry,0x90,sizeof entry);
    if(NativeCall::CallerChecked(entry) || !NativeCall::CallerChecked(nullptr))return 2;
    entry[0]=0xe8;const std::int32_t displacement=59;std::memcpy(entry+1,&displacement,sizeof displacement);
    const unsigned char checker[]={0x55,0x8b,0xec,0x51,0x89,0x4d,0xfc,0x6a,0x0a,0x6a,0x07,0x8b,0x45,0xfc,0x50,0xe8};
    std::memcpy(entry+64,checker,sizeof checker);
    if(!NativeCall::CallerChecked(entry))return 3;

    ID3DBlob *code=nullptr,*errors=nullptr;
    const HRESULT result=D3DCompile(VisualPost::source,std::strlen(VisualPost::source),nullptr,nullptr,nullptr,"main","ps_3_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
    if(errors){std::fprintf(stderr,"%s",static_cast<const char*>(errors->GetBufferPointer()));errors->Release();}
    if(code)code->Release();
    return FAILED(result)?1:0;
}
