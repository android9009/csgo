#pragma once
#include "visual_preview_state.hpp"
#include <array>
#include <mutex>
#include <atomic>
namespace VisualRuntime {
using Material=VisualPreview::Material;
enum Subject { Enemy, Team, Local, SubjectCount };
inline int SubjectFor(int local_team,int entity_team,bool local) {return local?Local:local_team==entity_team?Team:Enemy;}
enum Toggle { Enabled,Box,Skeleton,HistorySkeleton,Name,Health,Armor,Ammo,Weapon,Icon,Sound,ToggleCount };
enum Tint { BoxColor,SkeletonColor,HistoryColor,NameColor,HealthColor,HealthEnd,ArmorColor,AmmoColor,WeaponColor,IconColor,SoundColor,TintCount };
struct Esp {
    std::array<bool,ToggleCount> show{};
    std::array<std::uint32_t,TintCount> colors{};
    std::uint64_t flags{};
    VisualPreview::Layout layout{};
};
struct Highlight {bool enabled{};std::uint32_t color{0xffffffff};};
struct Effects {
    bool chams_enabled{};
    std::array<Material,3> players{},ragdolls{},attachments{},objects{};
    Material arms;
    std::array<Highlight,3> player_glow{},object_glow{};
    std::array<bool,3> object_enabled{};
};
struct Settings : Effects {std::array<Esp,3> esp{};};
inline std::mutex mutex;
inline Settings settings;
inline std::atomic<bool> any_material{false};
inline std::atomic<bool> glow_closing{false};
inline Settings Get(){std::lock_guard<std::mutex> lock(mutex);return settings;}
inline Effects GetEffects(){std::lock_guard<std::mutex> lock(mutex);return static_cast<const Effects&>(settings);}
inline void Publish(const Settings& s){
    bool any=false;
    for(int i=0;i<3;++i)any=any || (s.objects[i].enabled && s.objects[i].kind>0) ||
        (s.chams_enabled && (s.players[i].kind>0 || s.ragdolls[i].kind>0 || s.attachments[i].kind>0));
    any=any || (s.chams_enabled && s.arms.kind>0);
    {std::lock_guard<std::mutex> lock(mutex);settings=s;}
    any_material.store(any,std::memory_order_relaxed);
}
struct Segment {VisualPreview::Point a,b;};
struct Player {
    int subject{},health{},armor{},ammo{},max_ammo{30};
    std::uint64_t flags{};
    char name[128]{},weapon[48]{};
    VisualPreview::Box box;
    std::array<Segment,64> skeleton{};int segments{};
};
struct Players {std::array<Player,64> items{};int count{};};
inline Players player_frame;
inline Players GetPlayers(){std::lock_guard<std::mutex> lock(mutex);return player_frame;}
inline void PublishPlayers(const Players& p){std::lock_guard<std::mutex> lock(mutex);player_frame=p;}
void CapturePlayers();
void UpdateGlow();
void ShutdownGlow();
}
