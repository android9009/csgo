#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>

namespace VisualPreview {
struct Point { float x{}, y{}; };
struct Box { float left{.3f}, top{.15f}, right{.7f}, bottom{.9f}; };
struct Material {
    bool enabled{}, ignore_z{};
    int kind{};
    std::uint32_t primary{0xffffffff}, secondary{0xffffffff}, hidden{0xffffffff};
    bool operator==(const Material& o) const {
        return enabled==o.enabled && ignore_z==o.ignore_z && kind==o.kind && primary==o.primary && secondary==o.secondary && hidden==o.hidden;
    }
};
struct Geometry { Box box; bool valid{}; };
enum Element { Name, Health, Armor, Ammo, Weapon, Icon, Flags, ElementCount };
using Layout = std::array<Point, ElementCount>;
inline std::mutex mutex;
inline Material material;
inline Geometry geometry;
inline Layout layout{};
inline Material GetMaterial() { std::lock_guard<std::mutex> lock(mutex); return material; }
inline void SetMaterial(const Material& value) { std::lock_guard<std::mutex> lock(mutex); material=value; }
inline Geometry GetGeometry() { std::lock_guard<std::mutex> lock(mutex); return geometry; }
inline void SetGeometry(const Geometry& value) { std::lock_guard<std::mutex> lock(mutex); geometry=value; }
inline Layout GetLayout() { std::lock_guard<std::mutex> lock(mutex); return layout; }
inline Point ClampOffset(Point p) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y)) return {};
    return {(std::max)(-1.f,(std::min)(1.f,p.x)), (std::max)(-1.f,(std::min)(1.f,p.y))};
}
inline void SetOffset(int element, Point value) {
    if(element<0 || element>=ElementCount)return;
    std::lock_guard<std::mutex> lock(mutex); layout[element]=ClampOffset(value);
}
// Clamp the entire element, not just its origin, to the preview interior.
inline Point ClampPosition(Point p, Point size, Point viewport) {
    return {(std::max)(0.f,(std::min)(p.x,(std::max)(0.f,viewport.x-size.x))),
            (std::max)(0.f,(std::min)(p.y,(std::max)(0.f,viewport.y-size.y)))};
}
}
