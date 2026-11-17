#include "misc_runtime.hpp"
#include "postprocess.hpp"
#include "../../context.hpp"
#include "../../sdk/interfaces/interfaces.hpp"
#include "../../sdk/model_preview.hpp"
#include "../../sdk/material.hpp"
#include "../../utils/memory.hpp"
#include "../../utils/offsets/offsets.hpp"
#include "../../utils/detourhook.hpp"
#include "../../utils/log.hpp"
#include <vector>
#include <cstring>
#include <atomic>
#include <string>
#include <cstdio>
#include <unordered_set>
namespace VisualMisc {
namespace {
template<class T> T& Field(void* p,std::ptrdiff_t n){return *reinterpret_cast<T*>(static_cast<unsigned char*>(p)+n);}
bool Valid(std::ptrdiff_t n){return n>0 && n<0x10000;}
using View=SourceModelPreview::ViewSetup;
using ViewFn=void(__thiscall*)(void*,View*);
using FovFn=float(__thiscall*)(void*);
using AspectFn=float(__thiscall*)(void*,int,int);
using DrawVmFn=bool(__thiscall*)(void*);
using VmFn=void(__thiscall*)(void*,CCSPlayer*,const Vector&,const QAngle&);
VMT_Hook view_hook,fov_hook,aspect_hook,draw_vm_hook;
Detour_Hook vm_hook,bob_hook;
using BobFn=void(__thiscall*)(void*,Vector&);BobFn bob_original{};
ViewFn view_original{};FovFn fov_original{};AspectFn aspect_original{};DrawVmFn draw_vm_original{};VmFn vm_original{};
std::ptrdiff_t camera_flag{},camera_offset{},observer_offset{};
bool camera_owned{},old_camera{};Vector old_offset{};float camera_distance{},aspect_value{};int aspect_frame{-1};
CCSPlayer* observer_player{};int observer_original{};
std::atomic<float> model_alpha{1.f};std::atomic<unsigned> removal_bits{};
void RestoreCamera(){
    if(camera_owned && Interfaces::Input){Field<bool>(Interfaces::Input,camera_flag)=old_camera;Field<Vector>(Interfaces::Input,camera_offset)=old_offset;}
    camera_owned=false;camera_distance=0.f;
    if(observer_player && Interfaces::Engine && Interfaces::Engine->IsInGame() && ctx.local==observer_player && Valid(observer_offset) && Field<int>(observer_player,observer_offset)==5)Field<int>(observer_player,observer_offset)=observer_original;
    observer_player=nullptr;
}
void Camera(View& view,const Settings& s){
    if(!ctx.local || !ctx.in_game){RestoreCamera();return;}
    const int item=ctx.weapon ? ctx.weapon->m_iItemDefinitionIndex() : 0;const bool grenade=item>=43 && item<=48;
    const bool active=s.third && !(grenade && s.grenade_off);
    if(!ctx.local->IsAlive()){
        if(camera_owned)RestoreCamera();
        if(active && s.dead && Valid(observer_offset)){
            if(observer_player!=ctx.local){observer_player=ctx.local;observer_original=Field<int>(ctx.local,observer_offset);}
            Field<int>(ctx.local,observer_offset)=5;
        }else if(observer_player)RestoreCamera();
        return;
    }
    if(observer_player)RestoreCamera();
    if(!active || !Valid(camera_flag) || !Valid(camera_offset) || !Interfaces::Input){RestoreCamera();return;}
    if(!camera_owned){old_camera=Field<bool>(Interfaces::Input,camera_flag);old_offset=Field<Vector>(Interfaces::Input,camera_offset);camera_owned=true;}
    float target=std::clamp(s.distance,40.f,180.f);const Vector eye=ctx.local->GetEyePosition(),direction=Forward(view.angles);
    if(Interfaces::EngineTrace){Ray_t ray;ray.Init(eye,eye-direction*target,{-4,-4,-4},{4,4,4});CTraceFilterSimple filter(ctx.local);CGameTrace trace;
        Interfaces::EngineTrace->TraceRay(ray,0x200400Bu,&filter,&trace);
        target=trace.startsolid || trace.allsolid ? 0.f : (std::max)(0.f,target*std::clamp(trace.fraction,0.f,1.f)-2.f);
    }
    const float dt=Interfaces::Globals ? Interfaces::Globals->absoluteframetime : .016f;
    camera_distance=s.instant ? target : (std::min)(target,Smooth(camera_distance,target,dt)); // collision retraction is immediate
    Field<bool>(Interfaces::Input,camera_flag)=true;Field<Vector>(Interfaces::Input,camera_offset)={view.angles.x,view.angles.y,camera_distance};
    view.origin=eye-direction*camera_distance;
}
void ApplyView(View& view){
    const auto s=Get();if(!ctx.in_game || !ctx.local){RestoreCamera();return;}
    Camera(view,s);
    if(s.fov_enabled && ctx.local->IsAlive()){
        const bool scoped=ctx.local->m_bIsScoped();const float base=std::clamp(s.fov,60.f,140.f);
        const float zoomed=scoped && s.second_zoom ? (std::min)(view.fov,15.f) : view.fov;
        view.fov=scoped ? ZoomFov(zoomed,base,s.zoom) : base;
    }
    if(s.vm_enabled)view.viewmodel_fov=std::clamp(s.vm_fov,60.f,140.f);
}
void __fastcall OverrideView(void* self,void*,View* view){
    view_original(self,view);if(!view)return;
    __try{ApplyView(*view);}__except(EXCEPTION_EXECUTE_HANDLER){LOG_WARN("visual misc: invalid camera state");}
}
float __fastcall ViewmodelFov(void* self,void*){const auto s=Get();return s.vm_enabled && ctx.in_game ? std::clamp(s.vm_fov,60.f,140.f) : fov_original(self);}
bool __fastcall DrawViewmodel(void* self,void*){const auto s=Get();return s.fov_enabled && s.viewmodel_scope && ctx.in_game && ctx.local && ctx.local->IsAlive() && !s.third ? true : draw_vm_original(self);}
float __fastcall Aspect(void* self,void*,int width,int height){
    const float original=aspect_original(self,width,height);const auto s=Get();
    const float target=s.fov_enabled && s.aspect>0.f && ctx.in_game ? std::clamp(s.aspect*.01f,.5f,2.f) : original;
    if(!s.fov_enabled || s.aspect<=0.f || !ctx.in_game){aspect_value=original;return original;}
    if(aspect_value<=0.f)aspect_value=original;
    if(!Interfaces::Globals || aspect_frame!=Interfaces::Globals->framecount){
        aspect_frame=Interfaces::Globals ? Interfaces::Globals->framecount : -1;
        aspect_value=Smooth(aspect_value,target,Interfaces::Globals ? Interfaces::Globals->absoluteframetime : .016f);
    }
    return aspect_value;
}
void __fastcall Viewmodel(void* self,void*,CCSPlayer* owner,const Vector& eye,const QAngle& angles){
    const auto s=Get();Vector position=eye;QAngle rotation=angles;
    if(owner && owner==ctx.local && ctx.in_game && s.vm_enabled){position+=ViewmodelOffset(angles,s.vm_position);rotation.x+=s.vm_angles.x;rotation.y+=s.vm_angles.y;rotation.z+=s.vm_angles.z;}
    vm_original(self,owner,position,rotation);
}
void __fastcall LandingBob(void* self,void*,Vector& position){
    const Vector original=position;bob_original(self,position);
    const auto s=Get();if(self==ctx.local && ctx.in_game && s.vm_enabled && s.landing_bob)position=original;
}
struct OwnedCvar {
    const char* name;void* variable{};bool resolved{},owned{};float original{},applied{};
    void Set(bool enabled,float value){
        if(!enabled && !owned)return;
        if(!resolved){if(!Interfaces::ConVar)return;resolved=true;variable=MEM::GetVFunc<void*(__thiscall*)(void*,const char*)>(Interfaces::ConVar,14)(Interfaces::ConVar,name);}
        if(!variable)return;
        const auto get=MEM::GetVFunc<float(__thiscall*)(void*)>(variable,12);const auto set=MEM::GetVFunc<void(__thiscall*)(void*,float)>(variable,15);
        if(!enabled){if(std::abs(get(variable)-applied)<.001f)set(variable,original);owned=false;return;}
        if(!std::isfinite(value))return;
        if(!owned){original=get(variable);if(!std::isfinite(original))return;}
        if(!owned || value!=applied){set(variable,value);applied=value;owned=true;}
    }
};
OwnedCvar cvars[]={ {"weapon_debug_spread_show"},{"mat_fullbright"},{"mat_postprocess_enable"},{"cl_csm_enabled"},{"r_drawparticles"},{"r_drawtracers_firstperson"},{"r_waterdrawreflection"},{"r_waterdrawrefraction"},{"violence_hblood"},{"violence_ablood"},{"r_drawdecals"},{"cl_ragdoll_physics_enable"},{"r_drawspecificstaticprop"},{"fog_override"},{"fog_enable"},{"fog_start"},{"fog_end"},{"mat_force_tonemap_scale"},{"mat_bloom_scalefactor_scalar"} };
struct FogColor {
    void* variable{};bool resolved{},owned{};std::string original,applied;
    const char* Value(){
        auto* parent=Field<unsigned char*>(variable,0x1c);if(!parent || *reinterpret_cast<void***>(parent)!=*reinterpret_cast<void***>(variable))return nullptr;
        const char* name=MEM::GetVFunc<const char*(__thiscall*)(void*)>(parent,5)(parent);if(!name || std::strcmp(name,"fog_color"))return nullptr;
        const int length=Field<int>(parent,0x28);const char* value=Field<const char*>(parent,0x24);
        return length>0 && length<=4096 && value && std::memchr(value,0,static_cast<std::size_t>(length)) ? value : nullptr;
    }
    void Set(bool enabled,std::uint32_t color){
        if(!enabled && !owned)return;
        if(!resolved){if(!Interfaces::ConVar)return;resolved=true;variable=MEM::GetVFunc<void*(__thiscall*)(void*,const char*)>(Interfaces::ConVar,14)(Interfaces::ConVar,"fog_color");}
        if(!variable)return;
        const auto set=MEM::GetVFunc<void(__thiscall*)(void*,const char*)>(variable,14);
        if(!enabled){const char* current=Value();if(current && applied==current)set(variable,original.c_str());owned=false;return;}
        char text[32];std::snprintf(text,sizeof text,"%u %u %u",(color>>16)&255,(color>>8)&255,color&255);
        if(owned && applied==text)return;
        if(!owned){const char* value=Value();if(!value)return;original=value;}
        applied=text;set(variable,applied.c_str());owned=true;
    }
} fog_color;
void Cvars(const Settings& s,bool active){
    const auto removed=[&](unsigned bit){return active && (s.removals&(1u<<bit));};
    cvars[0].Set(active && s.crosshair && ctx.local && ctx.local->IsAlive() && !ctx.local->m_bIsScoped(),3.f);
    cvars[1].Set(active && s.fullbright,1.f);cvars[2].Set(removed(3),0.f);cvars[3].Set(removed(9),0.f);
    cvars[4].Set(removed(2),0.f);cvars[5].Set(removed(2),0.f);
    cvars[6].Set(removed(4),0.f);cvars[7].Set(removed(4),0.f);
    cvars[8].Set(removed(5),0.f);cvars[9].Set(removed(5),0.f);cvars[10].Set(removed(6),0.f);cvars[11].Set(removed(7),0.f);
    cvars[12].Set(active && (s.prop_alpha>0.f || s.night),0.f);
    fog_color.Set(active && s.fog,s.fog_color);
    cvars[13].Set(active && s.fog,1.f);cvars[14].Set(active && s.fog,1.f);cvars[15].Set(active && s.fog,0.f);cvars[16].Set(active && s.fog,1200.f);
    cvars[17].Set(active && s.exposure,(std::max)(.01f,s.exposure_value*.02f));cvars[18].Set(false,0.f);
}
struct MaterialRecord {IMaterial* value{};std::array<float,3> rgb{};float alpha{};bool nodraw{};unsigned group{};};
std::vector<MaterialRecord> materials;
std::unordered_set<IMaterial*> known_materials;int known_material_count{-1};
unsigned short material_cursor{};bool scanning{};std::size_t apply_cursor{};
struct MaterialSettings {
    float texture{},prop{};unsigned removals{};bool night{},fullbright{},sky{};std::uint32_t world{},bright{},sky_color{};
    bool operator==(const MaterialSettings& v)const{return texture==v.texture && prop==v.prop && removals==v.removals && night==v.night && fullbright==v.fullbright && sky==v.sky && world==v.world && bright==v.bright && sky_color==v.sky_color;}
};
MaterialSettings material_settings;bool material_settings_valid{};
void ReleaseMaterials(){for(auto& item:materials){item.value->ColorModulate(item.rgb[0],item.rgb[1],item.rgb[2]);item.value->AlphaModulate(item.alpha);item.value->SetMaterialVarFlag(1<<2,item.nodraw);item.value->DecrementReferenceCount();}materials.clear();known_materials.clear();known_material_count=-1;scanning=false;apply_cursor=0;material_settings_valid=false;}
void Modulate(MaterialRecord& item,const MaterialSettings& s){
    std::uint32_t tint=0xffffffff;
    if((item.group&3u) && s.night)tint=s.world;
    if((item.group&3u) && s.fullbright)tint=s.bright;
    if((item.group&4u) && s.sky)tint=s.sky_color;
    const float red=static_cast<float>((tint>>16)&255)/255.f,green=static_cast<float>((tint>>8)&255)/255.f,blue=static_cast<float>(tint&255)/255.f;
    const float shade=(item.group&3u) && s.night && !s.fullbright ? ((item.group&1u) ? .2f : .5f) : 1.f;
    item.value->ColorModulate(item.rgb[0]*red*shade,item.rgb[1]*green*shade,item.rgb[2]*blue*shade);
    const float transparency=(item.group&1u) ? s.texture : (item.group&2u) ? s.prop : 0.f;
    item.value->AlphaModulate(item.alpha*(1.f-std::clamp(transparency*.01f,0.f,1.f)));
    const bool hide=((item.group&8u) && (s.removals&2u)) || ((item.group&16u) && (s.removals&(1u<<4))) || ((item.group&32u) && (s.removals&(1u<<5)));
    item.value->SetMaterialVarFlag(1<<2,item.nodraw || hide);
}
void Materials(const Settings& s){
    if(!Interfaces::MaterialSystem)return;
    MaterialSettings next{s.texture_alpha,s.prop_alpha,s.removals&(2u|16u|32u),s.night,s.fullbright,s.sky,s.night_color,s.fullbright_color,s.sky_color};
    const bool requested=s.texture_alpha>0.f || s.prop_alpha>0.f || s.night || s.fullbright || s.sky || (s.removals&(2u|16u|32u));
    if(materials.empty() && !scanning && !requested)return;
    if(!material_settings_valid || !(next==material_settings)){material_settings=next;material_settings_valid=true;apply_cursor=0;}
    auto* system=Interfaces::MaterialSystem;
    const auto first=MEM::GetVFunc<unsigned short(__thiscall*)(void*)>(system,86);
    const auto advance=MEM::GetVFunc<unsigned short(__thiscall*)(void*,unsigned short)>(system,87);
    const auto invalid=MEM::GetVFunc<unsigned short(__thiscall*)(void*)>(system,88)(system);
    const auto get=MEM::GetVFunc<IMaterial*(__thiscall*)(void*,unsigned short)>(system,89);
    const int count=MEM::GetVFunc<int(__thiscall*)(void*)>(system,90)(system);
    if(!scanning && (materials.empty() || count!=known_material_count)){known_material_count=count;material_cursor=first(system);scanning=true;}
    // Amortized map scan and dirty application; never a full per-frame walk.
    int budget=128;
    while(scanning && budget-- >0){
        if(material_cursor==invalid){scanning=false;break;}
        auto* value=get(system,material_cursor);material_cursor=advance(system,material_cursor);
        if(!value || known_materials.count(value) || value->IsErrorMaterial())continue;
        const char* group=value->GetTextureGroupName();const char* name=value->GetName();if(!group || !name)continue;
        unsigned kind=std::strstr(group,"World") ? 1u : std::strstr(group,"StaticProp") ? 2u : std::strstr(group,"SkyBox") ? 4u : 0u;
        if(std::strstr(name,"vistasmoke") || std::strstr(name,"smokegrenade"))kind|=8u;
        if(std::strstr(group,"Water") || std::strstr(name,"water/"))kind|=16u;
        if(std::strstr(name,"blood"))kind|=32u;
        if(!kind)continue;
        MaterialRecord record;record.value=value;record.group=kind;record.alpha=value->GetAlphaModulation();value->GetColorModulation(&record.rgb[0],&record.rgb[1],&record.rgb[2]);record.nodraw=value->GetMaterialVarFlag(1<<2);
        value->IncrementReferenceCount();materials.push_back(record);known_materials.insert(value);
    }
    budget=128;while(apply_cursor<materials.size() && budget-- >0)Modulate(materials[apply_cursor++],material_settings);
}
int* smoke_count{};int smoke_backup{};bool smoke_saved{};
void RestoreSmoke(){if(smoke_saved && smoke_count)*smoke_count=smoke_backup;smoke_saved=false;}
CCSPlayer* punch_player{};Vector aim_backup{},view_backup{};bool punch_saved{};
CCSPlayer* flash_player{};float flash_backup{};bool flash_saved{};
bool LivePlayer(CCSPlayer* player){return player && Interfaces::Engine && Interfaces::EntityList && Interfaces::Engine->IsInGame() && Interfaces::EntityList->GetClientEntity(Interfaces::Engine->GetLocalPlayer())==player;}
void RestorePunch(){if(punch_saved && LivePlayer(punch_player)){punch_player->m_aimPunchAngle()=aim_backup;punch_player->m_viewPunchAngle()=view_backup;}punch_saved=false;punch_player=nullptr;}
void RestoreFlash(){if(flash_saved && LivePlayer(flash_player))flash_player->m_flFlashMaxAlpha()=flash_backup;flash_saved=false;flash_player=nullptr;}
struct EnvironmentField {
    CBaseEntity* entity{};int index{};std::ptrdiff_t offset{};unsigned size{},kind{};
    std::array<unsigned char,4> original{},applied{};bool owned{};
    bool Live()const{return entity && Interfaces::EntityList && Interfaces::EntityList->GetClientEntity(index)==entity;}
    void Restore(){if(owned && Live() && !std::memcmp(&Field<unsigned char>(entity,offset),applied.data(),size))std::memcpy(&Field<unsigned char>(entity,offset),original.data(),size);owned=false;}
    template<class T> void Set(bool enabled,const T& value){
        static_assert(sizeof(T)<=4,"network environment scalar");
        if(!enabled){Restore();return;}if(!Live() || !Valid(offset))return;
        auto* target=&Field<unsigned char>(entity,offset);if(!owned){std::memcpy(original.data(),target,sizeof(T));owned=true;}
        size=sizeof(T);std::memcpy(applied.data(),&value,sizeof(T));std::memcpy(target,&value,sizeof(T));
    }
};
std::vector<EnvironmentField> environment;
int environment_tick{-1};
void RestoreEnvironment(){for(auto& field:environment)field.Restore();environment.clear();environment_tick=-1;}
void Environment(const Settings& s){
    if(!Interfaces::EntityList || !Interfaces::Globals)return;
    const bool wanted=s.sunlight || s.fog || s.exposure;
    if(!wanted){if(!environment.empty())RestoreEnvironment();return;}
    const int tick=Interfaces::Globals->tickcount;
    if(environment_tick<0 || tick<environment_tick || tick-environment_tick>=64){
        RestoreEnvironment();environment_tick=tick;
        const int highest=(std::min)(Interfaces::EntityList->GetHighestEntityIndex(),4095);
        for(int index=1;index<=highest;++index){
            auto* entity=static_cast<CBaseEntity*>(Interfaces::EntityList->GetClientEntity(index));if(!entity)continue;
            const auto* cls=entity->GetClientClass();if(!cls || !cls->m_pNetworkName)continue;
            const char* table=nullptr;const char* names[4]{};
            if(!std::strcmp(cls->m_pNetworkName,"CCascadeLight")){table="DT_CascadeLight";names[0]="m_LightColor";}
            else if(!std::strcmp(cls->m_pNetworkName,"CFogController")){table="DT_FogController";names[0]="m_fog.colorPrimary";}
            else if(!std::strcmp(cls->m_pNetworkName,"CEnvTonemapController")){table="DT_EnvTonemapController";names[0]="m_bUseCustomAutoExposureMin";names[1]="m_bUseCustomAutoExposureMax";names[2]="m_flCustomAutoExposureMin";names[3]="m_flCustomAutoExposureMax";}
            if(!table)continue;
            for(const auto* name:names){if(!name)continue;const auto offset=PropManager::Get().GetOffset(table,name);if(!Valid(offset))continue;EnvironmentField field;field.entity=entity;field.index=index;field.offset=offset;field.kind=!std::strcmp(cls->m_pNetworkName,"CCascadeLight") ? 0u : !std::strcmp(cls->m_pNetworkName,"CFogController") ? 1u : std::strstr(name,"m_bUse") ? 2u : 3u;environment.push_back(field);}
        }
    }
    for(auto& field:environment){
        if(!field.Live())continue;
        if(field.kind<2){
            const bool sun=field.kind==0;const auto color=sun ? s.sunlight_color : s.fog_color;
            const std::uint32_t packed=(color&0xff00ff00u)|((color&255u)<<16)|((color>>16)&255u);field.Set(sun ? s.sunlight : s.fog,packed);
        }else if(field.kind==2)field.Set(s.exposure,true);else field.Set(s.exposure,(std::max)(.01f,s.exposure_value*.02f));
    }
}
void CaptureArrows(const Settings& s){
    static ArrowFrame frame;static std::array<void*,64> identities{};
    if(!s.arrows || !ctx.local || !ctx.local->IsAlive() || !Interfaces::EntityList){frame={};identities={};PublishArrows(frame);return;}
    frame.eye=ctx.local->GetEyePosition();
    const float dt=Interfaces::Globals ? Interfaces::Globals->absoluteframetime : .016f;
    for(int i=1;i<=64;++i){
        auto* player=static_cast<CCSPlayer*>(Interfaces::EntityList->GetClientEntity(i));auto& point=frame.points[i-1];
        if(identities[i-1]!=player){point={};identities[i-1]=player;}
        const bool enemy=player && player!=ctx.local && player->IsAlive() && player->m_iTeamNum()!=ctx.local->m_iTeamNum();
        const bool active=enemy && !player->IsDormant();
        if(active)point.position=player->m_vecOrigin()+Vector{0,0,36};
        point.alpha=Smooth(point.alpha,active ? 1.f : 0.f,dt,active ? 12.f : 2.f);
        if(!enemy)point.alpha=0.f;
    }
    PublishArrows(frame);
}
void PredictGrenade(const Settings& s){
    static int previous_tick{-1};static Vector previous_eye{},previous_velocity{};static QAngle previous_angles{};static int previous_item{};static float previous_strength{-1.f};
    static const auto strength_offset=PropManager::Get().GetOffset("DT_BaseCSGrenade","m_flThrowStrength");
    static void* gravity_var{};
    if(!s.trajectory || !ctx.local || !ctx.local->IsAlive() || !ctx.weapon || !Interfaces::EngineTrace || !Interfaces::Globals){PublishTrajectory({});previous_tick=-1;return;}
    const int item=ctx.weapon->m_iItemDefinitionIndex();if(item<43 || item>48 || !Valid(strength_offset)){PublishTrajectory({});previous_tick=-1;return;}
    const int tick=Interfaces::Globals->tickcount;if(tick==previous_tick)return;
    QAngle angles;Interfaces::Engine->GetViewAngles(angles);Vector eye=ctx.local->GetEyePosition(),player_velocity=ctx.local->m_vecVelocity();
    const float strength=std::clamp(Field<float>(ctx.weapon,strength_offset),0.f,1.f);
    if(previous_tick>=0 && tick-previous_tick<8 && previous_item==item && previous_strength==strength && previous_eye.DistSqr(eye)<.01f && previous_velocity.DistSqr(player_velocity)<.01f && previous_angles.x==angles.x && previous_angles.y==angles.y)return;
    previous_tick=tick;previous_eye=eye;previous_velocity=player_velocity;previous_angles=angles;previous_item=item;previous_strength=strength;
    if(!gravity_var && Interfaces::ConVar)gravity_var=MEM::GetVFunc<void*(__thiscall*)(void*,const char*)>(Interfaces::ConVar,14)(Interfaces::ConVar,"sv_gravity");
    float gravity=gravity_var ? MEM::GetVFunc<float(__thiscall*)(void*)>(gravity_var,12)(gravity_var)*.4f : 320.f;if(!std::isfinite(gravity))gravity=320.f;
    if(angles.x>90.f)angles.x-=360.f;else if(angles.x< -90.f)angles.x+=360.f;
    angles.x-=(90.f-std::abs(angles.x))*10.f/90.f;
    const Vector forward=Forward(angles);eye.z+=strength*12.f-12.f;
    CTraceFilterSimple filter(ctx.local);Ray_t ray;CGameTrace trace;
    ray.Init(eye,eye+forward*22.f,{-2,-2,-2},{2,2,2});Interfaces::EngineTrace->TraceRay(ray,0x200400Bu,&filter,&trace);
    Vector position=trace.endpos-forward*6.f;
    const auto* data=ctx.weapon->GetWeaponData();const float speed=data && std::isfinite(data->throw_velocity) && data->throw_velocity>0.f ? data->throw_velocity : 750.f;
    Vector velocity=player_velocity*1.25f+forward*(std::clamp(speed*.9f,15.f,750.f)*(strength*.7f+.3f));
    Trajectory result;result.points[result.count++]=position;
    const float dt=std::clamp(Interfaces::Globals->interval_per_tick,.0078125f,.03125f);
    for(int step=0;step<512 && result.count<result.points.size();++step){
        const float time=static_cast<float>(step+1)*dt;Vector movement=velocity*dt;movement.z-=.5f*gravity*dt*dt;velocity.z-=gravity*dt;
        ray.Init(position,position+movement,{-2,-2,-2},{2,2,2});trace=CGameTrace{};Interfaces::EngineTrace->TraceRay(ray,0x200400Bu,&filter,&trace);
        if(trace.allsolid || trace.startsolid)break;position=trace.endpos;const bool collision=trace.fraction<1.f;
        if(collision){velocity=(velocity-trace.plane.normal*(2.f*velocity.Dot(trace.plane.normal)))*.45f;if(trace.plane.normal.z>.7f && velocity.LengthSqr()<400.f)velocity={0,0,0};}
        const bool fire=item==46 || item==48;
        const bool detonate=((item==43 || item==44) && time>=1.5f) || (fire && ((collision && trace.plane.normal.z>.7f) || time>=3.5f)) || ((item==45 || item==47) && time>=.2f && velocity.LengthSqr()<.01f);
        if(collision || step%2==0 || detonate){result.points[result.count]=position;result.bounces[result.count]=collision;++result.count;}
        if(detonate){result.detonated=true;break;}
    }
    if(result.detonated && (item==44 || item==46 || item==48))for(int i=1;i<=64;++i){
        auto* enemy=static_cast<CCSPlayer*>(Interfaces::EntityList->GetClientEntity(i));if(!enemy || enemy==ctx.local || !enemy->IsAlive() || enemy->IsDormant() || enemy->m_iTeamNum()==ctx.local->m_iTeamNum())continue;
        if(enemy->m_vecOrigin().DistSqr(position)>(item==44 ? 350.f*350.f : 150.f*150.f))continue;
        ray.Init(position+Vector{0,0,2},enemy->GetEyePosition());trace=CGameTrace{};Interfaces::EngineTrace->TraceRay(ray,0x200400Bu,&filter,&trace);
        if(trace.fraction>.97f || trace.m_pEnt==enemy){result.hit=true;break;}
    }
    PublishTrajectory(result);
}
bool connected{};
void FrameImpl(int stage){
    if(stage!=5)return;
    const auto s=Get();const bool active=Interfaces::Engine && Interfaces::Engine->IsInGame() && Interfaces::Engine->IsConnected();
    if(!active){RestorePunch();RestoreFlash();RestoreCamera();if(connected){RestoreEnvironment();ReleaseMaterials();}connected=false;ClearEvents();PublishArrows({});PublishTrajectory({});PublishLocal({});Cvars(s,false);model_alpha.store(1.f);removal_bits.store(0);return;}
    connected=true;Environment(s);CaptureArrows(s);PredictGrenade(s);if(Interfaces::Globals)TickEvents(Interfaces::Globals->curtime);Cvars(s,true);Materials(s);removal_bits.store(s.removals,std::memory_order_relaxed);
    LocalFrame local;local.alive=ctx.local && ctx.local->IsAlive();local.scoped=local.alive && ctx.local->m_bIsScoped();
    if(local.alive && (s.spread || (s.scope&~1u)) && ctx.weapon){
        const auto* data=ctx.weapon->GetWeaponData();
        if(data && data->weapon_type>=1 && data->weapon_type<=6){
            const float inaccuracy=MEM::GetVFunc<float(__thiscall*)(void*)>(ctx.weapon,483)(ctx.weapon);
            const float spread=MEM::GetVFunc<float(__thiscall*)(void*)>(ctx.weapon,453)(ctx.weapon);
            if(std::isfinite(inaccuracy) && std::isfinite(spread))local.spread=std::clamp(inaccuracy+spread,0.f,2.f);
        }
    }
    PublishLocal(local);
    float alpha=1.f;if(ctx.local && ctx.local->IsAlive() && s.third){if(ctx.local->m_bIsScoped())alpha=1.f-s.scope_alpha*.01f;const int item=ctx.weapon ? ctx.weapon->m_iItemDefinitionIndex() : 0;if(item>=43 && item<=48)alpha=(std::min)(alpha,1.f-s.nade_alpha*.01f);}
    model_alpha.store(std::clamp(alpha,0.f,1.f),std::memory_order_relaxed);
    if(ctx.local && (s.removals&1u)){if(!flash_saved || flash_player!=ctx.local){RestoreFlash();flash_player=ctx.local;flash_backup=ctx.local->m_flFlashMaxAlpha();flash_saved=true;}ctx.local->m_flFlashMaxAlpha()=0.f;}else RestoreFlash();
}
}
void BeforeFrame(int stage){
    __try{
        if(stage==0 || stage==1 || stage==5){RestorePunch();RestoreSmoke();}
        if(stage==5 && ctx.in_game && smoke_count && (Get().removals&2u) && *smoke_count>=0 && *smoke_count<=4096){smoke_backup=*smoke_count;smoke_saved=true;*smoke_count=0;}
        if(stage==5 && LivePlayer(ctx.local) && ctx.in_game && (Get().removals&(1u<<8))){punch_player=ctx.local;aim_backup=ctx.local->m_aimPunchAngle();view_backup=ctx.local->m_viewPunchAngle();punch_saved=true;ctx.local->m_aimPunchAngle()={};ctx.local->m_viewPunchAngle()={};}
    }__except(EXCEPTION_EXECUTE_HANDLER){}
}
void Frame(int stage){__try{if(stage==6){RestorePunch();RestoreSmoke();}FrameImpl(stage);}__except(EXCEPTION_EXECUTE_HANDLER){static bool logged{};if(!logged){logged=true;LOG_WARN("visual misc: invalid frame resource");}}}
bool HideScopePanel(const char* name){return name && !std::strcmp(name,"HudZoom") && GetLocal().scoped && (Get().scope&~1u)!=0;}
float LocalAlpha(int index){const float alpha=model_alpha.load(std::memory_order_relaxed);return alpha<1.f && ctx.local && index==ctx.local->GetIndex() ? alpha : 1.f;}
bool RemovingRagdolls(){return (removal_bits.load(std::memory_order_relaxed)&(1u<<7))!=0;}
bool HideRagdoll(void* entity){if(!entity || !RemovingRagdolls())return false;const auto* cls=static_cast<CBaseEntity*>(entity)->GetClientClass();return cls && cls->m_pNetworkName && !std::strcmp(cls->m_pNetworkName,"CCSRagdoll");}
static bool ReadMatrix(std::uintptr_t address,std::array<float,16>& matrix){
    __try{std::memcpy(matrix.data(),reinterpret_cast<const void*>(address),sizeof(matrix));return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool CurrentMatrix(std::array<float,16>& matrix){
    static const auto base=reinterpret_cast<std::uintptr_t>(MEM::GetModuleBase("client.dll"));
    static const auto offset=offsets::Get("dwViewMatrix");if(!base || !offset)return false;
    if(!ReadMatrix(base+offset,matrix))return false;
    for(float value:matrix)if(!std::isfinite(value))return false;
    return true;
}
namespace {
struct BeamInfo {
    int type{};void* start_entity{};int start_attachment{};void* end_entity{};int end_attachment{};
    Vector start{},end{};int model{-1};const char* model_name{"sprites/physbeam.vmt"};int halo{-1};const char* halo_name{};
    float halo_scale{},life{1.5f},width{1.5f},end_width{1.5f},fade{},amplitude{},brightness{255.f},speed{};
    int start_frame{};float frame_rate{},red{},green{},blue{};bool renderable{true};int segments{2},flags{};Vector center{};float start_radius{},end_radius{};
};
void* beams{};
struct VisualEvent {
    const char* Name(){return MEM::GetVFunc<const char*(__thiscall*)(void*)>(this,1)(this);}
    int Int(const char* key){return MEM::GetVFunc<int(__thiscall*)(void*,const char*,int)>(this,6)(this,key,0);}
    float Float(const char* key){return MEM::GetVFunc<float(__thiscall*)(void*,const char*,float)>(this,8)(this,key,0.f);}
};
std::array<Vector,65> last_impact{};std::array<float,65> impact_time{};
void VisualEventImpl(VisualEvent* e){
    if(!e)return;const char* name=e->Name();if(!name)return;
    if(!std::strcmp(name,"round_start") || !std::strcmp(name,"game_newmap") || !std::strcmp(name,"cs_game_disconnected")){ClearEvents();last_impact={};impact_time={};
        if(!std::strcmp(name,"game_newmap") || !std::strcmp(name,"cs_game_disconnected")){RestoreCamera();RestoreEnvironment();ReleaseMaterials();Settings disabled;Cvars(disabled,false);connected=false;}
        return;}
    if(!Interfaces::Engine || !Interfaces::Engine->IsInGame() || !ctx.local || !Interfaces::EntityList || !Interfaces::Globals)return;
    const auto s=Get();const float now=Interfaces::Globals->curtime;
    const auto user=[&](int id){return MEM::GetVFunc<int(__thiscall*)(void*,int)>(Interfaces::Engine,9)(Interfaces::Engine,id);};
    const int index=user(e->Int("userid"));if(index<1 || index>64)return;
    auto* player=static_cast<CCSPlayer*>(Interfaces::EntityList->GetClientEntity(index));if(!player)return;
    const int local=Interfaces::Engine->GetLocalPlayer();
    if(!std::strcmp(name,"bullet_impact")){
        const Vector end{e->Float("x"),e->Float("y"),e->Float("z")};if(!std::isfinite(end.x) || !std::isfinite(end.y) || !std::isfinite(end.z))return;
        last_impact[index]=end;impact_time[index]=now;
        const unsigned group=index==local ? 0u : player->m_iTeamNum()!=ctx.local->m_iTeamNum() ? 1u : 2u;
        if((s.tracer_targets&(1u<<group)) || (s.impacts && group==0))AddEvent({player->GetEyePosition(),end,now,0,group});
        if(beams && (s.tracer_targets&(1u<<group)) && (s.tracer_styles[group]&1u)){
            BeamInfo info;info.start=player->GetEyePosition();info.end=end;const auto c=s.tracer_colors[group];info.red=static_cast<float>((c>>16)&255);info.green=static_cast<float>((c>>8)&255);info.blue=static_cast<float>(c&255);info.brightness=static_cast<float>((c>>24)&255);
            auto* beam=MEM::GetVFunc<void*(__thiscall*)(void*,BeamInfo&)>(beams,12)(beams,info);
            if(beam)MEM::GetVFunc<void(__thiscall*)(void*,void*)>(beams,5)(beams,beam);
        }
    }else if(!std::strcmp(name,"player_hurt") && user(e->Int("attacker"))==local && s.hitmarkers){
        Vector position=player->GetEyePosition();if(impact_time[local]>0.f && now-impact_time[local]>=0.f && now-impact_time[local]<.25f)position=last_impact[local];
        AddEvent({{},position,now,1,0});
    }
}
class VisualListener {
public:
    virtual ~VisualListener()=default;
    virtual void FireGameEvent(VisualEvent* e){__try{VisualEventImpl(e);}__except(EXCEPTION_EXECUTE_HANDLER){}}
    virtual int GetEventDebugID(){return 42;}
} visual_listener;
bool visual_registered{};
void RegisterVisualEvents(){
    beams=reinterpret_cast<void*>(offsets::Get("visual_render_beams"));
    if(!Interfaces::GameEvent)return;
    bool ok=true;auto add=MEM::GetVFunc<bool(__thiscall*)(void*,void*,const char*,bool)>(Interfaces::GameEvent,3);
    for(const char* name:{"bullet_impact","player_hurt","round_start","game_newmap","cs_game_disconnected"})ok=add(Interfaces::GameEvent,&visual_listener,name,false) && ok;
    if(!ok){MEM::GetVFunc<void(__thiscall*)(void*,void*)>(Interfaces::GameEvent,5)(Interfaces::GameEvent,&visual_listener);LOG_WARN("visual misc: event registration failed");return;}visual_registered=true;
}
}
void Initialize(){
    RegisterVisualEvents();
    smoke_count=reinterpret_cast<int*>(offsets::Get("visual_smoke_count"));
    camera_flag=static_cast<std::ptrdiff_t>(offsets::Get("input_thirdperson_flag"));camera_offset=0;
    // Bind CInput::GetCameraOffset (slot 34), not a global vector-getter
    // signature: unrelated entity vector getters have the same instructions.
    if(Interfaces::Input){
        const auto* getter=MEM::GetVFunc<const unsigned char*>(Interfaces::Input,34);
        const unsigned char prefix[]{0x55,0x8b,0xec,0x8b,0x55,0x08,0x8b,0x81};
        if(getter && !std::memcmp(getter,prefix,sizeof prefix)){
            int offset{};std::memcpy(&offset,getter+8,4);
            if(Valid(offset) && offset>camera_flag && offset-camera_flag<=8)camera_offset=offset;
        }
    }
    if(const auto address=offsets::Get("calc_viewmodel_landing_bob"))bob_hook.attach(reinterpret_cast<void*>(address),reinterpret_cast<void*>(&LandingBob),reinterpret_cast<void**>(&bob_original));
    observer_offset=PropManager::Get().GetOffset("DT_BasePlayer","m_iObserverMode");
    if(Interfaces::ClientMode){
        view_original=MEM::GetVFunc<ViewFn>(Interfaces::ClientMode,18);view_hook.attach(Interfaces::ClientMode,18,reinterpret_cast<void*>(&OverrideView));
        fov_original=MEM::GetVFunc<FovFn>(Interfaces::ClientMode,35);fov_hook.attach(Interfaces::ClientMode,35,reinterpret_cast<void*>(&ViewmodelFov));
        draw_vm_original=MEM::GetVFunc<DrawVmFn>(Interfaces::ClientMode,27);draw_vm_hook.attach(Interfaces::ClientMode,27,reinterpret_cast<void*>(&DrawViewmodel));
    }
    if(Interfaces::Engine){aspect_original=MEM::GetVFunc<AspectFn>(Interfaces::Engine,101);aspect_hook.attach(Interfaces::Engine,101,reinterpret_cast<void*>(&Aspect));}
    if(const auto address=offsets::Get("calc_viewmodel_view"))vm_hook.attach(reinterpret_cast<void*>(address),reinterpret_cast<void*>(&Viewmodel),reinterpret_cast<void**>(&vm_original));
}
void Shutdown(){
    VisualPost::Reset();
    if(visual_registered && Interfaces::GameEvent)MEM::GetVFunc<void(__thiscall*)(void*,void*)>(Interfaces::GameEvent,5)(Interfaces::GameEvent,&visual_listener);
    visual_registered=false;beams=nullptr;ClearEvents();PublishTrajectory({});
    view_hook.detach();fov_hook.detach();draw_vm_hook.detach();aspect_hook.detach();vm_hook.detach();bob_hook.detach();
    __try{RestorePunch();RestoreSmoke();RestoreFlash();RestoreCamera();RestoreEnvironment();ReleaseMaterials();Settings disabled;Cvars(disabled,false);}__except(EXCEPTION_EXECUTE_HANDLER){}
    model_alpha.store(1.f);removal_bits.store(0);connected=false;
}
}
