#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>

// The declarations in this file are the Windows x86 ABI used by fatalcs.zip's
// entity_rendering.cpp/CMergedMDL.h. They are intentionally independent of the
// old public CS:GO SDK layouts.

struct IDirect3DTexture9;

namespace source_preview {
namespace offset {
// Decrypted from fatalcs.zip misc/offsets.h with its randomized_hash entries and
// runtime_basis = 2166136261. These are offsets, not the encrypted constants.
inline constexpr std::uintptr_t frame_stage_notify = 0x262B20;
inline constexpr std::uintptr_t global_vars = 0x59EE60;
inline constexpr std::uintptr_t model_render = 0x7847A0;
inline constexpr std::uintptr_t material_system = 0xE2AC8;
inline constexpr std::uintptr_t render_view = 0x5A1D14;
inline constexpr std::uintptr_t memalloc = 0x514F0;
inline constexpr std::uintptr_t studio_render = 0x41FE98;
inline constexpr std::uintptr_t mdl_create = 0x9AE9A0;
inline constexpr std::uintptr_t set_merged_mdl = 0x9AF480;
inline constexpr std::uintptr_t setup_bones_attachment_queries = 0x9AEED0;
inline constexpr std::uintptr_t disable_render_target_allocation = 0x9EC;
} // namespace offset

inline std::uintptr_t module_address(const char* module_name, std::uintptr_t relative) {
    const auto module = reinterpret_cast<std::uintptr_t>(GetModuleHandleA(module_name));
    return module ? module + relative : 0;
}

template <typename T>
T* module_object(const char* module_name, std::uintptr_t relative) {
    return reinterpret_cast<T*>(module_address(module_name, relative));
}

template <typename R, typename... Args>
R call_vfunc(void* instance, std::size_t index, Args... args) {
    using function_t = R(__thiscall*)(void*, Args...);
    const auto table = *static_cast<void***>(instance);
    return reinterpret_cast<function_t>(table[index])(instance, args...);
}

struct Vector {
    float x, y, z;
    Vector() = default;
    constexpr Vector(float x, float y, float z) : x(x), y(y), z(z) {}
};

struct QAngle {
    float x, y, z;
    QAngle() = default;
    constexpr QAngle(float x, float y, float z) : x(x), y(y), z(z) {}
};

struct Vector4D {
    float x, y, z, w;
    Vector4D() = default;
    constexpr Vector4D(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
};

struct matrix3x4_t {
    float m[3][4];
    float* operator[](int row) { return m[row]; }
    const float* operator[](int row) const { return m[row]; }
};

struct VMatrix {
    float m[4][4];
};

struct CViewSetup {
    int x, x_old;
    int y, y_old;
    int width, width_old;
    int height, height_old;
    bool m_bOrtho;
    float m_OrthoLeft;
    float m_OrthoTop;
    float m_OrthoRight;
    float m_OrthoBottom;
    bool m_bCustomViewMatrix;
    matrix3x4_t m_matCustomViewMatrix;
    std::byte pad_0x68[0x48];
    float fov;
    float fovViewmodel;
    Vector origin;
    QAngle angles;
    float zNear;
    float zFar;
    float zNearViewmodel;
    float zFarViewmodel;
    float m_flAspectRatio;
    float m_flNearBlurDepth;
    float m_flNearFocusDepth;
    float m_flFarFocusDepth;
    float m_flFarBlurDepth;
    float m_flNearBlurRadius;
    float m_flFarBlurRadius;
    int m_nDoFQuality;
    int m_nMotionBlurMode;
    float m_flShutterTime;
    Vector m_vShutterOpenPosition;
    QAngle m_shutterOpenAngles;
    Vector m_vShutterClosePosition;
    QAngle m_shutterCloseAngles;
    float m_flOffCenterTop;
    float m_flOffCenterBottom;
    float m_flOffCenterLeft;
    float m_flOffCenterRight;
    bool m_bOffCenter : 1;
    bool m_bRenderToSubrectOfLargerScreen : 1;
    bool m_bDoBloomAndToneMapping : 1;
    bool m_bDoDepthOfField : 1;
    bool m_bHDRTarget : 1;
    bool m_bDrawWorldNormal : 1;
    bool m_bCullFrontFaces : 1;
    bool m_bCacheFullSceneState : 1;
    bool m_bCSMView : 1;
};

struct VPlane {
    Vector m_Normal;
    float m_Dist;
};
using Frustum = VPlane[6];

inline constexpr int VIEW_CLEAR_COLOR = 0x1;
inline constexpr int VIEW_CLEAR_DEPTH = 0x2;
inline constexpr int VIEW_CLEAR_STENCIL = 0x20;

using ImageFormat = int;

enum RenderTargetSizeMode_t {
    RT_SIZE_NO_CHANGE = 0,
    RT_SIZE_DEFAULT = 1,
    RT_SIZE_PICMIP = 2,
    RT_SIZE_HDR = 3,
    RT_SIZE_FULL_FRAME_BUFFER = 4,
    RT_SIZE_OFFSCREEN = 5,
    RT_SIZE_FULL_FRAME_BUFFER_ROUNDED_UP = 6
};

enum MaterialRenderTargetDepth_t {
    MATERIAL_RT_DEPTH_SHARED = 0,
    MATERIAL_RT_DEPTH_SEPARATE = 1,
    MATERIAL_RT_DEPTH_NONE = 2,
    MATERIAL_RT_DEPTH_ONLY = 3
};

inline constexpr unsigned int TEXTUREFLAGS_CLAMPS = 0x00000004;
inline constexpr unsigned int TEXTUREFLAGS_CLAMPT = 0x00000008;
inline constexpr unsigned int CREATERENDERTARGETFLAGS_HDR = 0x00000001;
inline constexpr const char* TEXTURE_GROUP_CUBE_MAP = "CubeMap textures";

struct Texture_t {
    std::byte pad0[0xC];
    IDirect3DTexture9* lpRawTexture;
};

class ITexture {
public:
    std::byte pad0[0x50];
    Texture_t** pTextureHandles;

