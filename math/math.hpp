#pragma once
// Математика углов/проекции — Fatality misc/math + Havoc utils/math, своя реализация.
#include "vector.hpp"
#include "qangle.hpp"
#include "matrix.hpp"
#include "color.hpp"
namespace Math {
    bool Setup();
    float normalize_yaw(float yaw);
    float normalize_pitch(float pitch);
    QAngle normalize_angles(QAngle ang);
    float angle_diff(float src, float dst);
    QAngle calc_angle(const Vector& src, const Vector& dst);
    void angle_vectors(const QAngle& ang, Vector* forward, Vector* right = nullptr, Vector* up = nullptr);
    Vector angle_vector(const QAngle& ang);
    bool world_to_screen(const Vector& world, Vector2D& screen);
    float lerp(float a, float b, float t);
    float clamp(float v, float lo, float hi);
    int clamp(int v, int lo, int hi);
    float fov(const QAngle& view, const QAngle& target);
    Vector get_eye_pos(void* player);
}
