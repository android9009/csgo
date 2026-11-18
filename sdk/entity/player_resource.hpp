#pragma once
// Per-slot match data for the scoreboard (skill group, wins, ...).
//
// CCSPlayerResource is not in the entity list: it is a single networked entity that
// the client reaches through the `dwPlayerResource` global, which holds a pointer to
// it. The arrays on it are indexed by player *slot* (entity index - 1).
#include "../netvars.hpp"
#include "../../utils/memory.hpp"
#include "../../utils/offsets/offsets.hpp"
#include <cstdint>

class CCSPlayerResource {
public:
    NETVAR_FALLBACK(int, m_iCompetitiveRanking, "DT_CSPlayerResource", "m_iCompetitiveRanking", offsets::netvars::m_iCompetitiveRanking)
    NETVAR_FALLBACK(int, m_iCompetitiveWins, "DT_CSPlayerResource", "m_iCompetitiveWins", offsets::netvars::m_iCompetitiveWins)

    // The netvar accessors hand back element 0 of the array, so the slot is the index
    // into that element's storage.
    int CompetitiveRanking(int slot) { return (&m_iCompetitiveRanking())[slot]; }
    int CompetitiveWins(int slot) { return (&m_iCompetitiveWins())[slot]; }

    // How many slots carry a skill group. The server only fills the array when it
    // decides to reveal ranks, so this doubles as a check that the reveal worked.
    int RankedSlots(int client_count) {
        int ranked = 0;
        for (int slot = 1; slot <= client_count && slot < kMaxSlots; ++slot)
            if (CompetitiveRanking(slot) > 0) ++ranked;
        return ranked;
    }

    // CS:GO has 64 player slots; the array is sized for that.
    static constexpr int kMaxSlots = 64;
};

// Null outside a live match, before the client has created the resource entity.
// Reading the global is a game-memory read, so callers guard it with SEH.
inline CCSPlayerResource* PlayerResource() {
    const auto base = reinterpret_cast<std::uintptr_t>(MEM::GetModuleBase("client.dll"));
    const auto rva = offsets::Get("dwPlayerResource");
    if (!base || !rva) return nullptr;
    return *reinterpret_cast<CCSPlayerResource**>(base + rva);
}
