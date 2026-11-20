#include "effects.hpp"
#include "fx.hpp"
#include "../../../context.hpp"
#include "../../../sdk/entity/player.hpp"
#include "../../../sdk/entity/weapon.hpp"
#include "../../../sdk/interfaces/interfaces.hpp"
#include "../../../sdk/visual_runtime_state.hpp"
#include "../../../utils/event_listener.hpp"
#include "../../../utils/log.hpp"
#include "../../../utils/memory.hpp"
#include "../../../utils/offsets/offsets.hpp"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace VisualsMisc {
namespace Effects {
namespace {
using namespace VisualsFx;

// How long a tracer and an impact stay on screen. `main` uses a three second beam and a four
// second impact box; the tracer is shorter here because a laser that lingers for three seconds
// turns a fight into a light show.
constexpr float kTracerLife = 1.5f;
constexpr float kImpactLife = 4.f;
// Pixel sizes, converted to a fraction of the screen height when the frame is published.
constexpr float kImpactPixels = 4.f;
constexpr float kHitmarkerPixels = 10.f;
constexpr float kKibitPixels = 18.f;

// The game thread owns these: the listener runs on it, and so does the frame stage.
std::vector<Tracer> g_tracers;
std::vector<Impact> g_impacts;
std::vector<Hit> g_hits;
float g_last_time{};
bool g_time_valid{};
bool g_reported{};

Vec3 ToVec(const Vector& v) { return Vec3{v.x, v.y, v.z}; }

float Now() {
    return Interfaces::Globals ? Interfaces::Globals->curtime : 0.f;
}

bool InGame() {
    return Interfaces::Engine && ctx.in_game;
}

// The entity behind an event's `userid`, or nullptr. `GetPlayerForUserID` is slot 9 of
// IEngineClient, next to the verified GetPlayerInfo (8): the event carries a user id, not an
// entity index, and this is the only published way to convert one into the other.
//
// The index is range-checked before it reaches the entity list. That is what keeps a wrong
// slot harmless: an out-of-range index would index past the list, while this version simply
// reports "no shooter" once and the feature stays quiet instead of crashing.
CCSPlayer* PlayerFromUserId(int user_id) {
    if (!Interfaces::Engine || !Interfaces::EntityList) return nullptr;
    const int index = Interfaces::Engine->GetPlayerForUserID(user_id);
    const int max_clients = Interfaces::Engine->GetMaxClients();
    if (index <= 0 || index > max_clients || max_clients <= 0 || max_clients > 64) {
        static bool logged = false;
        if (!logged && user_id > 0) {
            logged = true;
            LOG_WARN("visuals misc: GetPlayerForUserID(%d) returned %d (max %d); if tracers and "
                     "hitmarkers stay off on this build, that slot is the reason",
                     user_id, index, max_clients);
        }
        return nullptr;
    }
    return static_cast<CCSPlayer*>(Interfaces::EntityList->GetClientEntity(index));
}

void OnBulletImpact(IGameEvent* event, const Settings& settings) {
    CCSPlayer* shooter = PlayerFromUserId(event->GetInt("userid"));
    if (!shooter) return;
    const Vec3 impact{event->GetFloat("x"), event->GetFloat("y"), event->GetFloat("z")};
    if (!std::isfinite(impact.x) || !std::isfinite(impact.y) || !std::isfinite(impact.z)) return;

    const bool local = ctx.local && shooter == ctx.local;
    const bool teammate = !local && ctx.local && shooter->m_iTeamNum() == ctx.local->m_iTeamNum();
    const int category = Category(local, teammate);
    const float now = Now();

    // One record per impact, i.e. one per shot for every weapon that fires a single bullet.
    // A shotgun's pellets each land on their own impact point, which is what the game reports.
    if (settings.tracer[category]) {
        if (g_tracers.size() >= 128) g_tracers.erase(g_tracers.begin());
        g_tracers.push_back(Tracer{ToVec(shooter->GetEyePosition()), impact, category, now});
    }
    if (local && settings.impacts) {
        if (g_impacts.size() >= 128) g_impacts.erase(g_impacts.begin());
        g_impacts.push_back(Impact{impact, now});
    }
}

void OnPlayerHurt(IGameEvent* event, const Settings& settings) {
    if (!ctx.local || !Interfaces::Engine) return;
    // Only damage this module's own shots dealt: "attacker" is a userid, the same as "userid".
    const int attacker = Interfaces::Engine->GetPlayerForUserID(event->GetInt("attacker"));
    if (attacker != Interfaces::Engine->GetLocalPlayer()) return;
    CCSPlayer* victim = PlayerFromUserId(event->GetInt("userid"));
    if (!victim || victim == ctx.local) return;
    const bool any = settings.hitmarker[1] || settings.hitmarker[2] || settings.hitmarker[3];
    if (!any) return;
    const Vec3 position = ToVec(victim->m_vecOrigin());
    const int amount = event->GetInt("dmg_health");
    const float now = Now();
    for (int kind = 1; kind <= 3; ++kind) {
        if (!settings.hitmarker[kind]) continue;
        if (g_hits.size() >= 64) g_hits.erase(g_hits.begin());
        g_hits.push_back(Hit{position, kind, amount, now});
    }
}

// The hitmarker lifetimes come from the menu (one slider per variant); the array is filled
// from the published settings before Prune runs, so the predicate stays cheap.
std::array<float, 4> g_current_life{0.f, 1.f, 1.f, 1.f};

void Prune(float now) {
    auto expired = [&](float born, float life) { return !Alive(born, now, life); };
    g_tracers.erase(std::remove_if(g_tracers.begin(), g_tracers.end(),
                                   [&](const Tracer& t) { return expired(t.born, kTracerLife); }),
                    g_tracers.end());
    g_impacts.erase(std::remove_if(g_impacts.begin(), g_impacts.end(),
                                   [&](const Impact& i) { return expired(i.born, kImpactLife); }),
                    g_impacts.end());
    g_hits.erase(std::remove_if(g_hits.begin(), g_hits.end(),
                                [&](const Hit& h) {
                                    const auto& life = g_current_life;
                                    return expired(h.born, life[h.kind]);
                                }),
                 g_hits.end());
}

} // namespace

bool Setup() {
    EventListener::SetHandler(&OnEvent);
    if (!g_reported) {
        g_reported = true;
        LOG_INFO("visuals misc: tracers/impacts/hitmarker listen on bullet_impact and player_hurt");
    }
    return true;
}

void Reset() {
    g_tracers.clear();
    g_impacts.clear();
    g_hits.clear();
    g_time_valid = false;
    VisualRuntime::PublishFx(VisualRuntime::FxFrame{});
}

void OnEvent(IGameEvent* event) {
    if (!event) return;
    __try {
        const char* name = event->GetName();
        if (!name) return;
        const Settings settings = VisualRuntime::GetEffects().fx;
        if (std::strcmp(name, "bullet_impact") == 0) OnBulletImpact(event, settings);
        else if (std::strcmp(name, "player_hurt") == 0) OnPlayerHurt(event, settings);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            LOG_WARN("visuals misc: rejected a game-event read 0x%08lX; the record was dropped",
                     GetExceptionCode());
        }
    }
}

