#pragma once
#include "datatypes/datatypes.hpp"
#include <cstdint>
#include <cstring>

struct alignas(16) VectorAligned : Vector { float w = 0.f; };
struct Ray_t {
    VectorAligned m_Start{}, m_Delta{}, m_StartOffset{}, m_Extents{};
    const matrix3x4_t* m_pWorldAxisTransform = nullptr;
    bool m_IsRay = true;
    bool m_IsSwept = false;
    void Init(const Vector& start, const Vector& end) {
        m_Delta = VectorAligned{}; m_Delta.x = end.x - start.x; m_Delta.y = end.y - start.y; m_Delta.z = end.z - start.z;
        m_IsSwept = m_Delta.LengthSqr() != 0.f;
        m_Extents = VectorAligned{}; m_pWorldAxisTransform = nullptr;
        m_IsRay = true;
        m_Start = VectorAligned{}; m_Start.x = start.x; m_Start.y = start.y; m_Start.z = start.z;
        m_StartOffset = VectorAligned{};
    }
    void Init(const Vector& start, const Vector& end, const Vector& mins, const Vector& maxs) {
        m_Delta = VectorAligned{}; m_Delta.x = end.x - start.x; m_Delta.y = end.y - start.y; m_Delta.z = end.z - start.z;
        m_IsSwept = m_Delta.LengthSqr() != 0.f;
        m_Extents = VectorAligned{};
        m_Extents.x = (maxs.x - mins.x) * .5f; m_Extents.y = (maxs.y - mins.y) * .5f; m_Extents.z = (maxs.z - mins.z) * .5f;
        m_IsRay = m_Extents.LengthSqr() < 1e-6f;
        m_StartOffset = VectorAligned{};
        m_StartOffset.x = (maxs.x + mins.x) * -.5f; m_StartOffset.y = (maxs.y + mins.y) * -.5f; m_StartOffset.z = (maxs.z + mins.z) * -.5f;
        m_Start = VectorAligned{};
        m_Start.x = start.x - m_StartOffset.x; m_Start.y = start.y - m_StartOffset.y; m_Start.z = start.z - m_StartOffset.z;
        m_StartOffset.x = -m_StartOffset.x; m_StartOffset.y = -m_StartOffset.y; m_StartOffset.z = -m_StartOffset.z;
        m_pWorldAxisTransform = nullptr;
    }
};
struct cplane_t { Vector normal{}; float dist = 0.f; unsigned char type = 0, signbits = 0; unsigned char pad[2]{}; };
struct csurface_t { const char* name = nullptr; short surfaceProps = 0; unsigned short flags = 0; };
struct CBaseTrace {
    Vector startpos{}, endpos{};
    cplane_t plane{};
    float fraction = 1.f;
    int contents = 0;
    unsigned short dispFlags = 0;
    bool allsolid = false;
    bool startsolid = false;
};
struct CGameTrace : CBaseTrace {
    float fractionleftsolid = 0.f;
    csurface_t surface{};
    int hitgroup = 0;
    short physicsbone = 0;
    unsigned short worldSurfaceIndex = 0;
    void* m_pEnt = nullptr;
    int hitbox = 0;
};
class ITraceFilter {
public:
    virtual bool ShouldHitEntity(void* entity, int contents_mask) = 0;
    virtual int GetTraceType() const = 0;
};
class CTraceFilterSimple : public ITraceFilter {
public:
    explicit CTraceFilterSimple(const void* skip = nullptr) : m_skip(skip) {}
    bool ShouldHitEntity(void* entity, int) override { return entity != m_skip; }
    int GetTraceType() const override { return 0; }
private:
    const void* m_skip;
};
