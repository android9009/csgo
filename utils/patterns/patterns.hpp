#pragma once
// Runtime signature scanner. The catalogue itself lives in catalog.hpp, which is
// plain data so the portable tests can validate it without Windows headers.
#include "catalog.hpp"
#include <cstdint>
#include <string_view>
#include <vector>

namespace Patterns {
bool Init();
std::uintptr_t Get(std::string_view name);
const std::vector<Definition>& Definitions();
const std::vector<std::string_view>& Missing();
} // namespace Patterns
