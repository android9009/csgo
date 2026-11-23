#include "skin_changer.hpp"
#include "skin_state.hpp"
#include "equipment_changer.hpp"
#include "equipment_state.hpp"
#include "refresh_gate.hpp"
#include "../../sdk/interfaces/interfaces.hpp"
#include "../../sdk/netvars.hpp"
#include "../../utils/offsets/offsets.hpp"
#include "../../utils/memory.hpp"
#include "../../utils/log.hpp"
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>

namespace SkinChanger {
namespace {
struct Netvars {
    std::ptrdiff_t owner_low{}, owner_high{}, weapons{}, team{}, def{}, item_high{}, account{}, quality{}, paint{}, seed{}, wear{}, stat{}, name{};
    std::ptrdiff_t active{}, model{}, viewmodel{}, viewweapon{};
};
struct Original {
    void* entity{}; int definition{};
    int item_high{}, account{}, quality{}, paint{}, seed{}, stat{};
    float wear{}; char name[32]{};
    std::uint64_t revision{};
};
std::map<std::uint32_t, Original> originals;
// Selection revisions survive entity/map resets. A full update recreates entities
// and must not itself look like another user edit.
SkinRefresh::Gate selection_changes;
bool logged_missing{};
DWORD last_update{};
bool update_pending{};
bool full_update_in_progress{};
template <typename T> T& At(void* p, std::ptrdiff_t offset) { return *reinterpret_cast<T*>(reinterpret_cast<std::uintptr_t>(p) + offset); }
Netvars Resolve() {
    auto& n = PropManager::Get(); Netvars v{};
    v.weapons = n.GetOffset("DT_BaseCombatCharacter", "m_hMyWeapons");
    v.team = n.GetOffset("DT_BaseEntity", "m_iTeamNum");
    const auto table = "DT_BaseAttributableItem";
    v.owner_low = n.GetOffset(table,"m_OriginalOwnerXuidLow");
    v.owner_high = n.GetOffset(table,"m_OriginalOwnerXuidHigh");
    v.def = n.GetOffset(table,"m_iItemDefinitionIndex");
    v.item_high = n.GetOffset(table,"m_iItemIDHigh");
    v.account = n.GetOffset(table,"m_iAccountID");
    v.quality = n.GetOffset(table,"m_iEntityQuality");
    v.paint = n.GetOffset(table,"m_nFallbackPaintKit");
    v.seed = n.GetOffset(table,"m_nFallbackSeed");
    v.wear = n.GetOffset(table,"m_flFallbackWear");
    v.stat = n.GetOffset(table,"m_nFallbackStatTrak");
    v.name = n.GetOffset(table,"m_szCustomName");
    v.active = n.GetOffset("DT_BaseCombatCharacter","m_hActiveWeapon");
    if (!v.active) v.active = offsets::netvars::m_hActiveWeapon;
    v.model = n.GetOffset("DT_BaseEntity","m_nModelIndex");
    if (!v.model) v.model = offsets::netvars::m_nModelIndex;
    v.viewmodel = n.GetOffset("DT_BasePlayer","m_hViewModel[0]");
    v.viewweapon = n.GetOffset("DT_BaseViewModel","m_hWeapon");
    return v;
}
bool Valid(const Netvars& n) {
    for (const auto offset : {n.weapons,n.team,n.def,n.item_high,n.account,n.quality,n.paint,n.seed,n.wear,n.stat,n.name})
        if (offset <= 0 || offset > 0x10000) return false;
    return true;
}
bool LocalAccount(std::uint32_t& account,std::uint64_t& owner) {
    // player_info_t's SteamID64 begins at offset 8 in the Source 1 interface.
    alignas(8) unsigned char info[512]{};
    using Fn = bool (__thiscall*)(void*, int, void*);
    auto* engine = Interfaces::Engine;
    auto fn = reinterpret_cast<Fn>((*reinterpret_cast<void***>(engine))[8]);
    if (!fn || !fn(engine, engine->GetLocalPlayer(), info)) return false;
    std::memcpy(&owner, info + 8, sizeof(owner));
    account=static_cast<std::uint32_t>(owner);
    return account != 0;
}
void Save(void* e, int def, const Netvars& n, Original& o) {
    o.entity = e; o.definition = def;
    o.item_high=At<int>(e,n.item_high); o.account=At<int>(e,n.account); o.quality=At<int>(e,n.quality);
    o.paint=At<int>(e,n.paint); o.seed=At<int>(e,n.seed); o.wear=At<float>(e,n.wear); o.stat=At<int>(e,n.stat);
    std::memcpy(o.name, &At<char>(e,n.name), sizeof(o.name));
}
void Restore(void* e, const Netvars& n, const Original& o) {
    At<int>(e,n.item_high)=o.item_high; At<int>(e,n.account)=o.account; At<int>(e,n.quality)=o.quality;
    At<int>(e,n.paint)=o.paint; At<int>(e,n.seed)=o.seed; At<float>(e,n.wear)=o.wear; At<int>(e,n.stat)=o.stat;
    std::memcpy(&At<char>(e,n.name), o.name, sizeof(o.name));
}
void Apply(void* e, const Netvars& n, const Original& original, const SkinState::Choice& c, std::uint32_t account) {
    At<int>(e,n.item_high) = -1; At<int>(e,n.account) = static_cast<int>(account);
    At<int>(e,n.paint) = c.paint_kit; At<int>(e,n.seed) = c.seed;
    At<float>(e,n.wear) = (std::max)(0.000001f, c.wear);
    At<int>(e,n.stat) = c.stattrak == -2 ? original.stat : c.stattrak;
    // Unique, StatTrak, or tournament quality. No mutation of live inventory.
    At<int>(e,n.quality) = c.souvenir ? 12 : c.stattrak >= 0 ? 9 : c.stattrak == -1 && original.quality == 9 ? 4 : original.quality;
    std::memcpy(&At<char>(e,n.name), c.name.data(), c.name.size());
}
void RequestRefresh() { update_pending = true; }
// Custom-material cache invalidation - the legacy reference's force_update_skin.
// A full update rebuilds every weapon from server data: the ORIGINAL paint kit,
// and the engine marks the custom materials as initialized for it. Apply() then
// re-writes the fake kit into the netvars, but the item system is never told
// about that (raw memory writes trigger no OnDataChanged), so the weapon would
// render unscaled until an unrelated event (weapon pickup, another update)
// happens to re-run the engine's material pass - the flaky "skin only shows up
// when I pick the weapon up" behaviour and the mismatched-material console
// errors around weapon switches. Clearing the cached material lists and the
// "initialized" latch makes the engine rebuild them from the fake kit.
struct MaterialCache {
    std::ptrdiff_t item{};
    bool resolved{};
    bool valid{};
    // CWeaponCSBase / C_EconItemView layout of the pinned legacy client (the
    // same offsets the reference implementation writes).
    static constexpr std::ptrdiff_t Latch = 0x32DD;          // m_bCustomMaterialInitialized
    static constexpr std::ptrdiff_t WeaponMaterials = 0x9DC; // m_CustomMaterials
    static constexpr std::ptrdiff_t ItemMaterials = 0x14;    // m_Item.m_CustomMaterials
    static constexpr std::ptrdiff_t ItemProcessors = 0x220;  // m_Item.m_VisualsDataProcessors
};
MaterialCache material_cache{};
bool material_invalidation_off{};
struct UtlVec { std::uint32_t data; std::int32_t alloc; std::int32_t count; };
void ClearMaterialVec(UtlVec* v) {
    if (!v || v->count < 0 || v->count > 4096 || v->alloc < 0 || v->alloc > 65536) return;
    auto** entries = reinterpret_cast<void**>(v->data);
    for (int i = 0; i < v->count; ++i) entries[i] = nullptr;
    // Pointers are dropped without unref: a few leaked processor objects are
    // cheaper than calling into arbitrary engine vtables.
    v->count = 0;
}
void InvalidateCustomMaterials(void* weapon, const Netvars& n) {
    if (!weapon || material_invalidation_off) return;
    if (!material_cache.resolved) {
        material_cache.resolved = true;
        material_cache.item = PropManager::Get().GetOffset("DT_BaseAttributableItem", "m_Item");
        material_cache.valid = material_cache.item > 0x40 && material_cache.item < 0x10000;
        if (!material_cache.valid) {
            material_invalidation_off = true;
            LOG_WARN("skins: m_Item netvar missing; custom-material invalidation disabled");
        }
    }
    if (!material_cache.valid) return;
    __try {
        // Class guard: these offsets are CWeaponCSBase layout, never write them
        // into a handle that does not resolve to a weapon class.
        auto* net = static_cast<unsigned char*>(weapon) + 8;
        auto* cls = reinterpret_cast<ClientClass*(__thiscall*)(void*)>(MEM::GetVFunc<void*>(net, 2))(net);
        if (!cls || !cls->m_pNetworkName || std::strncmp(cls->m_pNetworkName, "CWeapon", 7) != 0) return;
        const auto base = reinterpret_cast<std::uintptr_t>(weapon);
        // Latch: "materials initialized for the current kit". True while no kit
        // is set (nothing to build); false forces a rebuild from the kit that
        // Apply() wrote.
        *reinterpret_cast<bool*>(base + MaterialCache::Latch) = At<int>(weapon, n.paint) <= 0;
        ClearMaterialVec(reinterpret_cast<UtlVec*>(base + MaterialCache::WeaponMaterials));
        auto* item = static_cast<unsigned char*>(weapon) + material_cache.item;
        ClearMaterialVec(reinterpret_cast<UtlVec*>(item + MaterialCache::ItemMaterials));
        ClearMaterialVec(reinterpret_cast<UtlVec*>(item + MaterialCache::ItemProcessors));
        // Re-run the networkable's update callbacks, exactly as the reference's
        // force_update_skin does (c_networkable: slot 7 = post_update(0),
        // slot 5 = data_changed(0)), so the entity re-syncs after the material
        // cache was dropped.
        using NotifyFn = void (__thiscall*)(void*, int);
        if (auto post = MEM::GetVFunc<NotifyFn>(net, 7)) post(net, 0);
        if (auto data = MEM::GetVFunc<NotifyFn>(net, 5)) data(net, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        material_invalidation_off = true;
        LOG_WARN("skins: custom-material invalidation raised an exception; disabled");
    }
}
// Full-update state machine (the legacy reference's dance):
//   request  : clientstate delta ticks := -1 (none in flight, 1s throttle)
//   in flight: delta ticks stay -1 until the server's full data is processed
//              and the entities are rebuilt
//   complete : delta ticks go positive again; invalidate the custom-material
//              cache so the engine re-derives it from the netvars Apply wrote
DWORD full_update_started{};
void RunRefresh(const Netvars& n, void* local) {
    const auto delta = offsets::Get("clientstate_delta_ticks");
    if (!Interfaces::ClientState || !delta || delta > 0x2000) return;
    int ticks = 0;
    __try { ticks = At<int>(Interfaces::ClientState, static_cast<std::ptrdiff_t>(delta)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    if (full_update_in_progress) {
        // The full data never arrives (round trip can stretch; never stall the
        // pipeline forever either).
        if (ticks > 0 || GetTickCount() - full_update_started > 3000) {
            full_update_in_progress = false;
            if (ticks <= 0) { LOG_WARN("skins: full update timed out; continuing"); return; }
            LOG_INFO("skins: full update complete; invalidating custom materials");
            // Re-read the current weapon list: the full update has just rebuilt
            // every weapon's state, so invalidate the material cache on all of
            // them (not just the selected ones) from the freshest handles.
            if (local && n.weapons > 0 && n.weapons < 0x10000)
                for (int i = 0; i < 64; ++i) {
                    std::uint32_t h = 0;
                    __try { h = At<std::uint32_t>(local, n.weapons + static_cast<std::ptrdiff_t>(i) * 4); }
                    __except (EXCEPTION_EXECUTE_HANDLER) { h = 0u; }
                    if (h == 0xffffffffu || h == 0u) continue;
                    InvalidateCustomMaterials(Interfaces::EntityList->GetClientEntityFromHandle(h), n);
                }
        }
        return;
    }
    if (!update_pending) return;
    const auto now = GetTickCount();
    if (last_update && now - last_update < 1000) return;
    __try { At<int>(Interfaces::ClientState, static_cast<std::ptrdiff_t>(delta)) = -1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    last_update = now; full_update_started = now;
    update_pending = false; full_update_in_progress = true;
    LOG_INFO("skins: requested client full update");
}
// Desync diagnostics (e.g. a knife model showing on the SSG): sample the carried
// weapon's definition/model and the view model's model once per second, logging
// only on change so the cheat log captures the exact moment and last known state
// around a desync. Kept in its own POD-only function because the __try below must
// not share scope with the set/map locals of FrameStage (MSVC C2712).
void LogDesyncSample(const Netvars& n, void* local) {
    static std::uint32_t last_sample{};
    static int last_def = -1, last_model = -1, last_vm = -1;
    if (GetTickCount() - last_sample <= 1000) return;
    last_sample = GetTickCount();
    int def = -1, model = -1, vm = -1;
    __try {
        if (n.active > 0 && n.active < 0x10000) {
            auto* weapon = Interfaces::EntityList->GetClientEntityFromHandle(At<std::uint32_t>(local, n.active));
            if (weapon) { def = At<short>(weapon, n.def); if (n.model > 0) model = At<int>(weapon, n.model); }
        }
        if (n.viewmodel > 0 && n.viewmodel < 0x10000) {
            auto* viewmodel = Interfaces::EntityList->GetClientEntityFromHandle(At<std::uint32_t>(local, n.viewmodel));
            if (viewmodel && n.model > 0) vm = At<int>(viewmodel, n.model);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    if (def != last_def || model != last_model || vm != last_vm) {
        LOG_INFO("skins: carried weapon def=%d model=%d viewmodel model=%d (was def=%d model=%d vm=%d)", def, model, vm, last_def, last_model, last_vm);
        last_def = def; last_model = model; last_vm = vm;
    }
}
} // namespace
void FrameStage() {
    if (!Interfaces::Engine || !Interfaces::EntityList || !PropManager::Get().IsReady() ||
        !Interfaces::Engine->IsInGame() || !Interfaces::Engine->IsConnected()) { Reset(); return; }
    EquipmentChanger::FrameStage();
    const auto skin_revision=SkinState::Revision();
    const auto equipment_revision=EquipmentState::Revision();
    if(selection_changes.Observe(skin_revision,equipment_revision)) RequestRefresh();
    const auto n = Resolve();
    if (!Valid(n)) {
        if (!logged_missing) { LOG_WARN("skins: required weapon/player netvars unavailable; application disabled"); logged_missing = true; }
        return;
    }
    logged_missing = false;
    auto* local = Interfaces::EntityList->GetClientEntity(Interfaces::Engine->GetLocalPlayer());
    if (!local) { Reset(); return; }
    const int team = At<int>(local, n.team);
    if (team != 2 && team != 3) { RunRefresh(n, local); return; }
    std::uint32_t handles[64]{};
    const auto* handles_src = reinterpret_cast<const std::uint32_t*>(reinterpret_cast<std::uintptr_t>(local) + n.weapons);
    for (int i=0; i<64; ++i) handles[i] = handles_src[i];
    if (originals.empty() && SkinState::Empty()) { RunRefresh(n, local); return; }
    std::uint32_t account{};
    std::uint64_t local_owner{};
    if (!LocalAccount(account,local_owner)) return;
    const bool allow_foreign=SkinState::AllowWeaponSkins();
    const bool owner_available=n.owner_low>0 && n.owner_low<0x10000 && n.owner_high>0 && n.owner_high<0x10000;
    std::set<std::uint32_t> held;
    for (int i=0; i<64; ++i) {
        const auto h = handles[i];
        if (h == 0xffffffffu || h == 0u) continue;
        auto* entity = Interfaces::EntityList->GetClientEntityFromHandle(h);
        if (!entity) continue;
        held.insert(h);
        const int def = At<short>(entity,n.def);
        SkinState::Choice selected{};
        const std::uint64_t original_owner=owner_available ?
            (static_cast<std::uint64_t>(At<std::uint32_t>(entity,n.owner_high))<<32) | At<std::uint32_t>(entity,n.owner_low) : 0;
        const bool enabled = SkinState::Get(team == 3, def, selected) &&
            SkinState::CanApply(allow_foreign,original_owner,local_owner);
        auto it = originals.find(h);
        if (it != originals.end() && (it->second.entity != entity || it->second.definition != def)) { originals.erase(it); it = originals.end(); }
        if (!enabled) {
            if (it != originals.end()) { Restore(entity,n,it->second); originals.erase(it); }
            continue;
        }
        if (it == originals.end()) {
            Original saved{}; Save(entity,def,n,saved);
            it = originals.emplace(h,saved).first;
        }
        it->second.revision = selected.revision;
        Apply(entity,n,it->second,selected,account);
    }
    // Restore dropped weapons too, but only if the original serial handle still resolves to the same entity.
    for (auto it = originals.begin(); it != originals.end();) {
        if (held.count(it->first)) { ++it; continue; }
        auto* entity = Interfaces::EntityList->GetClientEntityFromHandle(it->first);
        if (entity && entity == it->second.entity && At<short>(entity,n.def) == it->second.definition) {
            Restore(entity,n,it->second);
        }
        it = originals.erase(it);
    }
    LogDesyncSample(n, local);
    RunRefresh(n, local);
}
void Reset() { EquipmentChanger::Reset(); originals.clear(); update_pending = false; last_update = 0; full_update_in_progress = false; }
void Shutdown() {
    if (Interfaces::Engine && Interfaces::EntityList && Interfaces::Engine->IsInGame() && PropManager::Get().IsReady()) {
        const auto n = Resolve();
        if (Valid(n)) {
            auto* local = Interfaces::EntityList->GetClientEntity(Interfaces::Engine->GetLocalPlayer());
            bool restored = false;
            for (const auto& entry : originals) {
                auto* entity = Interfaces::EntityList->GetClientEntityFromHandle(entry.first);
                if (entity && entity == entry.second.entity && At<short>(entity,n.def) == entry.second.definition) {
                    Restore(entity,n,entry.second); restored = true;
                }
            }
            if (restored) { last_update = 0; RequestRefresh(); RunRefresh(n, local); }
        }
    }
    EquipmentChanger::Shutdown();
    Reset();
}
}
