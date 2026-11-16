#include "features/skins/local_inventory_ids.hpp"
#include <cassert>
int main() {
 LocalInventory::IdAllocator ids;
 ids.Observe(123,4);ids.Observe(50,7);
 ids.Observe(0xf000000000000010ULL,99999);
 std::uint64_t id{};std::uint32_t position{};
 assert(ids.Next(id,position) && id==124 && position==8);
 // Removing our last object must not allow reuse of its identity.
 ids.Observe(123,4);
 assert(ids.Next(id,position) && id==125 && position==9);
 // Genuine new items may arrive between local selections.
 ids.Observe(1000,1000);
 assert(ids.Next(id,position) && id==1001 && position==1001);
 ids.Observe(0x0ffffffffffffffeULL,1001);
 assert(!ids.Next(id,position));
 LocalInventory::IdAllocator full;
 full.Observe(1,0xffffffffu);
 assert(!full.Next(id,position));
}
