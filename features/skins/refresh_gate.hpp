#pragma once
#include <cstdint>
namespace SkinRefresh {
// Lifetime is the DLL session, not an individual entity or network full update.
struct Gate {
    std::uint64_t skins{},equipment{};
    bool Observe(std::uint64_t skin_revision,std::uint64_t equipment_revision) {
        if(skins==skin_revision && equipment==equipment_revision)return false;
        skins=skin_revision;equipment=equipment_revision;return true;
    }
};
}