    int GetActualWidth() { return call_vfunc<int>(this, 3); }
    int GetActualHeight() { return call_vfunc<int>(this, 4); }
    void IncrementReferenceCount() { call_vfunc<void>(this, 10); }
    void DecrementReferenceCount() { call_vfunc<void>(this, 11); }
};

class IMatRenderContext {
public:
    int Release() { return call_vfunc<int>(this, 1); }
    void BeginRender() { call_vfunc<void>(this, 2); }
    void EndRender() { call_vfunc<void>(this, 3); }
    void BindLocalCubemap(ITexture* texture) { call_vfunc<void>(this, 5, texture); }
    void SetRenderTarget(ITexture* texture) { call_vfunc<void>(this, 6, texture); }
    ITexture* GetRenderTarget() { return call_vfunc<ITexture*>(this, 7); }
    void ClearBuffers(bool color, bool depth, bool stencil = false) {
        call_vfunc<void>(this, 12, color, depth, stencil);
    }
    void SetAmbientLightCube(const Vector4D* cube) { call_vfunc<void>(this, 18, cube); }
    void ClearColor4ub(unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
        call_vfunc<void>(this, 79, r, g, b, a);
    }
    void PushRenderTargetAndViewport() { call_vfunc<void>(this, 116); }
    void PushRenderTargetAndViewport(ITexture* texture) {
        call_vfunc<void>(this, 117, texture);
    }
    void PushRenderTargetAndViewport(ITexture* texture, int x, int y, int width, int height) {
        call_vfunc<void>(this, 118, texture, x, y, width, height);
    }
    void PushRenderTargetAndViewport(ITexture* texture, ITexture* depth_texture,
        int x, int y, int width, int height) {
        call_vfunc<void>(this, 119, texture, depth_texture, x, y, width, height);
    }
    void PopRenderTargetAndViewport() { call_vfunc<void>(this, 120); }
    void SetIntRenderingParameter(int parameter, int value) {
        call_vfunc<void>(this, 126, parameter, value);
    }
    void SetLightingOrigin(Vector origin) { call_vfunc<void>(this, 158, origin); }
};

class IMaterialSystem {
public:
    ImageFormat GetBackBufferFormat() { return call_vfunc<ImageFormat>(this, 36); }
    ITexture* FindTexture(const char* name, const char* group, bool complain = true,
                          int additional_flags = 0) {
        return call_vfunc<ITexture*>(this, 91, name, group, complain, additional_flags);
    }
    void BeginRenderTargetAllocation() { call_vfunc<void>(this, 94); }
    void EndRenderTargetAllocation() { call_vfunc<void>(this, 95); }
    ITexture* CreateNamedRenderTargetTextureEx(const char* name, int width, int height,
        RenderTargetSizeMode_t size_mode, ImageFormat format, MaterialRenderTargetDepth_t depth,
        unsigned int texture_flags, unsigned int render_target_flags) {
        return call_vfunc<ITexture*>(this, 97, name, width, height, size_mode, format, depth,
            texture_flags, render_target_flags);
    }
    IMatRenderContext* GetRenderContext() { return call_vfunc<IMatRenderContext*>(this, 115); }
    void FinishRenderTargetAllocation() { call_vfunc<void>(this, 136); }