void Frame() {
    VisualRuntime::FxFrame frame;
    if (!InGame() || !Interfaces::Globals || !Interfaces::Engine) {
        Reset();
        return;
    }
    const Settings settings = VisualRuntime::GetEffects().fx;
    const float now = Now();
    // A clock that jumped backwards means a new map or a demo seek: the old records belong to
    // a world that no longer exists.
    if (g_time_valid && now < g_last_time) {
        g_tracers.clear();
        g_impacts.clear();
        g_hits.clear();
    }
    g_time_valid = true;
    g_last_time = now;

    g_current_life = settings.hitmarker_life;
    for (auto& life : g_current_life)
        if (!std::isfinite(life) || life <= 0.f) life = 1.f;
    Prune(now);

    if (g_tracers.empty() && g_impacts.empty() && g_hits.empty()) {
        VisualRuntime::PublishFx(frame);
        return;
    }

    std::array<float, 16> matrix{};
    const auto base = reinterpret_cast<std::uintptr_t>(MEM::GetModuleBase("client.dll"));
    const auto offset = offsets::Get("dwViewMatrix");
    if (!base || !offset) {
        VisualRuntime::PublishFx(frame);
        return;
    }
    std::memcpy(matrix.data(), reinterpret_cast<const void*>(base + offset), sizeof(float) * 16);

    int width = 0, height = 0;
    Interfaces::Engine->GetScreenSize(width, height);
    const float screen_height = static_cast<float>(height);
    if (screen_height <= 1.f) {
        VisualRuntime::PublishFx(frame);
        return;
    }

    for (const auto& tracer : g_tracers) {
        if (frame.line_count >= static_cast<int>(frame.lines.size())) break;
        ScreenSegment segment;
        if (!ProjectSegment(matrix, tracer.start, tracer.end, segment)) continue;
        if (!std::isfinite(segment.x1) || !std::isfinite(segment.y1)) continue;
        auto& line = frame.lines[frame.line_count++];
        line.x1 = segment.x1; line.y1 = segment.y1;
        line.x2 = segment.x2; line.y2 = segment.y2;
        line.width = TracerWidth(tracer.category < CategoryCount ? settings.tracer_style[tracer.category] : 1);
        line.color = settings.tracer_color[tracer.category < CategoryCount ? tracer.category : Enemy];
    }

    const float impact_size = NormalizedSize(kImpactPixels, screen_height);
    for (const auto& impact : g_impacts) {
        if (frame.mark_count >= static_cast<int>(frame.marks.size())) break;
        ScreenPoint point;
        if (!ProjectPoint(matrix, impact.position, point)) continue;
        auto& mark = frame.marks[frame.mark_count++];
        mark.x = point.x; mark.y = point.y;
        mark.size = impact_size;
        mark.color1 = settings.impact_color1;
        mark.color2 = settings.impact_color2;
        mark.kind = 0;
    }

    for (const auto& hit : g_hits) {
        if (frame.mark_count >= static_cast<int>(frame.marks.size())) break;
        ScreenPoint point{0.5f, 0.5f};
        // "World" sits on the victim, the other two on the crosshair; a victim that is not in
        // front of the camera has no world position to draw at, so its marker is dropped.
        if (hit.kind == 1 && !ProjectPoint(matrix, hit.position, point)) continue;
        auto& mark = frame.marks[frame.mark_count++];
        mark.x = point.x; mark.y = point.y;
        mark.size = NormalizedSize(hit.kind == 3 ? kKibitPixels : kHitmarkerPixels, screen_height);
        mark.color1 = settings.hitmarker_color[hit.kind < 4 ? hit.kind : 1];
        mark.color2 = mark.color1;
        mark.kind = hit.kind == 1 ? 1 : 2;
    }

    VisualRuntime::PublishFx(frame);
}
} // namespace Effects
} // namespace VisualsMisc
