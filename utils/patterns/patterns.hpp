#pragma once
// Signature catalogue based on hazedumper config.json (2023-09-05), independently wrapped.
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>
#include <unordered_map>
namespace Patterns {
struct Definition {
    std::string_view name;
    std::string_view module;
    std::string_view signature;
    int offsets[4];
    unsigned char offset_count;
    bool relative;
    std::ptrdiff_t extra;
};
bool Init();
std::uintptr_t Get(std::string_view name);
const std::vector<Definition>& Definitions();
const std::vector<std::string_view>& Missing();
} // namespace Patterns
