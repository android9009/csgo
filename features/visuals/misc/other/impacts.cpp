#include "impacts.hpp"
#include "../../../../context.hpp"
#include "../../../../sdk/interfaces/interfaces.hpp"
#include "../../../../sdk/entity/player.hpp"
#include "../../../../sdk/entity/entity.hpp"
#include "../../../../sdk/trace_ray.hpp"
#include "../../../../utils/math/math.hpp"
#include "../../../../utils/memory.hpp"
#include "../../../../utils/log.hpp"
#include <array>
#include <cstring>
#include <mutex>

namespace Impacts {
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
    Vector end{};
    float time{};
};
std::array<Record, 16> records{};
std::size_t next{};
bool registered{};
float Now() { return Interfaces::Globals ? Interfaces::Globals->curtime : 0.f; }
void Clear() {
    for (auto& r : records) r.valid = false;
}

void OnFire(Event* event) {
    if (!event) return;
    if (!Snapshot().enabled) {
        Clear();
        return;
    }
    if (!ctx.in_game || !ctx.local || !Interfaces::Engine || !Interfaces::EntityList || !Interfaces::EngineTrace) return;
    const char* name = event->Name();
    if (!name || std::strcmp(name, "weapon_fire") != 0) return;
    if (Interfaces::Engine->GetPlayerForUserID(event->Int("userid")) != Interfaces::Engine->GetLocalPlayer()) return;

    // The crosshair ray to where the bullet stops.
    QAngle angles;
    Interfaces::Engine->GetViewAngles(angles);
    const Vector direction = Math::angle_vector(angles);
    const Vector eye = ctx.local->GetEyePosition();
    Ray_t ray;
    ray.Init(eye, eye + direction * 8192.f);
    CTraceFilterSimple filter(ctx.local);
    CGameTrace trace;
    Interfaces::EngineTrace->TraceRay(ray, 0x4600400Bu, &filter, &trace);

    // A stop on a player is the hitmarker's territory.
    if (trace.m_pEnt) {
        auto* entity = static_cast<CBaseEntity*>(trace.m_pEnt);
        const int index = entity->GetIndex();
        const int max_clients = Interfaces::Engine->GetMaxClients();
        if (index >= 1 && index <= max_clients && Interfaces::EntityList->GetClientEntity(index) == entity) return;
    }

    auto& r = records[next++ % records.size()];
    r.valid = true;
    r.end = trace.endpos;
    r.time = Now();
}

class Listener {
public:
    virtual ~Listener() = default;
    virtual void FireGameEvent(Event* event) {
        __try { OnFire(event); }
        __except (EXCEPTION_EXECUTE_HANDLER) { Clear(); }
    }
    virtual int GetEventDebugID() { return 44; }
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
    const auto add = MEM::GetVFunc<bool(__thiscall*)(void*, void*, const char*, bool)>(Interfaces::GameEvent, 3);
    if (!add(Interfaces::GameEvent, &listener, "weapon_fire", false)) {
        MEM::GetVFunc<void(__thiscall*)(void*, void*)>(Interfaces::GameEvent, 5)(Interfaces::GameEvent, &listener);
        LOG_WARN("impacts: event listener unavailable");
        return;
    }
    registered = true;
    LOG_INFO("impacts: weapon_fire listener registered");
}

void Shutdown() {
    if (registered && Interfaces::GameEvent)
        MEM::GetVFunc<void(__thiscall*)(void*, void*)>(Interfaces::GameEvent, 5)(Interfaces::GameEvent, &listener);
    registered = false;
    Clear();
}

void Contribute(VisualRuntime::FxFrame& frame) {
    const auto s = Snapshot();
    if (!s.enabled || !ctx.in_game || !ctx.local) return;
    const float now = Now();
    int width = 0, height = 0;
    if (Interfaces::Engine) Interfaces::Engine->GetScreenSize(width, height);
    if (width < 2 || height < 2) return;

    for (auto& r : records) {
        if (frame.mark_count >= static_cast<int>(frame.marks.size())) break;
        if (!r.valid) continue;
        const float age = now - r.time;
        if (age < 0.f || age >= Detail::kLife) {
            r.valid = false;
            continue;
        }
        Vector2D p;
        if (!Math::world_to_screen(r.end, p)) continue;
        const float t = age / Detail::kLife;
        frame.marks[frame.mark_count++] = {
            p.x / width, p.y / height, Detail::kSize, 1.f,
            Detail::Fade(s.color1, t), Detail::Fade(s.color2, t), 0};
    }
}
} // namespace Impacts
