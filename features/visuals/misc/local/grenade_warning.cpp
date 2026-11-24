#include "grenade_warning.hpp"
#include "../../../../context.hpp"
#include "../../../../sdk/convars.hpp"
#include "../../../../sdk/entity/entity.hpp"
#include "../../../../sdk/entity/player.hpp"
#include "../../../../sdk/interfaces/interfaces.hpp"
#include "../../../../sdk/netvars.hpp"
#include "../../../../sdk/trace_ray.hpp"
#include "../../../../utils/math/math.hpp"
#include "../../../../utils/memory.hpp"
#include "../../../../utils/log.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace GrenadeWarning {
namespace {

using Detail::KindFlash;
using Detail::KindFire;
using Detail::KindHe;
using Detail::KindMolotov;
using Detail::SweepHit;
using Detail::Vec3;

Settings g_snapshot;
std::mutex g_mutex;

// ---------------------------------------------------------------------------
// Time, cvars and entity access
// ---------------------------------------------------------------------------

float Now() { return ctx.in_game ? ctx.curtime : 0.f; }

float TickInterval() {
    if (Interfaces::Globals && Interfaces::Globals->interval_per_tick > 1e-4f)
        return Interfaces::Globals->interval_per_tick;
    return 1.f / 64.f;
}

// Cached ConVars (the reference reads cvar_float() per call; the name check in
// ConVars::Find makes a cached pointer safe across maps).
ConVars::ConVar* g_gravity{};
ConVars::ConVar* g_maxvelocity{};
ConVars::ConVar* g_inferno_lifetime{};
ConVars::ConVar* g_inferno_range{};
ConVars::ConVar* g_friendlyfire{};
ConVars::ConVar* g_self_scale{};
ConVars::ConVar* g_team_scale{};
bool g_cvars_done{};

void EnsureCvars() {
    if (g_cvars_done) return;
    g_cvars_done = true;
    g_gravity = ConVars::Find("sv_gravity");
    g_maxvelocity = ConVars::Find("sv_maxvelocity");
    g_inferno_lifetime = ConVars::Find("inferno_flame_lifetime");
    g_inferno_range = ConVars::Find("inferno_max_range");
    g_friendlyfire = ConVars::Find("mp_friendlyfire");
    g_self_scale = ConVars::Find("ff_damage_reduction_grenade_self");
    g_team_scale = ConVars::Find("ff_damage_reduction_grenade");
}

float CvarFloat(ConVars::ConVar* var, float fallback) {
    if (!var) return fallback;
    const float v = var->GetFloat();
    return std::isfinite(v) && v > -1e6f && v < 1e6f ? v : fallback;
}

// Negative-cached netvar offsets (PropManager does not cache misses).
std::unordered_map<std::string, std::ptrdiff_t> g_offsets;
std::mutex g_offsets_mutex;

std::ptrdiff_t PropOffset(const char* table, const char* prop) {
    const std::string key = std::string(table) + '\x1f' + prop;
    {
        std::lock_guard<std::mutex> lock(g_offsets_mutex);
        if (auto it = g_offsets.find(key); it != g_offsets.end()) return it->second;
    }
    const auto off = PropManager::Get().GetOffset(table, prop);
    std::lock_guard<std::mutex> lock(g_offsets_mutex);
    g_offsets.emplace(key, off);
    return off;
}

template <typename T>
T PropRead(const void* base, const char* table, const char* prop, T fallback = T{}) {
    if (!base) return fallback;
    const auto off = PropOffset(table, prop);
    if (!off || off < 0 || off > 0x20000) return fallback;
    const auto addr = reinterpret_cast<std::uintptr_t>(base) + static_cast<std::uintptr_t>(off);
    __try {
        const auto value = *reinterpret_cast<const T*>(addr);
        if constexpr (std::is_floating_point_v<T>) {
            return std::isfinite(value) ? value : fallback;
        }
        return value;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return fallback;
    }
}

// Per-frame pointer -> index cache (the trace hands back entity pointers).
std::unordered_map<const void*, int> g_ptr_index;
int g_ptr_index_frame{};

void RebuildPtrIndex() {
    static int last_frame{};
    const auto frame = Interfaces::Globals ? Interfaces::Globals->framecount : 0;
    if (frame == last_frame && !g_ptr_index.empty()) return;
    last_frame = frame;
    g_ptr_index.clear();
    if (!Interfaces::EntityList) return;
    const int highest = Interfaces::EntityList->GetHighestEntityIndex();
    for (int i = 0; i <= (std::min)(highest, 2048); ++i) {
        if (auto* entity = Interfaces::EntityList->GetClientEntity(i))
            g_ptr_index.emplace(entity, i);
    }
}

int IndexOf(const void* pointer) {
    if (!pointer) return 0;
    RebuildPtrIndex();
    if (auto it = g_ptr_index.find(pointer); it != g_ptr_index.end()) return it->second;
    return 0;
}

const char* ClassOf(const void* entity) {
    if (!entity) return nullptr;
    auto* cls = const_cast<CBaseEntity*>(static_cast<const CBaseEntity*>(entity))->GetClientClass();
    return cls ? cls->m_pNetworkName : nullptr;
}

// The entity's own recv-table name (what the Lua's entity.get_prop resolves
// through): CInferno -> "DT_Inferno", CHEGrenadeProjectile -> "DT_CHEGrenadeProjectile".
// PropManager walks this table and its parents, so inherited netvars are found.
const char* TableName(const void* entity) {
    if (!entity) return nullptr;
    auto* cls = const_cast<CBaseEntity*>(static_cast<const CBaseEntity*>(entity))->GetClientClass();
    if (!cls || !cls->m_pRecvTable) return nullptr;
    const char* name = nullptr;
    __try {
        name = cls->m_pRecvTable->m_pNetTableName;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        name = nullptr;
    }
    return name;
}

bool IsEnemyEntity(int index) {
    if (index <= 0 || !ctx.local || !Interfaces::EntityList) return false;
    auto* entity = static_cast<CBaseEntity*>(Interfaces::EntityList->GetClientEntity(index));
    if (!entity) return false;
    const int team = entity->m_iTeamNum();
    return (team == 2 || team == 3) && team != ctx.local->m_iTeamNum();
}

// The breakable classification the simulation needs. The Lua script resolves the
// full health from csgo/scripts/propdata.txt, the model keyvalues and the entity
// lump (func_breakable brushes); this repository exposes none of those, so only
// glass (CBreakableSurface, health 1) shatters - everything else is solid.
Detail::BreakTarget ClassifyBreakable(int entindex) {
    if (!Interfaces::EntityList) return {};
    auto* entity = static_cast<CBaseEntity*>(Interfaces::EntityList->GetClientEntity(entindex));
    const char* cls = ClassOf(entity);
    if (!cls) return {};
    if (std::strcmp(cls, "CBreakableSurface") == 0)
        return {1, false, true, 1.f, 1.f, 0.f, false};
    if (std::strcmp(cls, "CBaseEntity") == 0)
        return {2, false, false, 1.f, 1.5f, 0.f, false};
    if (std::strcmp(cls, "CDynamicProp") == 0)
        return {3, false, false, 0.f, 1.f, 0.f, false};
    if (std::strcmp(cls, "CPhysicsPropMultiplayer") == 0)
        return {4, false, false, 0.f, 1.f, 0.f, false};
    return {};
}

// ---------------------------------------------------------------------------
// Tracing
// ---------------------------------------------------------------------------

// Up to 33 skips (the local player + the 32 broken entities of one simulation).
struct MultiSkipFilter : ITraceFilter {
    const void* skip[34]{};
    int count{};
    bool ShouldHitEntity(void* entity, int) override {
        for (int i = 0; i < count; ++i)
            if (skip[i] == entity) return false;
        return true;
    }
    int GetTraceType() const override { return 0; }
};

bool g_trace_ready{};   // the engine trace produced sane results at least once
bool g_trace_enabled{}; // rechecked every second until it is
float g_trace_next_check{};

bool TraceLine(const Vec3& from, const Vec3& to, MultiSkipFilter* filter, CGameTrace* out) {
    if (!Interfaces::EngineTrace) return false;
    Ray_t ray;
    ray.Init(Vector(from.x, from.y, from.z), Vector(to.x, to.y, to.z));
    __try {
        Interfaces::EngineTrace->TraceRay(ray, Detail::kMaskSolid, filter, out);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The Lua's trace_is_sane(): six directions, a hit needs a normalized plane
// normal and a fraction in range; at least two sane directions count.
bool TraceIsSane() {
    if (!ctx.local || !Interfaces::Engine) return false;
    QAngle angles;
    Interfaces::Engine->GetViewAngles(angles);
    Vector forward{}, right{}, up{};
    Math::angle_vectors(angles, &forward, &right, &up);
    const Vector eye = ctx.local->GetEyePosition();
    const Vector directions[6]{
        {0.f, 0.f, -1.f},
        {0.f, 0.f, 1.f},
        forward,
        right,
        -right,
        {right.y, -right.x, 0.f}, // the Lua's {sy, -cy, 0}
    };
    MultiSkipFilter filter;
    filter.skip[filter.count++] = ctx.local;
    int matches = 0;
    for (const auto& direction : directions) {
        const Vector to{eye.x + direction.x * 1500.f, eye.y + direction.y * 1500.f,
                        eye.z + direction.z * 1500.f};
        CGameTrace trace;
        if (!TraceLine(eye, to, &filter, &trace)) return false;
        if (trace.fraction < 0.f || trace.fraction > 1.f) return false;
        if (trace.fraction < 1.f) {
            const float length = trace.plane.normal.Length();
            if (std::abs(length - 1.f) > 0.02f) return false;
            matches += 1;
        }
    }
    return matches >= 2;
}

void CheckTrace(float now) {
    if (g_trace_ready) {
        if (!g_trace_enabled && now >= g_trace_next_check) {
            g_trace_next_check = now + 1.f;
            g_trace_enabled = TraceIsSane();
        }
        return;
    }
    if (now < g_trace_next_check) return;
    g_trace_next_check = now + 1.f;
    if (TraceIsSane()) {
        g_trace_ready = true;
        g_trace_enabled = true;
    }
}

// The Lua's sweep(): a grenade-hull sweep (half-extent 2) against MASK_GRENADE,
// retraced as a line when starting solid. The Lua's trace_solid() loop passes a
// hit on another player into the shared ignore list and retraces (up to
// MAX_IGNORED), so the sweep goes through players; a startsolid hull is retraced
// from the previous position with the doubled delta. When the engine trace is
// not sane yet, the nine-corner ray fallback (sweep_rays + probe_normal) keeps
// a prediction running (its trace_ray does only the single retry, as the Lua).
bool Sweep(const Vec3& pos, const Vec3& delta, SweepHit& out, Detail::IgnoreList& ignored) {
    if (g_trace_enabled) {
        const int max_clients = Interfaces::Engine ? Interfaces::Engine->GetMaxClients() : 64;
        CGameTrace trace;
        for (;;) {
            MultiSkipFilter filter;
            filter.skip[filter.count++] = ctx.local;
            for (int i = 0; i < ignored.count && filter.count < 33; ++i) {
                if (auto* entity = Interfaces::EntityList->GetClientEntity(ignored.index[i]))
                    filter.skip[filter.count++] = entity;
            }
            const Vector start{pos.x, pos.y, pos.z};
            const Vector end{pos.x + delta.x, pos.y + delta.y, pos.z + delta.z};
            bool ok;
            __try {
                Ray_t ray;
                ray.Init(start, end, Vector(pos.x - 2.f, pos.y - 2.f, pos.z - 2.f),
                         Vector(pos.x + 2.f, pos.y + 2.f, pos.z + 2.f));
                Interfaces::EngineTrace->TraceRay(ray, Detail::kMaskGrenade, &filter, &trace);
                if (trace.startsolid) {
                    Ray_t retry;
                    retry.Init(Vector(pos.x - delta.x, pos.y - delta.y, pos.z - delta.z), end);
                    Interfaces::EngineTrace->TraceRay(retry, Detail::kMaskLine, &filter, &trace);
                }
                ok = true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                ok = false;
            }
            if (!ok) return false;
            out.hit = trace.fraction < 1.f;
            out.fraction = trace.fraction;
            out.end = {trace.endpos.x, trace.endpos.y, trace.endpos.z};
            out.normal = {trace.plane.normal.x, trace.plane.normal.y, trace.plane.normal.z};
            out.sky = (trace.surface.flags & Detail::kSurfSky) != 0;
            out.entindex = 0;
            out.standable = true;
            if (out.hit && trace.m_pEnt && trace.m_pEnt != ctx.local) {
                const int entindex = IndexOf(trace.m_pEnt);
                // Another player: ignore it for the rest of the simulation and
                // retrace through it (the Lua's trace_solid while-loop).
                if (entindex > 0 && entindex <= max_clients && ignored.Add(entindex))
                    continue;
                out.entindex = entindex;
                if (out.entindex > 0) {
                    const unsigned short solid =
                        PropRead<unsigned short>(trace.m_pEnt, "DT_BaseEntity", "m_usSolidFlags", 0);
                    out.standable = (solid & Detail::kSolidNotStandable) == 0;
                }
            }
            return true;
        }
    }

    // Ray fallback: nine corners of the grenade box, the smallest fraction wins;
    // the normal comes from two probe rays (probe_normal).
    struct Corner { float x, y, z; };
    static const Corner corners[9]{{0, 0, 0},
                                   {-2, -2, -2}, {2, -2, -2}, {-2, 2, -2}, {2, 2, -2},
                                   {-2, -2, 2}, {2, -2, 2}, {-2, 2, 2}, {2, 2, 2}};
    MultiSkipFilter filter;
    filter.skip[filter.count++] = ctx.local;
    float best = 1.f;
    int best_corner = 0;
    int skip_index = 0;
    for (int i = 0; i < 9; ++i) {
        const Vec3 from{pos.x + corners[i].x, pos.y + corners[i].y, pos.z + corners[i].z};
        const Vec3 to{from.x + delta.x, from.y + delta.y, from.z + delta.z};
        CGameTrace trace;
        if (!TraceLine(from, to, &filter, &trace)) return false;
        if (trace.fraction < best) {
            best = trace.fraction;
            best_corner = i;
            // The Lua's trace_ray(): a hit on a different player is retraced with
            // that player ignored (one retry).
            if (trace.m_pEnt && trace.m_pEnt != filter.skip[0] && skip_index < 1) {
                filter.skip[filter.count++] = trace.m_pEnt;
                CGameTrace retry;
                if (TraceLine(from, to, &filter, &retry) && retry.fraction < best) {
                    best = retry.fraction;
                    best_corner = i;
                    skip_index += 1;
                }
            }
        }
    }
    if (best >= 1.f) {
        out.hit = false;
        out.end = {pos.x + delta.x, pos.y + delta.y, pos.z + delta.z};
        return true;
    }
    const Vec3 origin{pos.x + corners[best_corner].x, pos.y + corners[best_corner].y,
                      pos.z + corners[best_corner].z};
    const Vector direction{delta.x, delta.y, delta.z};
    const float length = direction.Length();
    if (!(length > 1e-6f)) {
        out.hit = true;
        out.fraction = best;
        out.end = {origin.x + delta.x * best, origin.y + delta.y * best, origin.z + delta.z * best};
        out.normal = {0.f, 0.f, 1.f};
        return true;
    }
    const Vector u{direction.x / length, direction.y / length, direction.z / length};
    Vector ref{0.f, 0.f, 1.f};
    if (std::abs(u.z) > 0.9f) ref = {1.f, 0.f, 0.f};
    const Vector a = u.Cross(ref).Normalized();
    const Vector b = u.Cross(a);
    static const int signs[4][2]{{1, 1}, {-1, 1}, {1, -1}, {-1, -1}};
    for (const auto& sign : signs) {
        const Vec3 fa_origin{origin.x + a.x * sign[0], origin.y + a.y * sign[0],
                             origin.z + a.z * sign[0]};
        const Vec3 fb_origin{origin.x + b.x * sign[1], origin.y + b.y * sign[1],
                             origin.z + b.z * sign[1]};
        const Vec3 to{origin.x + delta.x, origin.y + delta.y, origin.z + delta.z};
        CGameTrace fa_trace, fb_trace;
        if (!TraceLine(fa_origin, to, &filter, &fa_trace)) continue;
        if (!TraceLine(fb_origin, to, &filter, &fb_trace)) continue;
        if (fa_trace.fraction < 1.f && fb_trace.fraction < 1.f) {
            const float offset_a = delta.x * (fa_trace.fraction - best);
            const float offset_a2 = delta.y * (fa_trace.fraction - best);
            const float offset_a3 = delta.z * (fa_trace.fraction - best);
            const float offset_b = delta.x * (fb_trace.fraction - best);
            const float offset_b2 = delta.y * (fb_trace.fraction - best);
            const float offset_b3 = delta.z * (fb_trace.fraction - best);
            const Vector p{a.x * sign[0] + offset_a, a.y * sign[0] + offset_a2,
                           a.z * sign[0] + offset_a3};
            const Vector q{b.x * sign[1] + offset_b, b.y * sign[1] + offset_b2,
                           b.z * sign[1] + offset_b3};
            const Vector n = p.Cross(q);
            const float n_length = n.Length();
            if (n_length > 1e-6f) {
                Vector normal = n / n_length;
                if (normal.Dot(u) > 0.f) normal = -normal;
                out.hit = true;
                out.fraction = best;
                out.end = {origin.x + delta.x * best, origin.y + delta.y * best,
                           origin.z + delta.z * best};
                out.normal = {normal.x, normal.y, normal.z};
                return true;
            }
        }
    }
    out.hit = true;
    out.fraction = best;
    out.end = {origin.x + delta.x * best, origin.y + delta.y * best, origin.z + delta.z * best};
    out.normal = {-u.x, -u.y, -u.z};
    return true;
}

// The Lua's find_burn_point(): where a molotov pool starts - 10 up, 138 down.
Vec3 BurnPoint(const Vec3& pos) {
    MultiSkipFilter filter;
    filter.skip[filter.count++] = ctx.local;
    const Vec3 from{pos.x, pos.y, pos.z + 10.f};
    const Vec3 to{pos.x, pos.y, pos.z + 10.f - 138.f};
    CGameTrace trace;
    if (TraceLine(from, to, &filter, &trace) && trace.fraction < 1.f)
        return {pos.x, pos.y, trace.endpos.z + 2.f};
    return {pos.x, pos.y, pos.z + 10.f};
}

// Blast visibility: 1 for a clear line of sight from the explosion to a sample
// point (the trace-branch of the Lua's exposed(); the five-iteration player
// skip loop only runs without a sane trace).
int Exposed(const Vec3& from, const Vec3& to) {
    if (g_trace_enabled) {
        MultiSkipFilter filter;
        filter.skip[filter.count++] = ctx.local;
        CGameTrace trace;
        if (!TraceLine(from, to, &filter, &trace)) return 0;
        return trace.fraction >= 1.f ? 1 : 0;
    }
    MultiSkipFilter filter;
    filter.skip[filter.count++] = ctx.local;
    Vec3 current = from;
    for (int i = 0; i < 5; ++i) {
        CGameTrace trace;
        if (!TraceLine(current, to, &filter, &trace)) return 0;
        if (trace.fraction >= 1.f) return 1;
        if (!trace.m_pEnt || trace.m_pEnt == filter.skip[0]) return 0;
        if (filter.count < 33) filter.skip[filter.count++] = trace.m_pEnt;
        current = {current.x + (to.x - current.x) * trace.fraction,
                   current.y + (to.y - current.y) * trace.fraction,
                   current.z + (to.z - current.z) * trace.fraction};
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Damage targets (the Lua's target_snapshots / damage_targets)
// ---------------------------------------------------------------------------

struct TargetSnap {
    bool valid{};
    bool alive{};
    bool enemy{};
    Vec3 origin{};
    float view_z{64.f};
    int armor{};
    int team{};
    int health{};
    bool heavy{};
};

std::unordered_map<int, TargetSnap> g_snapshots;
std::vector<int> g_damage_targets;
float g_targets_updated{};
int g_targets_count{};
int g_targets_revision{};

void UpdateDamageTargets(float now) {
    if (!Interfaces::EntityList || !ctx.local) return;
    const int highest = (std::min)(Interfaces::EntityList->GetHighestEntityIndex(), 256);
    // Collect the live CCSPlayer set (plus the local player, who may be dormant).
    std::vector<int> candidates;
    candidates.push_back(Interfaces::Engine->GetLocalPlayer());
    for (int i = 1; i <= highest; ++i) {
        auto* entity = Interfaces::EntityList->GetClientEntity(i);
        if (!entity) continue;
        const char* cls = ClassOf(entity);
        if (cls && std::strcmp(cls, "CCSPlayer") == 0) candidates.push_back(i);
    }
    if (now - g_targets_updated < Detail::kDamageInterval &&
        static_cast<int>(candidates.size()) == g_targets_count)
        return;
    g_targets_updated = now;
    g_targets_count = static_cast<int>(candidates.size());
    g_targets_revision += 1;

    const int local = Interfaces::Engine->GetLocalPlayer();
    std::vector<int> seen;
    seen.reserve(candidates.size());
    g_damage_targets.clear();
    for (int index : candidates) {
        auto* entity = static_cast<CBaseEntity*>(Interfaces::EntityList->GetClientEntity(index));
        if (!entity) continue;
        seen.push_back(index);
        const bool dormant = entity->IsDormant();
        if (!dormant || !g_snapshots.count(index)) {
            // The snapshot keeps the last known values while a player is dormant
            // (the Lua's target_snapshots do the same).
            TargetSnap snap;
            snap.valid = true;
            snap.alive = entity->IsAlive();
            snap.enemy = IsEnemyEntity(index);
            snap.team = entity->m_iTeamNum();
            const Vector origin = entity->m_vecOrigin();
            snap.origin = {origin.x, origin.y, origin.z};
            if (index == local || index <= (std::min)(64, Interfaces::Engine->GetMaxClients())) {
                auto* player = static_cast<CCSPlayer*>(entity);
                const Vector view = player->m_vecViewOffset();
                snap.view_z = view.z;
                snap.armor = player->m_ArmorValue();
                snap.health = player->m_iHealth();
                snap.heavy = PropRead<bool>(entity, "DT_CSPlayer", "m_bHasHeavyArmor", false);
            }
            g_snapshots[index] = snap;
        }
        if (g_snapshots[index].alive) g_damage_targets.push_back(index);
    }
    // Drop snapshots of players that left.
    for (auto it = g_snapshots.begin(); it != g_snapshots.end();) {
        it = (std::find(seen.begin(), seen.end(), it->first) == seen.end()) ? g_snapshots.erase(it) : ++it;
    }
}

const TargetSnap* Snap(int index) {
    if (auto it = g_snapshots.find(index); it != g_snapshots.end()) return &it->second;
    return nullptr;
}

// The Lua's blast_damage(): gaussian falloff, then the six weighted visibility
// samples, then the armor split. Returns the floor()ed damage to health and the
// point-blank damage before the visibility check.
std::pair<int, int> BlastDamage(int target, int local_player, const Vec3& origin,
                                const Vec3& eye, float damage, float radius, float scale) {
    const TargetSnap* snap = Snap(target);
    if (!snap || !snap->valid) return {0, 0};
    const int armor = snap->armor;
    const bool heavy = snap->heavy;
    const auto after_armor = [&](float d, float s) {
        return Detail::AfterArmor(d, s, heavy, armor);
    };
    Vec3 target_eye;
    if (target == local_player) {
        target_eye = eye;
    } else {
        target_eye = {snap->origin.x, snap->origin.y, snap->origin.z + snap->view_z};
    }
    const Vec3 from{origin.x, origin.y, origin.z + 1.f};
    const float point_blank = after_armor(damage, scale);
    const float dx = target_eye.x - from.x;
    const float dy = target_eye.y - from.y;
    const float dz = target_eye.z - from.z;
    const float falloff = Detail::BlastFalloff(dx * dx + dy * dy + dz * dz, radius);
    if (after_armor(damage * falloff, scale) < 0.5f) return {0, static_cast<int>(point_blank)};
    const float rx = Detail::kPlayerHalfWidth;
    const float visible =
        0.40f * Exposed(from, target_eye) +
        0.20f * Exposed(from, {snap->origin.x, snap->origin.y, snap->origin.z + Detail::kPlayerHeight}) +
        0.20f * Exposed(from, {snap->origin.x, snap->origin.y, snap->origin.z}) +
        0.10f * Exposed(from, {snap->origin.x - rx, snap->origin.y, snap->origin.z + 36.f}) +
        0.10f * Exposed(from, {snap->origin.x + rx, snap->origin.y, snap->origin.z + 36.f});
    if (visible <= 0.f) return {0, static_cast<int>(point_blank)};
    const int final = (std::max)(0, static_cast<int>(after_armor(damage * falloff * visible, scale) + 0.5f));
    return {final, static_cast<int>(point_blank)};
}

struct DamageEntry {
    int index{};
    int damage{};
    bool local_hit{};
    bool enemy{};
};

// The Lua's collect_damage(): every live target, the friendly-fire scale, the
// entries sorted local-first then by damage.
void CollectDamage(const Vec3& origin, const Vec3& eye, int local_player, int thrower,
                   float self_scale, float team_scale, float raw_damage, float radius,
                   std::vector<DamageEntry>& entries, int& local_damage, int& max_damage) {
    entries.clear();
    local_damage = 0;
    max_damage = 0;
    const TargetSnap* thrower_snap = thrower > 0 ? Snap(thrower) : nullptr;
    const int thrower_team = thrower_snap ? thrower_snap->team : -1;
    for (int target : g_damage_targets) {
        const TargetSnap* snap = Snap(target);
        if (!snap || !snap->alive) continue;
        const bool same_team = thrower_team > 0 && thrower_team == snap->team;
        const float scale = target == thrower
                                ? self_scale
                                : (same_team ? team_scale : 1.f);
        if (scale <= 0.f) continue;
        const auto [damage, maximum] =
            BlastDamage(target, local_player, origin, eye, raw_damage, radius, scale);
        max_damage = target == local_player ? maximum : (std::max)(max_damage, maximum);
        if (damage <= 0) continue;
        DamageEntry entry;
        entry.index = target;
        entry.damage = damage;
        entry.local_hit = target == local_player;
        entry.enemy = target != local_player && snap->enemy;
        entries.push_back(entry);
        if (entry.local_hit) local_damage = damage;
    }
    std::stable_sort(entries.begin(), entries.end(), [](const DamageEntry& a, const DamageEntry& b) {
        if (a.local_hit != b.local_hit) return a.local_hit;
        return a.damage > b.damage;
    });
    if (entries.size() > 16) entries.resize(16);
}

// ---------------------------------------------------------------------------
// Warning state (the Lua's tracked / fire_states / retired maps)
// ---------------------------------------------------------------------------

struct LabelRow {
    int key{};      // -1 = distance, otherwise the player index
    float alpha{};
    float offset{};
    int order{};
    char text[24]{};
};

struct WarningState {
    int kind{};
    bool own{};
    // Simulation
    float first_seen{};
    float initial_age{};
    float simulated_at{-1.f};
    float sample_time{-1.f};
    float path_start{};
    Vec3 end{};
    Vec3 current{};
    float end_time{};
    // HE damage
    float damage_time{-1.f};
    int damage_revision{};
    int local_damage{};
    int max_damage{};
    std::vector<DamageEntry> damage_entries;
    bool enemy_danger{};
    // Molotov
    float danger_time{-1.f};
    // Path (time relative to path_start)
    std::array<float, 1024 * 4> path{};
    int path_count{};
    std::array<float, 128 * 4> collisions{};
    int collision_count{};
    // Warning timeline (realtime == curtime here)
    float alpha{};
    float scale{1.f};
    float risk{};
    float visible_since{};
    bool has_visible_since{};
    float fade_started{-1.f};
    float fade_alpha{};
    float warning_visibility{};
    float transition_from{};
    float transition_target{};
    float transition_started{};
    bool has_transition{};
    float last_frame{};
    float last_seen{};
    // Last warning (for the fade-out)
    bool has_warning{};
    bool finished{};
    float finished_at{};
    // Fire cells (molotov pool)
    std::array<Vec3, 64> cells{};
    int cell_count{};
    float cells_time{-1.f};
    bool has_world{};
    float world_x{}, world_y{}, world_z{};
    // Damage label rows
    std::array<LabelRow, 8> rows{};
    int row_count{};
    float labels_frame{};
};

std::unordered_map<int, WarningState> g_grenades;
std::unordered_map<int, WarningState> g_fires;
float g_fallback_scan_at{};
std::unordered_map<int, std::pair<int, int>> g_fallback_projectiles; // index -> {kind, model}

struct Warning {
    WarningState* state{};
    float x{}, y{}, z{};
    float remaining{};
    int kind{};
    bool own{};
    bool active{true};
    float distance{};
    float fire_distance{};
    bool touching{};
    int local_damage{};
    int max_damage{};
    const std::vector<DamageEntry>* entries{};
    bool enemy_danger{};
};

void Finish(WarningState& state, float now) {
    if (state.finished) return;
    state.finished = true;
    state.finished_at = now;
    state.path_count = 0;
    state.collision_count = 0;
}

int ModelKindFromName(const char* name) {
    if (!name) return 0;
    if (std::strstr(name, "frag") || std::strstr(name, "hegrenade")) return KindHe;
    if (std::strstr(name, "flash")) return KindFlash;
    if (std::strstr(name, "molotov") || std::strstr(name, "incendiary")) return KindMolotov;
    return 0;
}

} // namespace

// ---------------------------------------------------------------------------
// Public plumbing
// ---------------------------------------------------------------------------

void Publish(const Settings& s) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_snapshot = s;
}

Settings Snapshot() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_snapshot;
}

void Reset() {
    g_grenades.clear();
    g_fires.clear();
    g_fallback_projectiles.clear();
    g_snapshots.clear();
    g_damage_targets.clear();
    g_targets_updated = 0.f;
    g_targets_count = 0;
    g_targets_revision = 0;
    g_fallback_scan_at = 0.f;
    g_trace_ready = false;
    g_trace_enabled = false;
    g_trace_next_check = 0.f;
    g_cvars_done = false;
    g_gravity = g_maxvelocity = g_inferno_lifetime = nullptr;
    g_inferno_range = g_friendlyfire = g_self_scale = g_team_scale = nullptr;
}

// CS:GO IGameEvent ABI (same shape as the on-shot chams): detonations finish the
// matching warning early so it fades out at the blast.
struct Event {
    const char* Name() { return MEM::GetVFunc<const char*(__thiscall*)(void*)>(this, 1)(this); }
    int Int(const char* key) {
        return MEM::GetVFunc<int(__thiscall*)(void*, const char*, int)>(this, 6)(this, key, 0);
    }
    float Float(const char* key) {
        return MEM::GetVFunc<float(__thiscall*)(void*, const char*, float)>(this, 7)(this, key, 0.f);
    }
};

namespace {
bool g_registered{};

void OnDetonate(Event* event) {
    if (!event) return;
    const char* name = event->Name();
    // The Lua's on_detonate: an entityid over 2047 is an ehandle (mask 0x7FF),
    // a smaller one is already the entity index. Fires are never finished by
    // events (the Lua's tracked map holds only the projectiles).
    int index = event->Int("entityid");
    if (index > 2047) index &= 0x7FF;
    if (index > 0) {
        if (auto it = g_grenades.find(index); it != g_grenades.end()) {
            Finish(it->second, Now());
            return;
        }
    }
    const float x = event->Float("x");
    const float y = event->Float("y");
    const float z = event->Float("z");
    if (!(std::isfinite(x) && std::isfinite(y) && std::isfinite(z))) return;
    const int kind = std::strcmp(name, "hegrenade_detonate") == 0 ? KindHe : KindMolotov;
    for (auto& entry : g_grenades) {
        WarningState& state = entry.second;
        if (state.kind != kind) continue;
        auto distance2 = [&](const Vec3& from) {
            const float dx = from.x - x, dy = from.y - y, dz = from.z - z;
            return dx * dx + dy * dy + dz * dz;
        };
        float best = distance2(state.current);
        if (state.end.IsValid()) best = (std::min)(best, distance2(state.end));
        if (best < 128.f * 128.f) Finish(state, Now());
    }
}

class Listener {
public:
    virtual ~Listener() = default;
    virtual void FireGameEvent(Event* event) {
        __try {
            OnDetonate(event);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    virtual int GetEventDebugID() { return 44; }
} g_listener;
} // namespace

void Initialize() {
    if (g_registered || !Interfaces::GameEvent) return;
    const auto add = MEM::GetVFunc<bool(__thiscall*)(void*, void*, const char*, bool)>(Interfaces::GameEvent, 3);
    bool ok = true;
    for (const char* event_name : {"hegrenade_detonate", "molotov_detonate", "inferno_startburn"})
        ok = add(Interfaces::GameEvent, &g_listener, event_name, false) && ok;
    if (!ok) {
        MEM::GetVFunc<void(__thiscall*)(void*, void*)>(Interfaces::GameEvent, 5)(Interfaces::GameEvent, &g_listener);
        LOG_WARN("grenade warning: event listener unavailable");
        return;
    }
    g_registered = true;
    LOG_INFO("grenade warning: detonation listeners registered");
}

void Shutdown() {
    if (g_registered && Interfaces::GameEvent)
        MEM::GetVFunc<void(__thiscall*)(void*, void*)>(Interfaces::GameEvent, 5)(Interfaces::GameEvent, &g_listener);
    g_registered = false;
    Reset();
}

namespace {

// ---------------------------------------------------------------------------
// Projectiles (the Lua's add_projectiles)
// ---------------------------------------------------------------------------

int ProjectileKindFromClass(const char* cls) {
    if (!cls) return 0;
    if (std::strcmp(cls, "CHEGrenadeProjectile") == 0) return KindHe;
    if (std::strcmp(cls, "CFlashbangProjectile") == 0) return KindFlash;
    if (std::strcmp(cls, "CMolotovProjectile") == 0) return KindMolotov;
    if (std::strcmp(cls, "CBaseCSGrenadeProjectile") == 0) return 0;
    return -1;
}

// The Lua lowercases the class name before looking for "projectile".
bool IsProjectileClass(const char* cls) {
    if (!cls) return false;
    const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c; };
    std::string name;
    name.reserve(32);
    for (const char* p = cls; *p; ++p) name.push_back(lower(*p));
    return std::strstr(name.c_str(), "projectile") != nullptr;
}

// The Lua's is_threat(): every flash is a threat except your own; he and molotov
// always warn (they can hurt you through walls or off your own throw).
bool IsThreat(int kind, bool own) { return kind == KindFlash ? !own : true; }

void AddProjectiles(const Settings& s, float now, float dt, const Vec3& eye, std::vector<Warning>& warnings) {
    (void)s; // the prediction toggle only gates the drawing in Contribute
    const float self_scale = CvarFloat(g_self_scale, 1.f);
    const bool friendly_fire = CvarFloat(g_friendlyfire, 0.f) != 0.f;
    const float team_scale = friendly_fire ? CvarFloat(g_team_scale, 0.25f) : 0.f;
    const int local = Interfaces::Engine->GetLocalPlayer();
    const int highest = (std::min)(Interfaces::EntityList->GetHighestEntityIndex(), 2048);

    std::unordered_map<int, int> candidates; // index -> forced kind (0 = decide by model)
    for (int i = 1; i <= highest; ++i) {
        auto* entity = Interfaces::EntityList->GetClientEntity(i);
        if (!entity) continue;
        const int kind = ProjectileKindFromClass(ClassOf(entity));
        if (kind >= 0 && !candidates.count(i)) candidates[i] = kind;
    }

    // Fallback scan (every 0.25 s): any "*projectile" class decided by the model
    // name, the cached entries revalidated by model index.
    if (now >= g_fallback_scan_at) {
        g_fallback_scan_at = now + 0.25f;
        for (int i = 1; i <= highest; ++i) {
            if (candidates.count(i)) continue;
            auto* entity = Interfaces::EntityList->GetClientEntity(i);
            if (!entity) continue;
            const char* cls = ClassOf(entity);
            if (!IsProjectileClass(cls)) continue;
            const int model = PropRead<int>(entity, "DT_BaseEntity", "m_nModelIndex", 0);
            const char* name = static_cast<CBaseEntity*>(entity)->GetModelName();
            const std::string lower = name ? name : "";
            const int kind = ModelKindFromName(lower.c_str());
            if (kind) g_fallback_projectiles[i] = {kind, model};
        }
        for (auto it = g_fallback_projectiles.begin(); it != g_fallback_projectiles.end();) {
            auto* entity = Interfaces::EntityList->GetClientEntity(it->first);
            if (candidates.count(it->first) || !entity ||
                PropRead<int>(entity, "DT_BaseEntity", "m_nModelIndex", -1) != it->second.second) {
                it = g_fallback_projectiles.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (const auto& [index, cached] : g_fallback_projectiles)
        if (!candidates.count(index)) candidates[index] = cached.first;

    std::unordered_set<int> seen;
    for (const auto& [index, forced] : candidates) {
        auto* entity = Interfaces::EntityList->GetClientEntity(index);
        if (!entity) continue;
        const char* table = TableName(entity);
        if (!table) continue;
        const Vector origin = PropRead<Vector>(entity, table, "m_vecOrigin");
        if (!origin.IsValid()) continue;
        const bool exploded = PropRead<int>(entity, table, "m_nExplodeEffectTickBegin", 0) > 0;
        const int thrower_handle = PropRead<int>(entity, table, "m_hThrower", 0);
        const int thrower = thrower_handle > 0 ? thrower_handle & 0x7FF : -1;
        const bool own = thrower == local;
        const int radius_int = PropRead<int>(entity, table, "m_DmgRadius", 0);
        float radius = static_cast<float>(radius_int);
        int kind = forced;
        if (kind == 0) {
            // The Lua's grenade_kind(): model name first, then the damage radius.
            const char* name = static_cast<CBaseEntity*>(entity)->GetModelName();
            const std::string lower = name ? name : "";
            kind = ModelKindFromName(lower.c_str());
            if (!kind) kind = radius > 0.f && radius <= Detail::kHeMinRadius ? KindFlash : KindHe;
        }
        auto tracked = g_grenades.find(index);
        if (exploded || (tracked != g_grenades.end() && tracked->second.end_time > 0.f &&
                         now >= tracked->second.end_time)) {
            if (tracked != g_grenades.end()) Finish(tracked->second, now);
            continue;
        }
        if (seen.count(index)) continue;
        if (!IsThreat(kind, own)) continue;
        seen.insert(index);

        WarningState& g = g_grenades[index];
        const bool fresh = g.kind == 0;
        if (fresh) {
            g = WarningState{};
            g.kind = kind;
            g.first_seen = now;
            g.initial_age = dt; // the Lua adds half the latency; ~one tick here
        }
        g.own = own;
        g.current = {origin.x, origin.y, origin.z};

        const float simtime = PropRead<float>(entity, table, "m_flSimulationTime", now);
        const float spawned = PropRead<float>(entity, table, "m_flSpawnTime", 0.f);
        const Vector velocity = PropRead<Vector>(entity, table, "m_vecVelocity");
        const float age_base = g.initial_age + (now - g.first_seen);
        float age = age_base;
        if (spawned > 0.f && spawned <= simtime && simtime - spawned < 10.f) {
            age = simtime - spawned;
        } else if (PropRead<int>(entity, table, "m_nBounces", 0) == 0) {
            const Vector initial = PropRead<Vector>(entity, table, "m_vInitialVelocity");
            const float gravity = CvarFloat(g_gravity, 800.f) * Detail::kGravityScale;
            if (gravity > 0.f &&
                std::abs(initial.x - velocity.x) + std::abs(initial.y - velocity.y) < 12.f) {
                const float ballistic = (initial.z - velocity.z) / gravity;
                if (ballistic >= 0.f && ballistic < 4.f) age = ballistic;
            }
        }

        const bool outdated = g.simulated_at < 0.f || g.sample_time != simtime;
        if (outdated) {
            EnsureCvars();
            const int age_ticks = (std::max)(0, static_cast<int>(std::floor(age / dt + 0.5f)));
            Detail::SimConfig cfg;
            cfg.pos = g.current;
            cfg.vel = {velocity.x, velocity.y, velocity.z};
            cfg.dt = dt;
            cfg.gravity = CvarFloat(g_gravity, 800.f) * Detail::kGravityScale;
            cfg.speed_limit = CvarFloat(g_maxvelocity, 3000.f);
            cfg.kind = g.kind;
            cfg.age_ticks = age_ticks;

            auto sweep = [](const Vec3& pos, const Vec3& delta, SweepHit& out,
                            Detail::IgnoreList& ignored) { return Sweep(pos, delta, out, ignored); };
            auto burn = [](const Vec3& pos) { return BurnPoint(pos); };
            auto classify = [](int entindex) { return ClassifyBreakable(entindex); };
            const auto sim = Detail::Simulate(cfg, sweep, burn, classify);

            g.simulated_at = now;
            g.sample_time = simtime;
            g.path_start = now - (std::max)(0.f, now - simtime);
            g.end = sim.end;
            g.path_count = (std::min)(sim.path_count, 1024);
            for (int i = 0; i < g.path_count; ++i)
                std::memcpy(&g.path[i * 4], sim.path[i], sizeof(float) * 4);
            g.collision_count = (std::min)(sim.collision_count, 128);
            for (int i = 0; i < g.collision_count; ++i)
                std::memcpy(&g.collisions[i * 4], sim.collisions[i], sizeof(float) * 4);
            // The Lua keeps `end_time` (game time): for an HE it can only move
            // earlier, a molotov rest position only updates once it is imminent.
            const float predicted_end = g.path_start + sim.end_time;
            if (g.end_time > 0.f && (g.kind == KindHe || g.end_time - now <= 0.12f))
                g.end_time = (std::min)(g.end_time, predicted_end);
            else
                g.end_time = predicted_end;
        }

        // HE: the damage labels, refreshed at the damage interval or when the
        // target set changed.
        if (g.end.IsValid() && g.kind == KindHe &&
            (now - g.damage_time >= Detail::kDamageInterval || g.damage_revision != g_targets_revision)) {
            const float raw_damage = PropRead<float>(entity, table, "m_flDamage", 0.f);
            g.damage_time = now;
            g.damage_revision = g_targets_revision;
            CollectDamage(g.end, eye, local, thrower, self_scale, team_scale,
                          raw_damage >= 1.f ? raw_damage : Detail::kHeDamage,
                          radius >= Detail::kHeMinRadius ? radius : Detail::kHeRadius,
                          g.damage_entries, g.local_damage, g.max_damage);
            g.enemy_danger = false;
            for (const auto& entry : g.damage_entries)
                if (entry.enemy && entry.damage > 0) {
                    g.enemy_danger = true;
                    break;
                }
        }
        // Molotov: enemy_danger when an enemy stands in the pool of fire.
        if (g.end.IsValid() && g.kind == KindMolotov &&
            now - g.danger_time >= Detail::kDamageInterval) {
            g.danger_time = now;
            g.enemy_danger = false;
            const int owner_team = thrower > 0 ? Snap(thrower) ? Snap(thrower)->team : -1 : -1;
            for (int enemy : g_damage_targets) {
                const TargetSnap* snap = Snap(enemy);
                if (!snap || !snap->enemy) continue;
                if (!g.enemy_danger) {
                    const bool can_hurt = thrower <= 0 || enemy == thrower || owner_team != snap->team ||
                                          friendly_fire;
                    if (!can_hurt) continue;
                    const float dx = snap->origin.x - g.end.x;
                    const float dy = snap->origin.y - g.end.y;
                    const float dz = snap->origin.z - g.end.z;
                    const float horizontal2 = dx * dx + dy * dy;
                    if (horizontal2 > 0.f &&
                        horizontal2 + dz * dz < (3.f * 39.37f) * (3.f * 39.37f) &&
                        std::abs(dz) < 70.f &&
                        Exposed({g.end.x, g.end.y, g.end.z + 4.f},
                                {snap->origin.x, snap->origin.y, snap->origin.z + 36.f}) > 0)
                        g.enemy_danger = true;
                }
                if (g.enemy_danger) break;
            }
        }

        g.last_seen = now;
        g.has_warning = true;
        Warning w;
        w.state = &g;
        w.x = g.end.x;
        w.y = g.end.y;
        w.z = g.end.z;
        w.remaining = (std::max)(0.f, g.end_time - now);
        w.kind = g.kind;
        w.own = own;
        w.active = true;
        w.local_damage = g.local_damage;
        w.max_damage = g.max_damage;
        w.entries = &g.damage_entries;
        w.enemy_danger = g.enemy_danger;
        warnings.push_back(w);
    }

    // Finished grenades fade out for FADE_TIME, then the state is dropped.
    for (auto it = g_grenades.begin(); it != g_grenades.end();) {
        if (seen.count(it->first)) {
            ++it;
            continue;
        }
        WarningState& state = it->second;
        const float elapsed = now - (std::max)(state.finished_at, state.last_seen);
        if (state.has_warning && !state.finished && elapsed < Detail::kFadeTime) {
            Warning w;
            w.state = &state;
            w.x = state.end.x;
            w.y = state.end.y;
            w.z = state.end.z;
            w.remaining = 0.f;
            w.kind = state.kind;
            w.own = state.own;
            w.active = false;
            w.local_damage = state.local_damage;
            w.entries = &state.damage_entries;
            w.enemy_danger = state.enemy_danger;
            warnings.push_back(w);
            ++it;
        } else {
            it = g_grenades.erase(it);
        }
    }
}

// ---------------------------------------------------------------------------
// Inferno fires (the Lua's add_fires / nearest_fire_segment)
// ---------------------------------------------------------------------------

// The fire cell arrays are plain C arrays in the entity; the recv table names
// them with or without a "[0]" suffix depending on the build.
std::ptrdiff_t FirePropOffset(const char* table, const char* prop) {
    auto off = PropOffset(table, prop);
    if (off) return off;
    char suffixed[64];
    const int n = std::snprintf(suffixed, sizeof(suffixed), "%s[0]", prop);
    if (n > 0 && static_cast<size_t>(n) < sizeof(suffixed))
        off = PropOffset(table, suffixed);
    return off;
}

// The burning cells of one pool. A separate function (no non-trivial locals) so
// the per-slot SEH read stays in its own leaf.
struct FireCellRead {
    Vec3 cells[64]{};
    int used{};
};

FireCellRead ReadFireCells(const void* entity, const char* table, const Vec3& origin, int count) {
    FireCellRead result;
    const auto burning_base = FirePropOffset(table, "m_bFireIsBurning");
    const auto dx_base = FirePropOffset(table, "m_fireXDelta");
    const auto dy_base = FirePropOffset(table, "m_fireYDelta");
    const auto dz_base = FirePropOffset(table, "m_fireZDelta");
    const bool arrays_resolved = burning_base && dx_base && dy_base && dz_base;
    const auto base_addr = reinterpret_cast<std::uintptr_t>(entity);
    for (int slot = 0; slot < count && arrays_resolved; ++slot) {
        bool burning_slot = false;
        float dx = 0.f, dy = 0.f, dz = 0.f;
        __try {
            if (*reinterpret_cast<const bool*>(base_addr + burning_base + slot * 4)) {
                burning_slot = true;
                dx = *reinterpret_cast<const float*>(base_addr + dx_base + slot * 4);
                dy = *reinterpret_cast<const float*>(base_addr + dy_base + slot * 4);
                dz = *reinterpret_cast<const float*>(base_addr + dz_base + slot * 4);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            burning_slot = false;
        }
        if (burning_slot && result.used < 64)
            result.cells[result.used++] = {origin.x + dx, origin.y + dy, origin.z + dz};
    }
    // The Lua's fallback when the arrays are unreadable: one cell at the pool
    // origin. With readable arrays and no burning cells yet, the pool is empty.
    if (result.used == 0 && !arrays_resolved)
        result.cells[result.used++] = {origin.x, origin.y, origin.z};
    return result;
}

void AddFires(const Settings& s, float now, float dt, const Vec3& local_origin, std::vector<Warning>& warnings) {
    const float lifetime = CvarFloat(g_inferno_lifetime, 7.f);
    const bool friendly_fire = CvarFloat(g_friendlyfire, 0.f) != 0.f;
    const int local = Interfaces::Engine->GetLocalPlayer();
    const int highest = (std::min)(Interfaces::EntityList->GetHighestEntityIndex(), 2048);
    const int tick = Interfaces::Globals ? Interfaces::Globals->tickcount : 0;

    std::unordered_set<int> seen;
    for (int i = 1; i <= highest; ++i) {
        auto* entity = Interfaces::EntityList->GetClientEntity(i);
        if (!entity) continue;
        const char* cls = ClassOf(entity);
        if (!cls || std::strcmp(cls, "CInferno") != 0) continue;
        const char* table = TableName(entity);
        if (!table) continue;
        const Vector origin = PropRead<Vector>(entity, table, "m_vecOrigin");
        if (!origin.IsValid()) continue;
        const int ignited = PropRead<int>(entity, table, "m_nFireEffectTickBegin", 0);
        if (ignited <= 0) continue;
        const int owner_handle = PropRead<int>(entity, table, "m_hOwnerEntity", 0);
        const int owner = owner_handle > 0 ? owner_handle & 0x7FF : -1;
        const bool friendly = owner != -1 && owner != local && !IsEnemyEntity(owner);
        const float burning = (std::max)(0.f, (tick - ignited) * dt);
        if ((friendly && !friendly_fire) || burning >= lifetime) continue;

        WarningState& state = g_fires[i];
        if (state.kind == 0) {
            state = WarningState{};
            state.kind = KindFire;
            state.first_seen = now;
            state.visible_since = now;
            state.has_visible_since = true;
            state.last_frame = now;
        }

        // The burning cells of the pool, rebuilt at the damage interval.
        const int raw_count = PropRead<int>(entity, table, "m_fireCount", 0);
        const bool count_resolved = PropOffset(table, "m_fireCount") != 0;
        const int count = (std::min)(64, (std::max)(0, raw_count));
        if (state.cell_count != (count_resolved ? count : 1) ||
            now - state.cells_time >= Detail::kDamageInterval) {
            const FireCellRead read = ReadFireCells(entity, table, {origin.x, origin.y, origin.z}, count);
            state.cell_count = read.used;
            for (int c = 0; c < read.used; ++c) state.cells[c] = read.cells[c];
            state.cells_time = now;
        }
        if (state.cell_count == 0) continue;
        seen.insert(i);

        // Nearest point of the pool to the local player.
        const Vec3 self{local_origin.x, local_origin.y, local_origin.z};
        Vec3 closest{};
        float best_distance = 0.f;
        bool best_found = false;
        bool touching = false;
        const int cells = state.cell_count > 0 ? state.cell_count : 1;
        for (int c = 0; c < cells; ++c) {
            const Vec3 cell = state.cells[c];
            const float dx = self.x - cell.x;
            const float dy = self.y - cell.y;
            const float dz = self.z - cell.z;
            const float horizontal = std::hypot(dx, dy);
            const float edge = (std::max)(0.f, horizontal - 60.f);
            const float distance = std::hypot(edge, dz);
            if (!best_found || distance < best_distance) {
                best_found = true;
                best_distance = distance;
                const float fraction = horizontal > 60.f ? 60.f / horizontal : 0.f;
                closest = {cell.x + dx * fraction, cell.y + dy * fraction, cell.z};
                touching = horizontal <= 60.f && std::abs(dz) < 70.f;
            }
        }
        if (!best_found) continue;

        // Smoothed warning position (the Lua's approach(...) with speed 14).
        const float frame_time = (std::min)(0.1f, (std::max)(0.f, now - state.last_frame));
        state.last_frame = now;
        if (state.has_world) {
            state.world_x = Detail::Approach(state.world_x, closest.x, 14.f, frame_time);
            state.world_y = Detail::Approach(state.world_y, closest.y, 14.f, frame_time);
            state.world_z = Detail::Approach(state.world_z, closest.z, 14.f, frame_time);
        } else {
            state.world_x = closest.x;
            state.world_y = closest.y;
            state.world_z = closest.z;
            state.has_world = true;
        }
        state.last_seen = now;
        state.has_warning = true;

        Warning w;
        w.state = &state;
        w.x = state.world_x;
        w.y = state.world_y;
        w.z = state.world_z;
        w.remaining = (std::max)(0.f, lifetime - burning);
        w.kind = KindFire;
        w.own = owner == local;
        w.active = true;
        w.fire_distance = best_distance;
        w.touching = touching;
        warnings.push_back(w);
    }

    for (auto it = g_fires.begin(); it != g_fires.end();) {
        if (seen.count(it->first)) {
            ++it;
            continue;
        }
        WarningState& state = it->second;
        const float elapsed = now - (std::max)(state.finished_at, state.last_seen);
        if (state.has_warning && !state.finished && elapsed < Detail::kFadeTime) {
            Warning w;
            w.state = &state;
            w.x = state.world_x;
            w.y = state.world_y;
            w.z = state.world_z;
            w.remaining = 0.f;
            w.kind = KindFire;
            w.active = false;
            warnings.push_back(w);
            ++it;
        } else {
            it = g_fires.erase(it);
        }
    }
    (void)s;
}

// ---------------------------------------------------------------------------
// Drawing (the Lua's draw_warnings / draw_trajectory / draw_damage_labels /
// draw_warning), contributed into the FxFrame the overlay renders.
// ---------------------------------------------------------------------------

std::uint32_t Rgba(int r, int g, int b, float alpha) {
    const int a = static_cast<int>((std::min)((std::max)(alpha, 0.f), 1.f) * 255.f + 0.5f);
    return (static_cast<std::uint32_t>(r) << 16) | (static_cast<std::uint32_t>(g) << 8) |
           static_cast<std::uint32_t>(b) | (static_cast<std::uint32_t>(a) << 24);
}

struct Screen {
    int width{};
    int height{};
    bool Project(const Vec3& world, float& nx, float& ny) const {
        Vector2D screen;
        if (!Math::world_to_screen(Vector(world.x, world.y, world.z), screen)) return false;
        nx = screen.x / width;
        ny = screen.y / height;
        return true;
    }
};

// The Lua's draw_trajectory(): the smooth_path walk, the travelling wave alpha and
// the collision markers. Point budget: the path is walked at 30 ms resolution so
// a full future path fits the shared line array.
void DrawTrajectory(VisualRuntime::FxFrame& frame, const Settings& s, Warning& w, float now) {
    if (!s.prediction || w.kind == KindFire) return;
    WarningState& g = *w.state;
    if (g.path_count < 2) return;
    const float elapsed = (std::max)(0.f, now - g.path_start);
    // The Lua's trajectory_color(): the menu colour by default, a fixed GREEN when
    // the impact threatens an enemy, a fixed YELLOW when anyone takes damage.
    int r, gc, b;
    if (w.enemy_danger) {
        r = Detail::kGreen.r;
        gc = Detail::kGreen.g;
        b = Detail::kGreen.b;
    } else if (w.local_damage > 0 || !g.damage_entries.empty()) {
        r = Detail::kYellow.r;
        gc = Detail::kYellow.g;
        b = Detail::kYellow.b;
    } else {
        r = static_cast<int>((s.prediction_miss_color >> 16) & 0xFF);
        gc = static_cast<int>((s.prediction_miss_color >> 8) & 0xFF);
        b = static_cast<int>(s.prediction_miss_color & 0xFF);
    }
    const float base_alpha = static_cast<float>((s.prediction_miss_color >> 24) & 0xFF) / 255.f;

    Screen screen;
    screen.width = ctx.screen_size.x > 1.f ? static_cast<int>(ctx.screen_size.x) : 0;
    screen.height = ctx.screen_size.y > 1.f ? static_cast<int>(ctx.screen_size.y) : 0;
    if (screen.width < 2 || screen.height < 2) return;

    struct Point {
        float nx, ny, time;
    };
    std::vector<Point> points;
    points.reserve(64);
    const float step = 0.03f; // the shared line budget keeps ~30 ms resolution
    // Walk the recorded path. The head is interpolated exactly at `elapsed`
    // (the Lua's smooth_path first point); future points are kept per 30 ms bucket.
    int prev = -1;
    int last_bucket = -1;
    for (int i = 0; i < g.path_count; ++i) {
        const float time = g.path[i * 4 + 3];
        if (time < elapsed) {
            prev = i;
            continue;
        }
        if (prev >= 0) {
            const float prev_time = g.path[prev * 4 + 3];
            const float span = time - prev_time;
            const float f = span > 1e-6f ? (elapsed - prev_time) / span : 0.f;
            const Vec3 head{
                g.path[prev * 4] + (g.path[i * 4] - g.path[prev * 4]) * f,
                g.path[prev * 4 + 1] + (g.path[i * 4 + 1] - g.path[prev * 4 + 1]) * f,
                g.path[prev * 4 + 2] + (g.path[i * 4 + 2] - g.path[prev * 4 + 2]) * f};
            float nx, ny;
            if (screen.Project(head, nx, ny)) points.push_back({nx, ny, elapsed});
            prev = -1;
        }
        const int bucket = static_cast<int>(std::floor(time / step));
        if (bucket == last_bucket) continue;
        last_bucket = bucket;
        float nx, ny;
        if (screen.Project({g.path[i * 4], g.path[i * 4 + 1], g.path[i * 4 + 2]}, nx, ny))
            points.push_back({nx, ny, time});
    }
    if (points.size() >= 2) {
        for (std::size_t i = 1; i < points.size() && frame.line_count < static_cast<int>(frame.lines.size()); ++i) {
            const Point& a = points[i - 1];
            const Point& b = points[i];
            const float dx = (b.nx - a.nx) * screen.width;
            const float dy = (b.ny - a.ny) * screen.height;
            const float length = std::hypot(dx, dy);
            if (length < 0.25f) continue;
            const int parts = a.time - elapsed < 0.08f ? (std::min)(16, (std::max)(1, static_cast<int>(std::ceil(length / 6.f)))) : 1;
            for (int part = 1; part <= parts; ++part) {
                const float t0 = static_cast<float>(part - 1) / parts;
                const float t1 = static_cast<float>(part) / parts;
                const float time = a.time + (b.time - a.time) * ((t0 + t1) * 0.5f);
                const float fade = Detail::TrajectoryFade(time, elapsed);
                const float wave = Detail::TrajectoryWave(g.path_start + time, now);
                const float alpha = base_alpha * wave * fade;
                if (alpha <= 0.005f) continue;
                frame.lines[frame.line_count++] = {
                    a.nx + (b.nx - a.nx) * t0, a.ny + (b.ny - a.ny) * t0,
                    a.nx + (b.nx - a.nx) * t1, a.ny + (b.ny - a.ny) * t1,
                    1.f, Rgba(r, gc, b, alpha)};
            }
        }
    }

    // Collision markers on the future path.
    for (int i = 0; i < g.collision_count && frame.mark_count < static_cast<int>(frame.marks.size()); ++i) {
        const float time = g.collisions[i * 4 + 3];
        if (time < elapsed) continue;
        float nx, ny;
        if (!screen.Project({g.collisions[i * 4], g.collisions[i * 4 + 1], g.collisions[i * 4 + 2]}, nx, ny))
            continue;
        const float alpha = base_alpha * Detail::TrajectoryFade(time, elapsed);
        if (alpha <= 0.005f) continue;
        frame.marks[frame.mark_count++] = {nx, ny, 3.f / screen.height, alpha,
                                           Rgba(255, 255, 255, 1.f), Rgba(r, gc, b, 1.f), 3};
    }

    // The predicted impact: green when it hits an enemy, red otherwise.
    if (w.active) {
        float nx, ny;
        if (screen.Project({w.x, w.y, w.z}, nx, ny)) {
            const int er = w.enemy_danger ? Detail::kGreen.r : Detail::kRed.r;
            const int eg = w.enemy_danger ? Detail::kGreen.g : Detail::kRed.g;
            const int eb = w.enemy_danger ? Detail::kGreen.b : Detail::kRed.b;
            frame.marks[frame.mark_count++] = {nx, ny, 5.f / screen.height, base_alpha,
                                               Rgba(255, 255, 255, 1.f), Rgba(er, eg, eb, 1.f), 3};
        }
    }
}

// The Lua's draw_damage_labels(): up to four rows with a per-row alpha/offset
// animation, centred under the predicted impact. The rows fade out while the
// warning disc is visible (text_alpha = alpha * (1 - warning_visibility));
// without the disc feature the visibility stays 0.
void DrawDamageLabels(VisualRuntime::FxFrame& frame, Warning& w, float now, bool with_disc) {
    WarningState& g = *w.state;
    if (w.kind == KindFire) return;
    Screen screen;
    screen.width = ctx.screen_size.x > 1.f ? static_cast<int>(ctx.screen_size.x) : 0;
    screen.height = ctx.screen_size.y > 1.f ? static_cast<int>(ctx.screen_size.y) : 0;
    if (screen.width < 2 || screen.height < 2) return;
    float nx, ny;
    if (!screen.Project({w.x, w.y, w.z + 4.f}, nx, ny)) return;

    struct Row {
        int key{};
        int damage{};
        bool local_hit{};
        bool enemy{};
        float distance{};
        int order{};
        const char* text{};
    };
    std::vector<Row> wanted;
    if (w.kind == KindHe) {
        if (w.entries) {
            for (const auto& entry : *w.entries) {
                if ((entry.local_hit || entry.enemy) && entry.damage > 0 && wanted.size() < 4) {
                    char buffer[24];
                    std::snprintf(buffer, sizeof(buffer), "%d dmg", entry.damage);
                    wanted.push_back({entry.index, entry.damage, entry.local_hit, entry.enemy,
                                      0.f, static_cast<int>(wanted.size()), buffer});
                }
            }
        }
    } else if (w.kind == KindMolotov) {
        const float fire_reach = CvarFloat(g_inferno_range, Detail::kInfernoMaxRange);
        const int local = Interfaces::Engine ? Interfaces::Engine->GetLocalPlayer() : -1;
        for (int target : g_damage_targets) {
            const TargetSnap* snap = Snap(target);
            if (!snap) continue;
            if (target != local && !snap->enemy) continue;
            const float dx = w.x - snap->origin.x;
            const float dy = w.y - snap->origin.y;
            const float dz = w.z - snap->origin.z;
            const float distance = std::hypot(dx, dy, dz);
            const float metres = Detail::ToMetres(distance);
            if (metres <= 0.f || metres >= 4.f) continue;
            char buffer[24];
            Detail::MetresLabel(metres, buffer, sizeof(buffer));
            // The Lua's local_hit for the molotov rows: the local player only
            // counts as a "hit" while the pool is within the fire range.
            const bool local_hit = target == local && distance <= fire_reach;
            wanted.push_back({target, 0, local_hit, snap->enemy, distance,
                              static_cast<int>(wanted.size()), buffer});
        }
        std::stable_sort(wanted.begin(), wanted.end(), [](const Row& a, const Row& b) {
            if (a.local_hit != b.local_hit) return a.local_hit;
            if (a.distance == b.distance) return a.key < b.key;
            return a.distance < b.distance;
        });
        if (wanted.size() > 4) wanted.resize(4);
    }
    if (!w.active) wanted.clear();

    const float elapsed = (std::min)(0.1f, (std::max)(0.f, now - g.labels_frame));
    g.labels_frame = now;
    std::vector<int> wanted_keys;
    for (const auto& row : wanted) wanted_keys.push_back(row.key);

    // Update the per-row animation state.
    for (int i = 0; i < g.row_count; ++i) {
        LabelRow& row = g.rows[i];
        const bool keep = std::find(wanted_keys.begin(), wanted_keys.end(), row.key) != wanted_keys.end();
        row.alpha = Detail::Approach(row.alpha, keep ? 1.f : 0.f, keep ? 16.f : 20.f, elapsed);
        if (!keep && row.alpha < 0.008f) {
            g.rows[i] = g.rows[g.row_count - 1];
            --g.row_count;
            --i;
        }
    }
    for (const auto& row : wanted) {
        int slot = -1;
        for (int i = 0; i < g.row_count; ++i)
            if (g.rows[i].key == row.key) {
                slot = i;
                break;
            }
        if (slot < 0) {
            if (g.row_count >= 8) continue;
            slot = g.row_count++;
            g.rows[slot] = {};
        }
        g.rows[slot].key = row.key;
        g.rows[slot].order = row.order;
        std::snprintf(g.rows[slot].text, sizeof(g.rows[slot].text), "%s", row.text);
    }

    // Stack the rows bottom-up with the smoothed offsets (the Lua's target_offset).
    float total = 0.f;
    for (int i = 0; i < g.row_count; ++i) total += 13.f * g.rows[i].alpha;
    float cursor = 0.f;
    for (int i = 0; i < g.row_count && frame.text_count < static_cast<int>(frame.texts.size()); ++i) {
        LabelRow& row = g.rows[i];
        const float target_offset = -6.f - total + cursor;
        row.offset = Detail::Approach(row.offset, target_offset, 18.f, elapsed);
        cursor += 13.f * row.alpha;
        const float text_alpha = row.alpha * (1.f - (with_disc ? g.warning_visibility : 0.f));
        if (text_alpha <= 0.005f) continue;
        // Colours: red for self, green for big enemy damage / close molotov, white
        // otherwise.
        int r = 255, gc = 255, b = 255;
        bool local_hit = false;
        bool big_enemy = false;
        bool close_molotov = false;
        for (const auto& want : wanted) {
            if (want.key != row.key) continue;
            local_hit = want.local_hit;
            big_enemy = want.enemy && want.damage > 50;
            close_molotov = w.kind == KindMolotov && !want.local_hit &&
                            want.distance < 3.f * 39.37f;
        }
        if (local_hit) {
            r = 255;
            gc = 58;
            b = 58;
        } else if (big_enemy || close_molotov) {
            r = Detail::kGreen.r;
            gc = Detail::kGreen.g;
            b = Detail::kGreen.b;
        }
        frame.texts[frame.text_count++] = {nx, ny + (row.offset - (1.f - row.alpha) * 5.f) / screen.height,
                                           text_alpha, Rgba(r, gc, b, 1.f), row.text};
    }
}

// The Lua's draw_warning(): the appear/fade alpha, the risk-based colour ramp,
// the pulse, the clamped edge position and the off-screen arrow.
void DrawWarning(VisualRuntime::FxFrame& frame, const Settings& s, Warning& w, const Vec3& eye,
                 float now, float dt) {
    WarningState& g = *w.state;
    const float frame_dt = (std::min)(0.1f, (std::max)(0.f, now - g.last_frame));
    g.last_frame = now;
    const bool active = w.active;
    if (!g.has_visible_since) {
        g.has_visible_since = true;
        g.visible_since = g.first_seen > 0.f ? g.first_seen : now;
    }
    if (active) {
        g.fade_started = -1.f;
        const float appearing = Detail::AlphaState::Appear(now, g.visible_since);
        g.alpha = appearing;
        g.scale = 0.72f + 0.28f * appearing;
    } else {
        if (g.fade_started < 0.f) {
            g.fade_started = now;
            g.fade_alpha = g.alpha;
        }
        const float fading = Detail::Smoothstep((now - g.fade_started) / Detail::kFadeTime);
        g.alpha = g.fade_alpha * (1.f - fading);
        g.scale = 1.f - 0.18f * fading;
    }
    if (g.alpha < 0.005f) return;

    const float fire_reach = CvarFloat(g_inferno_range, Detail::kInfernoMaxRange);
    const auto risk = Detail::WarningRisk(w.kind, w.kind == KindHe ? w.local_damage : 0.f,
                                          w.kind == KindFire ? w.fire_distance : w.distance,
                                          fire_reach, w.kind == KindFire && w.touching);
    g.risk = Detail::Approach(g.risk, risk.value, 8.f, frame_dt);
    const Detail::Rgb color =
        Detail::MixColor(risk.critical ? 1.f : g.risk,
                         {static_cast<int>((s.warning_color >> 16) & 0xFF),
                          static_cast<int>((s.warning_color >> 8) & 0xFF),
                          static_cast<int>(s.warning_color & 0xFF)});
    const auto pulse = Detail::WarningPulse(now, risk.critical, g.risk);
    const float display_alpha = g.alpha * g.warning_visibility;
    const float alpha255 = display_alpha * 255.f;
    if (alpha255 < 2.f) return;

    Screen screen;
    screen.width = ctx.screen_size.x > 1.f ? static_cast<int>(ctx.screen_size.x) : 0;
    screen.height = ctx.screen_size.y > 1.f ? static_cast<int>(ctx.screen_size.y) : 0;
    if (screen.width < 2 || screen.height < 2) return;

    const float radius_px = Detail::DiscRadius(g.risk, pulse.value, g.scale);
    const float glow_px = Detail::GlowRadius(g.risk, pulse.value, g.scale);

    // Edge placement with the 90 px margin (the Lua's screen_position).
    float nx, ny;
    bool offscreen = false;
    if (screen.Project({w.x, w.y, w.z + 12.f}, nx, ny)) {
        const auto edge = Detail::ScreenEdge(nx, ny, 90.f / screen.width, 90.f / screen.height);
        nx = edge.x;
        ny = edge.y;
        offscreen = edge.offscreen;
    } else {
        // Behind the camera: walk along the view basis like the Lua's fallback.
        QAngle angles;
        if (Interfaces::Engine) Interfaces::Engine->GetViewAngles(angles);
        Vector forward{}, right{}, up{};
        Math::angle_vectors(angles, &forward, &right, &up);
        const Vector from{eye.x, eye.y, eye.z};
        const Vector to{w.x, w.y, w.z};
        const Vector delta = to - from;
        const float right_dot = delta.Dot(right);
        const float down_dot = -delta.Dot(forward);
        const float length = std::hypot(right_dot, down_dot);
        if (!(length > 1e-4f)) {
            nx = 0.5f;
            ny = 1.f - 90.f / screen.height;
            offscreen = true;
        } else {
            const float rx = right_dot / length;
            const float dy = down_dot / length;
            const float half_w = screen.width / 2.f - 90.f;
            const float half_h = screen.height / 2.f - 90.f;
            const float scale = 1.f / std::hypot(rx / half_w, dy / half_h);
            nx = 0.5f + rx * scale / screen.width;
            ny = 0.5f + dy * scale / screen.height;
            offscreen = true;
        }
    }

    const int icon = (w.kind == KindMolotov || w.kind == KindFire) ? 2 : 1;
    if (frame.disc_count < static_cast<int>(frame.discs.size())) {
        frame.discs[frame.disc_count++] = {nx, ny, radius_px / screen.height, glow_px / screen.height,
                                           display_alpha, icon, Rgba(color.r, color.g, color.b, 1.f)};
    }

    // The "-N HP" detail and the remaining-fuse line.
    const bool show_damage = w.kind == KindHe && w.local_damage > 0;
    if (show_damage && frame.text_count < static_cast<int>(frame.texts.size())) {
        char buffer[24];
        Detail::DamageLabel(w.local_damage, buffer, sizeof(buffer));
        frame.texts[frame.text_count++] = {nx, ny + 12.f / screen.height, display_alpha,
                                           Rgba(255, 255, 255, 1.f), buffer};
    }
    if (frame.text_count < static_cast<int>(frame.texts.size())) {
        char buffer[24];
        Detail::TimeLabel(w.remaining, buffer, sizeof(buffer));
        frame.texts[frame.text_count++] = {nx, ny + (show_damage ? 24.f : 12.f) / screen.height,
                                           display_alpha, Rgba(255, 255, 255, 1.f), buffer};
    }

    // Off-screen arrow beyond the disc (the Lua's draw_arrow: 43 + 10 px).
    if (offscreen && frame.arrow_count < static_cast<int>(frame.arrows.size())) {
        const float dx = nx - 0.5f;
        const float dy = ny - 0.5f;
        const float length = (std::max)(1e-4f, std::hypot(dx * screen.width, dy * screen.height));
        const float dirx = dx * screen.width / length;
        const float diry = dy * screen.height / length;
        const float step_px = 53.f;
        frame.arrows[frame.arrow_count++] = {nx + dirx * step_px / screen.width,
                                             ny + diry * step_px / screen.height,
                                             std::atan2(diry, dirx), 10.f / screen.height,
                                             Rgba(255, 255, 255, display_alpha)};
    }
    (void)dt;
}

} // namespace

void Contribute(VisualRuntime::FxFrame& frame) {
    const auto s = Snapshot();
    static bool was_in_game{};
    static int last_tickcount{};
    // The Lua resets on level_init / disconnect; the repo has no level-init hook,
    // so the tickcount wrap (a new map restarts the tick counter) and the in-game
    // falling edge are the reset signals.
    const int tickcount = Interfaces::Globals ? Interfaces::Globals->tickcount : 0;
    if ((was_in_game && !ctx.in_game) ||
        (ctx.in_game && last_tickcount > 1000 && tickcount < last_tickcount - 1000)) {
        Reset();
        was_in_game = ctx.in_game;
        last_tickcount = tickcount;
        return;
    }
    was_in_game = ctx.in_game;
    last_tickcount = tickcount;

    if (!s.warning && !s.prediction) {
        Reset();
        return;
    }
    if (!ctx.in_game || !ctx.local || !ctx.local->IsAlive()) return;
    if (!Interfaces::Engine || !Interfaces::EntityList) return;
    EnsureCvars();

    const float now = Now();
    const float dt = TickInterval();
    const Vec3 eye{ctx.local->GetEyePosition().x, ctx.local->GetEyePosition().y,
                   ctx.local->GetEyePosition().z};
    const Vec3 local_origin{ctx.local->m_vecOrigin().x, ctx.local->m_vecOrigin().y,
                            ctx.local->m_vecOrigin().z};
    CheckTrace(now);
    UpdateDamageTargets(now);

    std::vector<Warning> warnings;
    AddProjectiles(s, now, dt, eye, warnings);
    AddFires(s, now, dt, local_origin, warnings);

    // "Grenade prediction": the trajectory and the damage/distance labels are drawn
    // for every tracked warning, like the Lua's draw_trajectory / draw_damage_labels.
    if (s.prediction) {
        for (auto& w : warnings) {
            if (w.kind != KindFire && w.active) DrawTrajectory(frame, s, w, now);
            if (w.active) DrawDamageLabels(frame, w, now, s.warning);
        }
    }
    if (!s.warning) return;

    // Distances, the nearest-first selection and the visibility transition
    // (the Lua's draw_warnings prologue).
    for (auto& w : warnings) {
        if (w.kind == KindFire) {
            w.distance = w.fire_distance;
        } else {
            const float dx = w.x - eye.x, dy = w.y - eye.y, dz = w.z - eye.z;
            w.distance = std::hypot(dx, dy, dz);
        }
    }
    std::vector<Warning*> nearby;
    for (auto& w : warnings)
        if (w.distance <= Detail::kRange) nearby.push_back(&w);
    std::stable_sort(nearby.begin(), nearby.end(), [](const Warning* a, const Warning* b) {
        return a->distance < b->distance;
    });
    std::unordered_set<WarningState*> selected;
    for (std::size_t i = 0; i < nearby.size() && i < static_cast<std::size_t>(Detail::kMaxWarnings); ++i)
        selected.insert(nearby[i]->state);

    for (auto& w : warnings) {
        WarningState& g = *w.state;
        const float target = selected.count(&g) ? 1.f : 0.f;
        if (!g.has_transition || g.transition_target != target) {
            g.transition_from = g.has_transition ? g.warning_transition : 0.f;
            g.transition_target = target;
            g.transition_started = now;
            g.has_transition = true;
        }
        g.warning_transition = g.transition_from +
                               (g.transition_target - g.transition_from) *
                                   Detail::EaseOutCubic((std::min)(1.f, (now - g.transition_started) / 0.22f));
        g.warning_visibility =
            g.warning_transition * Detail::Smoothstep((Detail::kRange - w.distance) / 24.f);
    }
    // The discs draw far-to-near so the nearest one is on top.
    for (auto it = nearby.rbegin(); it != nearby.rend(); ++it) {
        if ((*it)->state->warning_visibility <= 0.f) continue;
        DrawWarning(frame, s, **it, eye, now, dt);
    }
}

} // namespace GrenadeWarning
