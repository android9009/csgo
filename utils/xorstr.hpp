#pragma once
// Compile-time XOR string storage. Unlike runtime obfuscators, this is intentionally small.
#include <array>
#include <cstddef>
#include <cstdint>
namespace xorstr_detail {
    template <std::size_t N>
    struct crypt_string {
        std::array<char, N> data{};
        constexpr crypt_string(const char (&text)[N], std::uint32_t key) : data{} {
            for (std::size_t i = 0; i < N; ++i)
                data[i] = static_cast<char>(text[i] ^ static_cast<char>(key + i * 0x9E3779B9u));
        }
    };
}
#define XOR_STR(text) []() -> const char* { \
    constexpr auto encrypted = xorstr_detail::crypt_string{text, 0x5A5A5A5Au}; \
    static const auto decoded = [&]() { \
        std::array<char, sizeof(text)> result{}; \
        for (std::size_t i = 0; i < sizeof(text); ++i) \
            result[i] = static_cast<char>(encrypted.data[i] ^ static_cast<char>(0x5A5A5A5Au + i * 0x9E3779B9u)); \
        return result; \
    }(); \
    return decoded.data(); \
}()
#define _(text) XOR_STR(text)
