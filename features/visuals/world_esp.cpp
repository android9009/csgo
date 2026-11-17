#include "../../sdk/world_esp_state.hpp"
#include "../../sdk/visual_entity_read.hpp"
#include "../../sdk/interfaces/interfaces.hpp"
#include "../../sdk/netvars.hpp"
#include "../../context.hpp"
#include "../../utils/offsets/offsets.hpp"
#include "../../utils/log.hpp"
#include <cstdio>
#include <cstring>
namespace WorldEsp {
namespace {
Frame cached;
Options previous;
std::uint64_t next_scan{};
std::ptrdiff_t owner{},ticking{},blow{},defuse_end{},defuser{},dormant{},origin{},armor{};
std::uintptr_t matrix_address{};
VisualRead::Module client;
bool initialized{};
struct ClassEntry {ClientClass* cls{};Category category{None};bool planted{},projectile{};char label[48]{};};
std::array<ClassEntry,512> classes;
std::size_t class_count{};
int Fault(EXCEPTION_POINTERS* info,const char* where){if(info && info->ExceptionRecord)LOG_ERROR("world esp: %s exception code=0x%08lX address=%p",where,info->ExceptionRecord->ExceptionCode,info->ExceptionRecord->ExceptionAddress);return EXCEPTION_EXECUTE_HANDLER;}
template<class T> T Read(const void* entity,std::ptrdiff_t offset){return *reinterpret_cast<const T*>(static_cast<const unsigned char*>(entity)+offset);}
bool Offset(std::ptrdiff_t n){return n>0 && n<0x20000;}
const ClassEntry* ClassInfo(CBaseEntity* entity){
    auto* cls=VisualRead::Class(entity,client);if(!cls)return nullptr;
    for(std::size_t i=0;i<class_count;++i)if(classes[i].cls==cls)return &classes[i];
    if(class_count==classes.size())return nullptr;
    auto& entry=classes[class_count++];entry.cls=cls;entry.category=Classify(cls->m_pNetworkName);entry.planted=!std::strcmp(cls->m_pNetworkName,"CPlantedC4");entry.projectile=Projectile(cls->m_pNetworkName);
    const char* name=cls->m_pNetworkName;if(entry.category==C4)name="C4";else if(!std::strncmp(name,"CWeapon",7))name+=7;else if(name[0]=='C')++name;
    std::snprintf(entry.label,sizeof entry.label,"%s",name);return &entry;
}
// Raw leaf: no mutex, PropManager calls, function-local statics, containers
// with destructors, or publication. The only SEH callers are ScanRaw/FrameRaw.
bool CaptureScan(const Options& options,Frame& out){
    if(!Interfaces::Engine || !Interfaces::Engine->IsInGame() || !Interfaces::Engine->IsConnected() || !Interfaces::EntityList || !Interfaces::Globals)return false;
    const int highest=(std::min)(Interfaces::EntityList->GetHighestEntityIndex(),4095);
    for(int i=1;i<=highest && out.count<out.items.size();++i){
        auto* entity=static_cast<CBaseEntity*>(Interfaces::EntityList->GetClientEntity(i));if(!entity)continue;
        if(Offset(dormant) && Read<bool>(entity,dormant))continue;
        const auto* type=ClassInfo(entity);if(!type || type->category==None || !options.enabled[type->category])continue;
        if(type->planted && (!Offset(ticking) || !Read<bool>(entity,ticking)))continue;
        if(!type->planted && !type->projectile){const auto handle=Read<std::uint32_t>(entity,owner);if(handle && handle!=0xffffffffu && Interfaces::EntityList->GetClientEntityFromHandle(handle))continue;}
        const auto position=Read<Vector>(entity,origin);if(!position.IsValid() || position.IsZero())continue;
        Item item;item.category=type->category;item.x=position.x;item.y=position.y;item.z=position.z;item.planted=type->planted;std::memcpy(item.label,type->label,sizeof item.label);
        if(type->planted){
            if(Offset(blow))item.explode_at=Read<float>(entity,blow);
            if(Offset(defuse_end))item.defuse_at=Read<float>(entity,defuse_end);
            const auto handle=Offset(defuser)?Read<std::uint32_t>(entity,defuser):0xffffffffu;
            item.defusing=handle && handle!=0xffffffffu && std::isfinite(item.defuse_at) && item.defuse_at>Interfaces::Globals->curtime;
            if(ctx.local && Offset(armor))item.estimated_damage=BombDamage(position.Dist(Read<Vector>(ctx.local,origin)),Read<int>(ctx.local,armor));
        }
        out.items[out.count++]=item;
    }
    return true;
}
bool ScanRaw(const Options& options,Frame& out){__try{return CaptureScan(options,out);}__except(Fault(GetExceptionInformation(),"scan")){return false;}}
bool CaptureFrame(Frame& out){
    if(!Interfaces::Engine || !Interfaces::Engine->IsInGame() || !Interfaces::Engine->IsConnected() || !Interfaces::Globals || !matrix_address || !client.Contains(reinterpret_cast<void*>(matrix_address),sizeof(out.matrix)))return false;
    std::memcpy(out.matrix.data(),reinterpret_cast<const void*>(matrix_address),sizeof out.matrix);
    out.time=Interfaces::Globals->curtime;out.framecount=static_cast<std::uint32_t>(Interfaces::Globals->framecount);
    return std::isfinite(out.time) && ValidMatrix(out.matrix);
}
bool FrameRaw(Frame& out){__try{return CaptureFrame(out);}__except(Fault(GetExceptionInformation(),"matrix")){return false;}}
}
void Initialize(){
    if(initialized)return;
    // Called before hooks run, outside every SEH scan and outside static-init guards.
    auto& net=PropManager::Get();owner=net.GetOffset("DT_BaseEntity","m_hOwnerEntity");if(!Offset(owner))owner=offsets::netvars::m_hOwnerEntity;
    ticking=net.GetOffset("DT_PlantedC4","m_bBombTicking");blow=net.GetOffset("DT_PlantedC4","m_flC4Blow");defuse_end=net.GetOffset("DT_PlantedC4","m_flDefuseCountDown");defuser=net.GetOffset("DT_PlantedC4","m_hBombDefuser");
    origin=offsets::netvars::m_vecOrigin;armor=offsets::netvars::m_ArmorValue;dormant=static_cast<std::ptrdiff_t>(offsets::Get("m_bDormant"));
    client=VisualRead::ClientModule();const auto offset=offsets::Get("dwViewMatrix");matrix_address=client.begin && offset ? client.begin+offset : 0;initialized=true;
}
void Scan(){
    const auto options=GetOptions();
    if(!initialized || !options.Any() || !ctx.in_game){cached={};previous=options;next_scan=0;class_count=0;Publish(cached);return;}
    const auto now=GetTickCount64();if(now<next_scan && previous.enabled==options.enabled)return;
    next_scan=now+200;previous=options;Frame next;
    LOG_INFO("world esp: scan start categories=%d/%d/%d",options.enabled[0],options.enabled[1],options.enabled[2]);
    const bool ok=ScanRaw(options,next);
    LOG_INFO("world esp: scan end items=%u categories=%d/%d/%d ok=%d",static_cast<unsigned>(next.count),options.enabled[0],options.enabled[1],options.enabled[2],ok);
    cached=ok ? next : Frame{};
    if(!ok){class_count=0;Publish(cached);} // No locks in the exception handler.
}
void Capture(){
    const auto options=GetOptions();Frame next=cached;
    next.valid=initialized && options.Any() && FrameRaw(next);next.captured_ms=GetTickCount64();
    if(!next.valid){next.count=0;cached={};next_scan=0;}
    static bool reported{},was_valid{};
    if(!reported || was_valid!=next.valid){LOG_INFO("world esp: matrix valid=%d",next.valid);reported=true;was_valid=next.valid;}
    Publish(next);
}
bool RenderClock(std::uint32_t& frame,float& time){__try{if(!Interfaces::Globals || !Interfaces::Engine || !Interfaces::Engine->IsInGame())return false;frame=static_cast<std::uint32_t>(Interfaces::Globals->framecount);time=Interfaces::Globals->curtime;return std::isfinite(time);}__except(Fault(GetExceptionInformation(),"clock")){return false;}}
}
