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
    std::array<bool,3> gradient{};std::array<std::uint32_t,3> gradient_end{};
    std::uint64_t flags{};
    VisualPreview::Layout layout=EspDock::Defaults();
};
struct Highlight {bool enabled{};std::uint32_t color{0xffffffff};};
struct Effects {
    bool chams_enabled{};
    std::array<Material,3> players{},ragdolls{},attachments{},objects{};
    Material arms,shot;
    std::array<Highlight,3> player_glow{},object_glow{};
    std::array<bool,3> object_enabled{};
    // Visuals -> Misc: the two transparency sliders (0 = off, 100 = invisible).
    // Published as percentages; the draw hook converts them to alpha.
    float texture_transparency{},prop_transparency{};
    // Visuals -> World: "Fog" (the colour of the map's own fog, see features/visuals/
    // world_fog.hpp). Published as a packed rgba so the game thread never reads a
    // half-updated colour.
    bool world_fog{};
    std::uint32_t world_fog_colour{0xFFFFFFFFu};
    // Visuals -> World: "Nightmode" (world texture modulation), "Sky" (skybox texture
    // modulation) and "Fullbright" (the engine's own `mat_fullbright` switch). See
    // features/visuals/misc/world/world_modulation.hpp.
    bool nightmode{};
    std::uint32_t nightmode_colour{0xFFFFFFFFu};
    bool sky{};
    std::uint32_t sky_colour{0xFFFFFFFFu};
    bool fullbright{};
    // Visuals -> World: "Sunlight" (cascaded-shadow rotation override).
    bool sunlight{};
    // Visuals -> Misc -> local: "Remove scope" (mode, the 0..4 values of cfg_t::noscope),
    // its line colour/thickness/invert switch, and the "Spread" circle. The overlay in
    // menu/overlays/screen_overlay.hpp reads these; the game-side scope-mask logic has been
    // removed, so only the menu drawing consumes this block now.
    struct Screen {
        int scope_mode{};
        std::uint32_t scope_line_color{0xFF000000u}; // 0xRRGGBBAA, black by default
        float scope_line_size{100.f};
        bool scope_invert{};
        bool spread{};
        std::uint32_t spread_color1{0xFFFFFFFFu};
        std::uint32_t spread_color2{0xFFFFFFFFu};
    } screen;
};
struct Settings : Effects {std::array<Esp,3> esp{};};
// All runtime locks are recursive, deliberately: the modern MSVC STL
// std::mutex throws std::system_error ("resource deadlock would occur") when
// the same thread locks it twice, and one such re-entrant lock in the render
// path used to abort every EndScene frame so nothing - menu or overlays -
// ever drew. Cross-thread safety is unchanged (recursive_mutex is still a
// proper mutual-exclusion lock between threads).
inline std::recursive_mutex mutex;
inline Settings settings;
inline std::atomic<bool> any_material{false};
inline std::atomic<bool> any_transparency{false};
inline std::atomic<bool> shot_requested{false};
inline std::atomic<bool> glow_closing{false};
inline Settings Get(){std::lock_guard<std::recursive_mutex> lock(mutex);return settings;}
inline Effects GetEffects(){std::lock_guard<std::recursive_mutex> lock(mutex);return static_cast<const Effects&>(settings);}
inline void Publish(const Settings& s){
    bool any=false;
    for(int i=0;i<3;++i)any=any || (s.objects[i].enabled && s.objects[i].kind>0) ||
        (s.chams_enabled && (s.players[i].kind>0 || s.ragdolls[i].kind>0 || s.attachments[i].kind>0));
    any=any || (s.chams_enabled && (s.arms.kind>0 || s.shot.kind>0));
    {std::lock_guard<std::recursive_mutex> lock(mutex);settings=s;}
    any_material.store(any,std::memory_order_relaxed);
    shot_requested.store(s.chams_enabled && s.shot.enabled && s.shot.kind>0,std::memory_order_relaxed);
    any_transparency.store(s.texture_transparency>0.f || s.prop_transparency>0.f,std::memory_order_relaxed);
}
// Live game thread -> render thread state for the same overlay: whether the local player is
// scoped, the field of view the picture is drawn with and the weapon's current inaccuracy
// (radians). Everything the gap/radius maths needs, so the render thread never touches an
// entity.
struct ScreenState {
    bool scoped{};
    float inaccuracy{};
    float fov{90.f};
    int width{};
    int height{};
};
// One projected record from the game thread. Coordinates are normalized to the viewport
// (0..1), `size` is a fraction of the screen height.
struct ScreenLine {
    float x1{}, y1{}, x2{}, y2{};
    float width{1.5f};
    std::uint32_t color{};
};
struct ScreenMark {
    float x{}, y{};
    float size{};   // half-extent, fraction of the screen height
    float alpha{1.f};
    std::uint32_t color1{};
    std::uint32_t color2{};
    int kind{};     // 0 = impact box, 1 = world hitmarker, 2 = centre hitmarker,
                    // 3 = grenade trajectory marker (white ring, color2 core)
};

