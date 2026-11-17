#pragma once
#include "misc_state.hpp"
#include "../../sdk/interfaces/interfaces.hpp"
#include <d3d9.h>
#include <d3dcompiler.h>
#include <cstring>
#include "../../utils/log.hpp"
namespace VisualPost {
inline IDirect3DTexture9* scene{};
inline IDirect3DSurface9* surface{};
inline IDirect3DPixelShader9* shader{};
inline IDirect3DDevice9* owner{};
inline UINT width{},height{};
inline int last_frame{-1};
inline bool failed{};
template<class T> inline void Release(T*& object){if(object){object->Release();object=nullptr;}}
inline void Reset(){Release(surface);Release(scene);Release(shader);owner=nullptr;width=height=0;last_frame=-1;failed=false;}
inline bool Prepare(IDirect3DDevice9* dev,const D3DSURFACE_DESC& desc){
    if(owner!=dev || width!=desc.Width || height!=desc.Height)Reset();
    if(failed)return false;
    if(scene && shader)return true;
    owner=dev;width=desc.Width;height=desc.Height;
    static const char source[]=R"hlsl(
 sampler2D scene : register(s0);
 float4 pixel : register(c0);
 float4 correction : register(c1);
 float4 balance : register(c2);
 float4 switches : register(c3);
 float4 vignette : register(c4);
 float4 local : register(c5);
 float4 limits : register(c6);
 float4 bloom : register(c7);
 float4 blur : register(c8);
 float4 weights[5] : register(c9);
 float3 fetch(float2 uv){return pow(max(tex2D(scene,uv).rgb,0),2.2);}
 float3 bright(float2 uv){float3 v=fetch(uv);float l=max(v.r,max(v.g,v.b));return v*smoothstep(bloom.z,max(bloom.z+.001,bloom.z+bloom.w),l);}

 float4 main(float2 uv:TEXCOORD0):COLOR0 {
   float3 c=fetch(uv);
   if(blur.y>.5){
     float3 light=0;
     [unroll] for(int i=0;i<5;++i){
       float2 step=pixel.xy*(1.0+blur.x*24.0)*(i+1);
       light+=(bright(uv+float2(step.x,0))+bright(uv-float2(step.x,0))+bright(uv+float2(0,step.y))+bright(uv-float2(0,step.y)))*.25*weights[i].rgb*weights[i].a;
     }
     c+=light*bloom.x*bloom.y;
   }
   if(switches.y>0.5){
     float2 delta=pixel.xy*(1.0+limits.z*7.0);
     float3 blurred=(fetch(uv+float2(delta.x,0))+fetch(uv-float2(delta.x,0))+fetch(uv+float2(0,delta.y))+fetch(uv-float2(0,delta.y)))*0.25;
     float radial=length((uv-.5)*1.4142);
     float mask=1.0-smoothstep(limits.x,max(limits.x+.001,limits.y),radial);
     c+=((c-blurred)*local.y+(c-.5)*local.x*.2)*mask;
   }
   if(switches.x>0.5){
     float l=dot(c,float3(.2126,.7152,.0722));
     c+=balance.x*float3(.12,0,-.12)+balance.y*float3(-.06,.12,-.06);
     c+=correction.y*(1.0-l)*(1.0-l)*.3+correction.z*l*l*.3;
     c=(c-.5)*(1.0+correction.x)+.5;
     float mx=max(c.r,max(c.g,c.b)),mn=min(c.r,min(c.g,c.b));
     c=lerp(dot(c,float3(.2126,.7152,.0722)).xxx,c,1.0+correction.w+balance.z*(1.0-saturate(mx-mn)));
     if(balance.w>0.5){float3 x=max(0,c);c=saturate((x*(2.51*x+.03))/(x*(2.43*x+.59)+.14));}
   }
   if(switches.z>0.5)c=dot(c,float3(.2126,.7152,.0722)).xxx;
   if(switches.w>0.5){float r=length((uv-.5)*1.4142);c=lerp(c,vignette.rgb,smoothstep(.3,1.0,r)*vignette.a);}
   return float4(pow(saturate(c),1.0/2.2),1);
 }
)hlsl";
    ID3DBlob *code=nullptr,*error=nullptr;
    const HRESULT compiled=D3DCompile(source,std::strlen(source),nullptr,nullptr,nullptr,"main","ps_3_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
    if(FAILED(compiled) && error)LOG_WARN("visual postprocess shader: %s",static_cast<const char*>(error->GetBufferPointer()));
    Release(error);
    if(FAILED(compiled) || !code){Release(code);failed=true;return false;}
    const HRESULT created=dev->CreatePixelShader(static_cast<const DWORD*>(code->GetBufferPointer()),&shader);Release(code);
    if(FAILED(created) || FAILED(dev->CreateTexture(width,height,1,D3DUSAGE_RENDERTARGET,desc.Format,D3DPOOL_DEFAULT,&scene,nullptr)) || FAILED(scene->GetSurfaceLevel(0,&surface))){Release(surface);Release(scene);Release(shader);failed=true;return false;}
    return true;
}
inline void Render(IDirect3DDevice9* dev){
    const auto s=VisualMisc::Get();
    if(!Interfaces::Engine || !Interfaces::Engine->IsInGame()){if(owner)Reset();return;}
    if((s.removals&8u) || !(s.correction || s.vignette || s.local_contrast || s.grayscale || s.bloom)){if(owner)Reset();return;}
    if(Interfaces::Globals){const int frame=Interfaces::Globals->framecount;if(last_frame==frame)return;last_frame=frame;}
    IDirect3DSurface9* target=nullptr;if(FAILED(dev->GetRenderTarget(0,&target)) || !target)return;
    D3DSURFACE_DESC desc{};const HRESULT described=target->GetDesc(&desc);
    if(FAILED(described) || !Prepare(dev,desc) || FAILED(dev->StretchRect(target,nullptr,surface,nullptr,D3DTEXF_NONE))){Release(target);return;}
    Release(target);
    IDirect3DStateBlock9* state=nullptr;
    if(FAILED(dev->CreateStateBlock(D3DSBT_ALL,&state)) || !state)return;
    if(FAILED(state->Capture())){Release(state);return;}
    float constants[14][4]={
        {1.f/static_cast<float>(width),1.f/static_cast<float>(height),0,0},
        {s.contrast*.01f,s.shadows*.01f,s.highlights*.01f,s.saturation*.01f},
        {s.temperature*.01f,s.tint*.01f,s.vibrance*.01f,(s.tone_curve&2u)?1.f:0.f},
        {s.correction?1.f:0.f,s.local_contrast?1.f:0.f,s.grayscale?1.f:0.f,s.vignette?1.f:0.f},
        {static_cast<float>((s.vignette_color>>16)&255)/255.f,static_cast<float>((s.vignette_color>>8)&255)/255.f,static_cast<float>(s.vignette_color&255)/255.f,static_cast<float>((s.vignette_color>>24)&255)/255.f},
        {s.local_strength*.01f,s.edge_strength*.01f,0,0},
        {s.vignette_start*.01f,s.vignette_end*.01f,s.vignette_blur*.01f,0},
        {s.bloom_value*.02f,s.bloom_screen*.02f,s.bloom_threshold*.01f,s.bloom_width*.01f},
        {s.bloom_blur*.01f,s.bloom?1.f:0.f,0,0}
    };
    for(unsigned i=0;i<5;++i){const auto c=s.bloom_colors[i];constants[9+i][0]=static_cast<float>((c>>16)&255)/255.f;constants[9+i][1]=static_cast<float>((c>>8)&255)/255.f;constants[9+i][2]=static_cast<float>(c&255)/255.f;constants[9+i][3]=s.bloom_weights[i]*.01f*static_cast<float>((c>>24)&255)/255.f;}
    struct Vertex{float x,y,z,rhw,u,v;};
    const float w=static_cast<float>(width)-.5f,h=static_cast<float>(height)-.5f;
    const Vertex vertices[]={{-.5f,-.5f,0,1,0,0},{w,-.5f,0,1,1,0},{-.5f,h,0,1,0,1},{w,h,0,1,1,1}};
    D3DVIEWPORT9 viewport{0,0,width,height,0.f,1.f};dev->SetViewport(&viewport);
    dev->SetVertexShader(nullptr);dev->SetPixelShader(shader);dev->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);
    dev->SetRenderState(D3DRS_ZENABLE,FALSE);dev->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);dev->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);dev->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);dev->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);dev->SetRenderState(D3DRS_STENCILENABLE,FALSE);dev->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);dev->SetRenderState(D3DRS_COLORWRITEENABLE,15);dev->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
    dev->SetTexture(0,scene);dev->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);dev->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);dev->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);dev->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);dev->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
    dev->SetPixelShaderConstantF(0,&constants[0][0],14);dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Vertex));
    state->Apply();Release(state);
}
}
