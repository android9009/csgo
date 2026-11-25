#pragma once
#include <cmath>
class QAngle {
public:
    float x, y, z; // pitch, yaw, roll
    QAngle() = default;
    QAngle(float x, float y, float z) : x(x), y(y), z(z) {}
    void Init(float ix = 0.f, float iy = 0.f, float iz = 0.f) { x = ix; y = iy; z = iz; }
    void Zero() { x = y = z = 0.f; }
    bool IsValid() const { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }
    float& operator[](int i) { return reinterpret_cast<float*>(this)[i]; }
    float operator[](int i) const { return reinterpret_cast<const float*>(this)[i]; }
    QAngle operator+(const QAngle& v) const { return {x + v.x, y + v.y, z + v.z}; }
    QAngle operator-(const QAngle& v) const { return {x - v.x, y - v.y, z - v.z}; }
    QAngle& operator+=(const QAngle& v) { x += v.x; y += v.y; z += v.z; return *this; }
    QAngle& operator-=(const QAngle& v) { x -= v.x; y -= v.y; z -= v.z; return *this; }
    bool operator==(const QAngle& v) const { return x == v.x && y == v.y && z == v.z; }
    bool operator!=(const QAngle& v) const { return !(*this == v); }
    float* Base() { return &x; }
    const float* Base() const { return &x; }
};
