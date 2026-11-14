#pragma once
// FNV1a + cthash — Fatality cthash.hpp / Havoc sdk/hash/fnv1a.h.
#include <cstdint>
#include <string_view>
namespace hash {
    constexpr uint32_t fnv1a(const char* str, uint32_t h = 2166136261u) {
        while (*str) { h ^= (uint8_t)*str++; h *= 16777619u; }
        return h;
    }
    constexpr uint32_t fnv1a(std::string_view s) {
        uint32_t h = 2166136261u;
        for (char c : s) { h ^= (uint8_t)c; h *= 16777619u; }
        return h;
    }
}
#define HASH(s) hash::fnv1a(s)
