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
    static Color Lerp(const Color& from, const Color& to, float t) {
        if (t < 0.f) t = 0.f; else if (t > 1.f) t = 1.f;
        return Color(
            uint8_t(from.r + (to.r - from.r) * t),
            uint8_t(from.g + (to.g - from.g) * t),
            uint8_t(from.b + (to.b - from.b) * t),
            uint8_t(from.a + (to.a - from.a) * t));
    }
    Color WithAlpha(uint8_t alpha) const { return Color(r, g, b, alpha); }
    bool operator==(const Color& c) const { return r==c.r && g==c.g && b==c.b && a==c.a; }
    bool operator!=(const Color& c) const { return !(*this == c); }
};
