#pragma once
// Compat shim: the ported menu includes the old project's netvar helpers.
// Nothing in the menu tree resolves properties at runtime (it reads netvars
// by fixed offset through the entity layer), so the adapter is a no-op
// namespace the old headers/inline code can reference.
#include <cstdint>
namespace netvars {
inline int Resolve(void*, const char*) { return 0; }
}
