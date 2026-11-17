#pragma once
namespace VisualPost {
inline constexpr char source[]=R"hlsl(
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
}