    bool& DisableRenderTargetAllocationForever() {
        return *reinterpret_cast<bool*>(reinterpret_cast<std::uintptr_t>(this) +
            offset::disable_render_target_allocation);
    }
    void ForceBeginRenderTargetAllocation() {
        const bool old = DisableRenderTargetAllocationForever();
        DisableRenderTargetAllocationForever() = false;
        BeginRenderTargetAllocation();
        DisableRenderTargetAllocationForever() = old;
    }
    void ForceEndRenderTargetAllocation() {
        const bool old = DisableRenderTargetAllocationForever();
        DisableRenderTargetAllocationForever() = false;
        EndRenderTargetAllocation();
        DisableRenderTargetAllocationForever() = old;
    }
};

class IVRenderView {
public:
    void Push3DView(IMatRenderContext* context, const CViewSetup& view, int flags,
                    ITexture* render_target, Frustum frustum) {
        using function_t = void(__thiscall*)(void*, IMatRenderContext*, const CViewSetup&, int,
            ITexture*, VPlane*);
        const auto table = *reinterpret_cast<void***>(this);
        reinterpret_cast<function_t>(table[43])(this, context, view, flags, render_target, frustum);
    }
    void Push3DView(IMatRenderContext* context, const CViewSetup& view, int flags,
                    ITexture* render_target, Frustum frustum, ITexture* depth_texture) {
        using function_t = void(__thiscall*)(void*, IMatRenderContext*, const CViewSetup&, int,
            ITexture*, VPlane*, ITexture*);
        const auto table = *reinterpret_cast<void***>(this);
        reinterpret_cast<function_t>(table[55])(this, context, view, flags, render_target, frustum,
            depth_texture);
    }
    void PopView(IMatRenderContext* context, Frustum frustum) {
        using function_t = void(__thiscall*)(void*, IMatRenderContext*, VPlane*);
        const auto table = *reinterpret_cast<void***>(this);
        reinterpret_cast<function_t>(table[45])(this, context, frustum);
    }
    void GetMatricesForView(const CViewSetup& view, VMatrix* world_to_view,
        VMatrix* view_to_projection, VMatrix* world_to_projection, VMatrix* world_to_pixels) {
        using function_t = void(__thiscall*)(void*, const CViewSetup&, VMatrix*, VMatrix*, VMatrix*,
            VMatrix*);
        const auto table = *reinterpret_cast<void***>(this);
        reinterpret_cast<function_t>(table[56])(this, view, world_to_view, view_to_projection,
            world_to_projection, world_to_pixels);
    }
};

struct LightDesc_t;
class IStudioRender {
public:
    void SetAmbientLightColors2(const Vector4D* colors) { call_vfunc<void>(this, 20, colors); }
    void SetLocalLights2(int count, const LightDesc_t* lights) {
        call_vfunc<void>(this, 22, count, lights);
    }
};

class IVModelRender {
public:
    void SuppressEngineLighting(bool suppress) { call_vfunc<void>(this, 24, suppress); }
};

class IMemAlloc {
public:
    void* Alloc(std::size_t size) { return call_vfunc<void*>(this, 1, size); }
    void Free(void* memory) { call_vfunc<void>(this, 5, memory); }
};

class IGlobalVarsBase {
public:
    float realtime;
    int framecount;
    float absoluteframetime;
    float absoluteframestarttimestddev;
    float curtime;
    float frametime;
};

using MDLHandle_t = unsigned short;
inline constexpr int MAXSTUDIOFLEXCTRL = 96;

// Fatality's Color is four byte-sized channels. A uint32_t here has alignment 4 and would insert
// two extra bytes both before Color and before nSkin, shifting the rest of CMDL by four bytes.
struct color_rgba {
    std::uint8_t r, g, b, a;
};

class CCustomMaterialOwner;
class CMDL {
public:
    std::byte pad[0x3C];
    MDLHandle_t hModelHandle;
    std::byte pad0[0x8];
    color_rgba Color;
    std::byte pad1[0x2];
    int nSkin;
    int nBody;
    int nSequence;
    int nLOD;
    float flPlaybackRate;
    float flTime;
    float flCurrentAnimEndTime;
    float arrFlexControls[MAXSTUDIOFLEXCTRL * 4];
    Vector vecViewTarget;
    bool bWorldSpaceViewTarget;
    bool bUseSequencePlaybackFPS;
    std::byte pad2[0x2];
    void* pProxyData;
    float flTimeBasisAdjustment;
    // Fatality marks the remaining CMDL tail as unreliable. The foreign constructor owns the
    // complete 0x75C allocation; only this verified prefix is needed by the preview.
};

class IRenderToRTHelperObject {
public:
    virtual void Draw(const matrix3x4_t& root_to_world) = 0;
    virtual bool BoundingSphere(Vector& center, float& radius) = 0;
    virtual ITexture* GetEnvCubeMap() = 0;
};

class C_MergedMDL : public IRenderToRTHelperObject {
public:
    virtual ~C_MergedMDL() {}
    virtual void SetMDL(MDLHandle_t handle, CCustomMaterialOwner* owner = nullptr,
                        void* proxy_data = nullptr) = 0;
    virtual void SetMDL(const char* path, CCustomMaterialOwner* owner = nullptr,
                        void* proxy_data = nullptr) = 0;

