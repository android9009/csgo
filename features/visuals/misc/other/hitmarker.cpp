#include "hitmarker.hpp"
#include "../../../../context.hpp"
#include "../../../../sdk/interfaces/interfaces.hpp"
#include "../../../../sdk/entity/player.hpp"
#include "../../../../utils/math/math.hpp"
#include "../../../../utils/memory.hpp"
#include "../../../../utils/log.hpp"
#include <array>
#include <cstring>
#include <mutex>

namespace Hitmarker {
namespace {
Settings g_snapshot;
std::mutex g_mutex;

// CS:GO IGameEvent / IGameEventManager2 ABI, matching Fatality's SDK (same shape the
// on-shot chams use in features/visuals/chams/shot_chams.cpp).
struct Event {
    const char* Name() { return MEM::GetVFunc<const char*(__thiscall*)(void*)>(this, 1)(this); }
    int Int(const char* key) {
        return MEM::GetVFunc<int(__thiscall*)(void*, const char*, int)>(this, 6)(this, key, 0);
    }
};

struct Record {
    bool valid{};
    int index{};
    void* entity{};
    Vector pos{}; // where the victim was at the moment of the hit (fallback)
    float time{};
};
std::array<Record, 8> records{};
std::size_t next{};
bool registered{};
float Now() { return Interfaces::Globals ? Interfaces::Globals->curtime : 0.f; }
void Clear() {
    for (auto& r : records) r.valid = false;
}

void OnEvent(Event* event) {
    if (!event || !ctx.in_game || !ctx.local || !Interfaces::Engine || !Interfaces::EntityList) return;
    const char* name = event->Name();
    if (!name) return;
    if (!std::strcmp(name, "round_start") || !std::strcmp(name, "game_newmap")) {
        Clear();
        return;
    }
    // A confirmed hit: the server tells us a player was hurt by the local player.
    // Never an enemy-fire timer - that is how hitmarkers end up on players nobody
    // shot.
    if (std::strcmp(name, "player_hurt") != 0) return;
    if (Interfaces::Engine->GetPlayerForUserID(event->Int("attacker")) != Interfaces::Engine->GetLocalPlayer()) return;
    const int victim = Interfaces::Engine->GetPlayerForUserID(event->Int("userid"));
    if (victim < 1 || victim > 64) return;
    auto* entity = static_cast<CCSPlayer*>(Interfaces::EntityList->GetClientEntity(victim));
    if (!entity) return;

    auto& r = records[next++ % records.size()];
    r.valid = true;
    r.index = victim;
    r.entity = entity;
    r.pos = entity->m_vecOrigin();
    r.time = Now();
}

class Listener {
public:
    virtual ~Listener() = default;
    virtual void FireGameEvent(Event* event) {
        __try { OnEvent(event); }
        __except (EXCEPTION_EXECUTE_HANDLER) { Clear(); }
    }
    virtual int GetEventDebugID() { return 45; }
} listener;
} // namespace

void Publish(const Settings& s) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_snapshot = s;
}

Settings Snapshot() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_snapshot;
}

void Initialize() {
    if (registered || !Interfaces::GameEvent) return;
    auto add = MEM::GetVFunc<bool(__thiscall*)(void*, void*, const char*, bool)>(Interfaces::GameEvent, 3);
    bool ok = true;
    for (const char* event : {"player_hurt", "round_start", "game_newmap"})
        ok = add(Interfaces::GameEvent, &listener, event, false) && ok;
    if (!ok) {
        MEM::GetVFunc<void(__thiscall*)(void*, void*)>(Interfaces::GameEvent, 5)(Interfaces::GameEvent, &listener);
        LOG_WARN("hitmarker: event listener unavailable");
        return;
    }
    registered = true;
    LOG_INFO("hitmarker: player_hurt listener registered");
}

void Shutdown() {
    if (registered && Interfaces::GameEvent)
        MEM::GetVFunc<void(__thiscall*)(void*, void*)>(Interfaces::GameEvent, 5)(Interfaces::GameEvent, &listener);
    registered = false;
    Clear();
}

void Contribute(VisualRuntime::FxFrame& frame) {
    const auto s = Snapshot();
    const bool any = s.world || s.screen || s.kibit;
    if (!any || !ctx.in_game) return;
    const float now = Now();
    int width = 0, height = 0;
    if (Interfaces::Engine) Interfaces::Engine->GetScreenSize(width, height);
    if (width < 2 || height < 2) return;

    for (auto& r : records) {
        if (!r.valid) continue;
        const float age = now - r.time;
        const bool in_screen = s.screen && Detail::Within(age, s.screen_duration);
        const bool in_world = s.world && Detail::Within(age, s.world_duration);
        const bool in_kibit = s.kibit && Detail::Within(age, s.kibit_duration);
        if (age < 0.f || (!in_screen && !in_world && !in_kibit)) {
            r.valid = false;
            continue;
        }
        // The three selections coexist, each for its own duration.
        if (in_screen && frame.mark_count < static_cast<int>(frame.marks.size()))
            frame.marks[frame.mark_count++] = {.5f, .5f, Detail::kScreenSize, 1.f, s.screen_color, s.screen_color, 2};
        if (in_world || in_kibit) {
            // world and kibit both sit on the victim, following it while it is alive.
            Vector pos = r.pos;
            auto* entity = Interfaces::EntityList
                ? static_cast<CCSPlayer*>(Interfaces::EntityList->GetClientEntity(r.index))
                : nullptr;
            if (entity == r.entity && entity && !entity->IsDormant() && entity->IsAlive())
                pos = entity->m_vecOrigin();
            if (!pos.IsValid()) continue;
            Vector2D p;
            if (!Math::world_to_screen(pos, p)) continue;
            const float nx = p.x / width;
            const float ny = p.y / height;
            if (in_world && frame.mark_count < static_cast<int>(frame.marks.size()))
                frame.marks[frame.mark_count++] = {nx, ny, Detail::kWorldSize, 1.f, s.world_color, s.world_color, 1};
            if (in_kibit && frame.mark_count < static_cast<int>(frame.marks.size()))
                frame.marks[frame.mark_count++] = {nx, ny, Detail::kKibitSize, 1.f, s.kibit_color, s.kibit_color, 1};
        }
    }
}
} // namespace Hitmarker
