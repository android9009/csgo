#include "../../features/visuals/postprocess_shader.hpp"
#include <d3dcompiler.h>
#include <cstring>
#include <cstdio>
int main(){
    ID3DBlob *code=nullptr,*errors=nullptr;
    const HRESULT result=D3DCompile(VisualPost::source,std::strlen(VisualPost::source),nullptr,nullptr,nullptr,"main","ps_3_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
    if(errors){std::fprintf(stderr,"%s",static_cast<const char*>(errors->GetBufferPointer()));errors->Release();}
    if(code)code->Release();
    return FAILED(result)?1:0;
}