// "Grenade proximity warning": a radial glow with a ring and a small icon. `x`/`y`
// are normalized, `radius` and `glow` are fractions of the screen height, `icon`
// picks the silhouette (0 none, 1 grenade - also used for flash, 2 molotov/fire).
struct ScreenDisc {
    float x{}, y{};
    float radius{};
    float glow{};
    float alpha{1.f};
    int icon{};
    std::uint32_t color{};
};

// A floating label (predicted damage, remaining fuse, molotov distance) centred on
// (x, y) in normalized coordinates; `text` is NUL-terminated.
struct ScreenText {
    float x{}, y{};
    float alpha{1.f};
    std::uint32_t color{};
    char text[24]{};
};

// "Out of FOV": an arrow at the screen edge. `x`/`y` are normalized, `angle` is the direction
// in radians (0 = right, positive = downwards, matching the screen axes), `size` a fraction of
// the screen height.
struct ScreenArrow {
    float x{}, y{};
    float angle{};
    float size{};
    std::uint32_t color{};
};

// The events keep firing while the frame is built, so the list is a snapshot per frame: the
// game thread fills a fresh one at FRAME_RENDER_START and the render thread copies it.
struct FxFrame {
    std::array<ScreenLine, 96> lines{};
    int line_count{};
    std::array<ScreenMark, 96> marks{};
    int mark_count{};
    std::array<ScreenArrow, 32> arrows{};
    int arrow_count{};
    std::array<ScreenDisc, 8> discs{};
    int disc_count{};
    std::array<ScreenText, 24> texts{};
    int text_count{};
};
inline std::recursive_mutex fx_mutex;
inline FxFrame fx_frame;

inline void PublishFx(const FxFrame& frame) {
    std::lock_guard<std::recursive_mutex> lock(fx_mutex);
    fx_frame = frame;
}
inline FxFrame GetFx() {
    std::lock_guard<std::recursive_mutex> lock(fx_mutex);
    return fx_frame;
}

inline std::recursive_mutex screen_mutex;
inline ScreenState screen_state;

inline void PublishScreen(const ScreenState& state) {
    std::lock_guard<std::recursive_mutex> lock(screen_mutex);
    screen_state = state;
}
inline ScreenState GetScreen() {
    std::lock_guard<std::recursive_mutex> lock(screen_mutex);
    return screen_state;
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
// Fill a caller-owned persistent buffer; never return ~80 KiB by value on
// the game's small render-thread stack (including disabled-feature frames).
inline void CopyPlayers(Players& out){std::lock_guard<std::recursive_mutex> lock(mutex);out=player_frame;}
inline void ClearPlayers(){std::lock_guard<std::recursive_mutex> lock(mutex);player_frame.count=0;}
inline void PublishPlayers(const Players& p){std::lock_guard<std::recursive_mutex> lock(mutex);player_frame=p;}
void CapturePlayers();
void UpdateGlow();
void RestoreGlow();
void ShutdownGlow();
}
