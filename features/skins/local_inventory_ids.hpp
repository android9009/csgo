#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>

namespace LocalInventory {
// Persistent high-water marks prevent Panorama from reusing stale item images
// after a synthetic item is removed/replaced. Base/default IDs are not economic IDs.
struct IdAllocator {
    std::uint64_t item{};
    std::uint32_t position{};
    void Observe(std::uint64_t id, std::uint32_t inventory_position) {
        if(id & 0xf000000000000000ULL) return;
        item=(std::max)(item,id);
        position=(std::max)(position,inventory_position);
    }
    bool Next(std::uint64_t& id,std::uint32_t& inventory_position) {
        if(item>=0x0ffffffffffffffeULL || position==(std::numeric_limits<std::uint32_t>::max)()) return false;
        id=++item;inventory_position=++position;return true;
    }
};
}
