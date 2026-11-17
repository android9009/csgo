#pragma once
// Vector / Vector2D — Fatality sdk/datatypes/Vector.h + Havoc sdk/datatypes/vector.hpp, своя реализация.
#include <cmath>
#include <limits>

class Vector2D {
public:
    float x, y;
    constexpr Vector2D(float x = 0.f, float y = 0.f) : x(x), y(y) {}
    bool IsValid() const { return std::isfinite(x) && std::isfinite(y); }
    bool IsZero() const { return x == 0.f && y == 0.f; }
    float Length() const { return std::sqrt(x * x + y * y); }
    float LengthSqr() const { return x * x + y * y; }
    float Dot(const Vector2D& v) const { return x * v.x + y * v.y; }
    Vector2D operator+(const Vector2D& v) const { return {x + v.x, y + v.y}; }
    Vector2D operator-(const Vector2D& v) const { return {x - v.x, y - v.y}; }
    Vector2D operator*(float s) const { return {x * s, y * s}; }
    Vector2D operator/(float s) const { return {x / s, y / s}; }
    Vector2D& operator+=(const Vector2D& v) { x += v.x; y += v.y; return *this; }
    Vector2D& operator-=(const Vector2D& v) { x -= v.x; y -= v.y; return *this; }
    bool operator==(const Vector2D& v) const { return x == v.x && y == v.y; }
    bool operator!=(const Vector2D& v) const { return !(*this == v); }
};

class Vector {
public:
    float x, y, z;
    Vector() = default;
    Vector(float x, float y, float z) : x(x), y(y), z(z) {}
    Vector(float v) : x(v), y(v), z(v) {}
    void Init(float ix = 0.f, float iy = 0.f, float iz = 0.f) { x = ix; y = iy; z = iz; }
    void Zero() { x = y = z = 0.f; }
    bool IsValid() const { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }
    bool IsZero(float tol = 0.01f) const {
        return x > -tol && x < tol && y > -tol && y < tol && z > -tol && z < tol;
    }
    float& operator[](int i) { return reinterpret_cast<float*>(this)[i]; }
    float operator[](int i) const { return reinterpret_cast<const float*>(this)[i]; }
    float Length() const { return std::sqrt(x * x + y * y + z * z); }
    float LengthSqr() const { return x * x + y * y + z * z; }
    float Length2D() const { return std::sqrt(x * x + y * y); }
    float Length2DSqr() const { return x * x + y * y; }
    float Dist(const Vector& v) const { return (*this - v).Length(); }
    float DistSqr(const Vector& v) const { return (*this - v).LengthSqr(); }
    float Dot(const Vector& v) const { return x * v.x + y * v.y + z * v.z; }
    Vector Cross(const Vector& v) const {
        return {y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x};
    }
    float NormalizeInPlace() {
        float l = Length();
        if (l > 0.f) { x /= l; y /= l; z /= l; }
        return l;
    }
    Vector Normalized() const { Vector v = *this; v.NormalizeInPlace(); return v; }
    Vector To2D() const { return {x, y, 0.f}; }
    Vector operator+(const Vector& v) const { return {x + v.x, y + v.y, z + v.z}; }
    Vector operator-(const Vector& v) const { return {x - v.x, y - v.y, z - v.z}; }
    Vector operator*(const Vector& v) const { return {x * v.x, y * v.y, z * v.z}; }
    Vector operator/(const Vector& v) const { return {x / v.x, y / v.y, z / v.z}; }
    Vector operator*(float s) const { return {x * s, y * s, z * s}; }
    Vector operator/(float s) const { return {x / s, y / s, z / s}; }
    Vector operator-() const { return {-x, -y, -z}; }
    Vector& operator+=(const Vector& v) { x += v.x; y += v.y; z += v.z; return *this; }
    Vector& operator-=(const Vector& v) { x -= v.x; y -= v.y; z -= v.z; return *this; }
    Vector& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    Vector& operator/=(float s) { x /= s; y /= s; z /= s; return *this; }
    bool operator==(const Vector& v) const { return x == v.x && y == v.y && z == v.z; }
    bool operator!=(const Vector& v) const { return !(*this == v); }
    float* Base() { return &x; }
    const float* Base() const { return &x; }
};
