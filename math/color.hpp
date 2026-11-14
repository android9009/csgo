#pragma once
#include <cstdint>
class Color {
public:
    uint8_t r, g, b, a;
    Color() : r(0), g(0), b(0), a(255) {}
    Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) : r(r), g(g), b(b), a(a) {}
    Color(int r, int g, int b, int a = 255)
        : r((uint8_t)r), g((uint8_t)g), b((uint8_t)b), a((uint8_t)a) {}
    uint32_t ToUInt32() const { return (uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b); }
    bool operator==(const Color& c) const { return r==c.r && g==c.g && b==c.b && a==c.a; }
    bool operator!=(const Color& c) const { return !(*this == c); }
};
