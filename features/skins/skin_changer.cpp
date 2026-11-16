#include "skin_changer.hpp"
#include "skin_state.hpp"
#include "equipment_changer.hpp"
#include "equipment_state.hpp"
#include "refresh_gate.hpp"
#include "../../sdk/interfaces/interfaces.hpp"
#include "../../sdk/netvars.hpp"
#include "../../offsets/offsets.hpp"
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
void FlushRefresh() {
    if (!update_pending || !Interfaces::ClientState) return;
    const auto now = GetTickCount();
    if (last_update && now - last_update < 1000) return;
    const auto delta = offsets::Get("clientstate_delta_ticks");
    if (!delta || delta > 0x2000) { LOG_WARN("skins: full update offset unavailable"); update_pending=false; return; }
    // Same client-state full update mechanism used by Fatality's skinchanger.
    At<int>(Interfaces::ClientState, static_cast<std::ptrdiff_t>(delta)) = -1;
    last_update = now; update_pending = false;
    LOG_INFO("skins: requested client full update");
}
}
void FrameStage() {
    if (!Interfaces::Engine || !Interfaces::EntityList || !PropManager::Get().IsReady() ||
        !Interfaces::Engine->IsInGame() || !Interfaces::Engine->IsConnected()) { Reset(); return; }
    EquipmentChanger::FrameStage();
    const auto skin_revision=SkinState::Revision();
    const auto equipment_revision=EquipmentState::Revision();
    if(selection_changes.Observe(skin_revision,equipment_revision)) RequestRefresh();
    if (originals.empty() && SkinState::Empty()) { FlushRefresh(); return; }
    const auto n = Resolve();
    if (!Valid(n)) {
        if (!logged_missing) { LOG_WARN("skins: required weapon/player netvars unavailable; application disabled"); logged_missing = true; }
        return;
    }
    logged_missing = false;
    auto* local = Interfaces::EntityList->GetClientEntity(Interfaces::Engine->GetLocalPlayer());
    if (!local) { Reset(); return; }
    const int team = At<int>(local, n.team);
    if (team != 2 && team != 3) return;
    std::uint32_t account{};
    std::uint64_t local_owner{};
    if (!LocalAccount(account,local_owner)) return;
    const bool allow_foreign=SkinState::AllowWeaponSkins();
    const bool owner_available=n.owner_low>0 && n.owner_low<0x10000 && n.owner_high>0 && n.owner_high<0x10000;
    const auto* handles = reinterpret_cast<const std::uint32_t*>(reinterpret_cast<std::uintptr_t>(local) + n.weapons);
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
    FlushRefresh();
}
void Reset() { EquipmentChanger::Reset(); originals.clear(); update_pending = false; last_update = 0; }
void Shutdown() {
    if (Interfaces::Engine && Interfaces::EntityList && Interfaces::Engine->IsInGame() && PropManager::Get().IsReady()) {
        const auto n = Resolve();
        if (Valid(n)) {
            bool restored = false;
            for (const auto& entry : originals) {
                auto* entity = Interfaces::EntityList->GetClientEntityFromHandle(entry.first);
                if (entity && entity == entry.second.entity && At<short>(entity,n.def) == entry.second.definition) {
                    Restore(entity,n,entry.second); restored = true;
                }
            }
            if (restored) { last_update = 0; RequestRefresh(); FlushRefresh(); }
        }
    }
    EquipmentChanger::Shutdown();
    Reset();
}
}
