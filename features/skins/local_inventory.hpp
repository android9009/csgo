#pragma once
#include <atomic>
namespace LocalInventory {
inline std::atomic<bool> enabled{false};
// Game-thread entry, including outside matches. Experimental legacy PE32 ABI.
void Frame();
void Shutdown();
}