    void SetMergedMDL(const char* path, CCustomMaterialOwner* owner = nullptr,
                      void* proxy_data = nullptr) {
        using function_t = void(__thiscall*)(void*, const char*, CCustomMaterialOwner*, void*, bool);
        reinterpret_cast<function_t>(module_address("client.dll", offset::set_merged_mdl))(
            this, path, owner, proxy_data, false);
    }
    void SetupBonesForAttachmentQueries() {
        using function_t = void(__thiscall*)(void*);
        reinterpret_cast<function_t>(module_address(
            "client.dll", offset::setup_bones_attachment_queries))(this);
    }
    void SetSequence(int sequence, bool use_sequence_playback_fps) {
        RootMDL.nSequence = sequence;
        RootMDL.bUseSequencePlaybackFPS = use_sequence_playback_fps;
        RootMDL.flTimeBasisAdjustment = RootMDL.flTime;
    }
    void create() {
        using function_t = void(__thiscall*)(void*);
        reinterpret_cast<function_t>(module_address("client.dll", offset::mdl_create))(this);
    }

public:
    CMDL RootMDL;
};

inline IGlobalVarsBase* global_vars() {
    return module_object<IGlobalVarsBase>("engine.dll", offset::global_vars);
}
inline IVModelRender* model_render() {
    return module_object<IVModelRender>("engine.dll", offset::model_render);
}
inline IMaterialSystem* material_system() {
    return module_object<IMaterialSystem>("materialsystem.dll", offset::material_system);
}
inline IVRenderView* render_view() {
    return module_object<IVRenderView>("engine.dll", offset::render_view);
}
inline IMemAlloc* memalloc() {
    return module_object<IMemAlloc>("tier0.dll", offset::memalloc);
}
inline IStudioRender* studio_render() {
    return module_object<IStudioRender>("studiorender.dll", offset::studio_render);
}

inline bool model_api_ready() {
    return module_address("client.dll", offset::mdl_create) &&
           module_address("client.dll", offset::set_merged_mdl) &&
           module_address("client.dll", offset::setup_bones_attachment_queries);
}

static_assert(offsetof(CViewSetup, fov) == 0xB0);
static_assert(offsetof(CViewSetup, origin) == 0xB8);
static_assert(offsetof(CViewSetup, zNear) == 0xD0);
static_assert(offsetof(Texture_t, lpRawTexture) == 0x0C);
static_assert(offsetof(ITexture, pTextureHandles) == 0x50);
static_assert(offsetof(IGlobalVarsBase, frametime) == 0x14);
static_assert(sizeof(color_rgba) == 0x4);
static_assert(alignof(color_rgba) == 0x1);
static_assert(offsetof(CMDL, hModelHandle) == 0x3C);
static_assert(offsetof(CMDL, Color) == 0x46);
static_assert(offsetof(CMDL, nSkin) == 0x4C);
static_assert(offsetof(CMDL, nSequence) == 0x54);
static_assert(offsetof(CMDL, flTime) == 0x60);
static_assert(offsetof(CMDL, arrFlexControls) == 0x68);
static_assert(offsetof(CMDL, bUseSequencePlaybackFPS) == 0x675);
static_assert(offsetof(CMDL, pProxyData) == 0x678);
static_assert(offsetof(CMDL, flTimeBasisAdjustment) == 0x67C);
static_assert(sizeof(CMDL) == 0x680);
} // namespace source_preview
