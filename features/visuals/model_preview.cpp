// Experimental Source 1 preview, based on Fatality's entity_rendering::paint.
#include "model_preview.hpp"
#include "../../sdk/model_preview.hpp"
#include "../../sdk/interfaces/interfaces.hpp"
#include "../../third_party/fatality/preview_bridge.hpp"
#include "../../utils/log.hpp"
#include <windows.h>
#include <d3d9.h>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace ModelPreview {
namespace {
using namespace SourceModelPreview;
template<typename T> T Slot(void* p, int index) { return reinterpret_cast<T>((*reinterpret_cast<void***>(p))[index]); }
void* render_texture{};
void* cubemap{};
void* model{};
bool failed{}, texture_retained{};
const char* volatile phase = "not started";
struct Fault { DWORD code{}; std::uintptr_t instruction{}, address{}; ULONG_PTR operation{}; };
Fault fault{};
int CaptureFault(EXCEPTION_POINTERS* info) {
    if (info && info->ExceptionRecord) {
        const auto* record = info->ExceptionRecord;
        fault.code = record->ExceptionCode;
        fault.instruction = reinterpret_cast<std::uintptr_t>(record->ExceptionAddress);
        if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
            fault.operation = record->ExceptionInformation[0];
            fault.address = record->ExceptionInformation[1];
        }
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
std::uintptr_t ctor{}, bones{}, merged{};
constexpr int width = 235, height = 370;

bool Resolve() {
    ctor = model_constructor.load(std::memory_order_relaxed);
    bones = attachment_bones.load(std::memory_order_relaxed);
    merged = set_merged_model.load(std::memory_order_relaxed);
    return ctor && bones && merged;
}

bool Initialize() {
    auto* ms = Interfaces::MaterialSystem;
    auto* alloc = Interfaces::MemAlloc;
    if (!ms || !Interfaces::RenderView || !Interfaces::StudioRender || !Interfaces::ModelRender || !alloc || !Resolve()) return false;
    // Fatality's Source 1 material system uses this flag to temporarily allow target allocation.
    phase = "read material allocation flag";
    auto* allocation_disabled = reinterpret_cast<bool*>(reinterpret_cast<std::uintptr_t>(ms) + 0x9ec);
    const bool old = *allocation_disabled;
    *allocation_disabled = false;
    phase = "begin render target allocation";
    Slot<void(__thiscall*)(void*)>(ms, 94)(ms);
    phase = "get back buffer format";
    const int format = Slot<int(__thiscall*)(void*)>(ms, 36)(ms);
    phase = "create render target texture";
    render_texture = Slot<void*(__thiscall*)(void*, const char*, int, int, int, int, int, unsigned, unsigned)>(ms, 97)(
        ms, "_fatality_preview_port", width, height, 0, format, 0, 0x4u | 0x8u, 1u);
    phase = "end render target allocation";
    Slot<void(__thiscall*)(void*)>(ms, 95)(ms);
    *allocation_disabled = old;
    phase = "finish render target allocation";
    Slot<void(__thiscall*)(void*)>(ms, 136)(ms);
    if (!render_texture) return false;
    phase = "retain render target";
    Slot<void(__thiscall*)(void*)>(render_texture, 10)(render_texture);
    texture_retained = true;
    phase = "find cubemap";
    cubemap = Slot<void*(__thiscall*)(void*, const char*, const char*, bool, int)>(ms, 91)(ms, "editor/cubemap.hdr", "CubeMap textures", true, 0);
    phase = "allocate merged model";
    model = Slot<void*(__thiscall*)(void*, size_t)>(alloc, 1)(alloc, 0x75c);
    if (!model) return false;
    std::memset(model, 0, 0x75c);
    phase = "construct merged model";
    reinterpret_cast<ConstructModelFn>(ctor)(static_cast<MergedModel*>(model));
    phase = "set animation model";
    Slot<void(__thiscall*)(void*, const char*, void*, void*)>(model, 5)(model, "models/player/custom_player/uiplayer/animset_uiplayer.mdl", nullptr, nullptr);
    phase = "merge player model";
    reinterpret_cast<SetMergedModelFn>(merged)(static_cast<MergedModel*>(model), "models/player/custom_player/legacy/ctm_fbi_variantb.mdl", nullptr, nullptr, false);
    *reinterpret_cast<int*>(reinterpret_cast<std::uintptr_t>(model) + 4 + 0x54) = 113;
    *reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(model) + 4 + 0x60) = 20.f;
    phase = "setup model bones";
    reinterpret_cast<SetupAttachmentBonesFn>(bones)(static_cast<MergedModel*>(model));
    LOG_INFO("model preview initialized (experimental Source 1)");
    return true;
}

void PaintImpl() {
    if (!render_texture && !Initialize()) { failed = true; LOG_WARN("model preview initialization failed"); return; }
    if (!model) { failed = true; return; }
    auto* ms = Interfaces::MaterialSystem;
    phase = "get render context";
    void* context = Slot<void*(__thiscall*)(void*)>(ms, 115)(ms);
    if (!context) return;
    phase = "begin context";
    Slot<void(__thiscall*)(void*)>(context, 2)(context);
    phase = "push render target and viewport";
    Slot<void(__thiscall*)(void*)>(context, 116)(context);
    phase = "bind render target";
    Slot<void(__thiscall*)(void*, void*)>(context, 6)(context, render_texture);
    phase = "bind cubemap";
    Slot<void(__thiscall*)(void*, void*)>(context, 5)(context, cubemap);
    phase = "set lighting origin";
    Slot<void(__thiscall*)(void*, Vector)>(context, 158)(context, Vector{-65.f, 10.f, 100.f});
    phase = "set rendering parameter";
    Slot<void(__thiscall*)(void*, int, int)>(context, 126)(context, 10, 0);
    ViewSetup view{};
    view.width = width; view.height = height;
    view.fov = 12.f; view.origin = Vector{-FatalityPreviewBridge::distance.load(std::memory_order_relaxed), 0.f, 52.f};
    view.angles = QAngle{2.f, 0.f, 0.f}; view.near_z = 20.f; view.far_z = 1000.f;
    view.flags = 4; // bloom and tone mapping, as in Fatality
    Frustum frustum{};
    auto* rv = Interfaces::RenderView;
    phase = "push 3D view";
    Slot<Push3DViewFn>(rv, static_cast<int>(push_3d_view_slot))(rv, static_cast<RenderContext*>(context), view, clear_color_depth_stencil, static_cast<Texture*>(render_texture), frustum);
    phase = "clear color";
    Slot<void(__thiscall*)(void*, unsigned char, unsigned char, unsigned char, unsigned char)>(context, 79)(context, 0, 0, 0, 0);
    phase = "clear buffers";
    Slot<void(__thiscall*)(void*, bool, bool, bool)>(context, 12)(context, true, true, true);
    struct Light { float x,y,z,w; };
    Light light[6]; for (auto& l : light) l = {0.68f, 1.53f, 1.7f, 1.f};
    phase = "set ambient light";
    Slot<void(__thiscall*)(void*, Light*)>(context, 18)(context, light);
    phase = "set studio ambient light";
    Slot<void(__thiscall*)(void*, const Light*)>(Interfaces::StudioRender, 20)(Interfaces::StudioRender, light);
    phase = "set studio local lights";
    Slot<void(__thiscall*)(void*, int, void*)>(Interfaces::StudioRender, 22)(Interfaces::StudioRender, 0, nullptr);
    const float angle = FatalityPreviewBridge::yaw.load(std::memory_order_relaxed) * 0.01745329252f;
    const float pitch = FatalityPreviewBridge::pitch.load(std::memory_order_relaxed) * 0.01745329252f;
    const float cy = std::cos(angle), sy = std::sin(angle), cp = std::cos(pitch), sp = std::sin(pitch);
    matrix3x4_t matrix{};
    matrix[0][0] = cp * cy; matrix[0][1] = -sy; matrix[0][2] = sp * cy;
    matrix[1][0] = cp * sy; matrix[1][1] = cy; matrix[1][2] = sp * sy;
    matrix[2][0] = -sp; matrix[2][2] = cp;
    static DWORD last_time{};
    const DWORD now = GetTickCount();
    if (last_time) {
        const DWORD elapsed = now - last_time;
        *reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(model) + 4 + 0x60) += (elapsed < 100 ? elapsed : 100) * 0.0005f;
    }
    last_time = now;
    auto* mr = Interfaces::ModelRender;
    phase = "suppress engine lighting";
    Slot<void(__thiscall*)(void*, bool)>(mr, 24)(mr, true);
    phase = "draw merged model";
    Slot<void(__thiscall*)(void*, const matrix3x4_t&)>(model, 0)(model, matrix);
    phase = "restore engine lighting";
    Slot<void(__thiscall*)(void*, bool)>(mr, 24)(mr, false);
    phase = "pop 3D view";
    Slot<PopViewFn>(rv, static_cast<int>(pop_view_slot))(rv, static_cast<RenderContext*>(context), frustum);
    phase = "unbind cubemap";
    Slot<void(__thiscall*)(void*, void*)>(context, 5)(context, nullptr);
    phase = "pop render target";
    Slot<void(__thiscall*)(void*)>(context, 120)(context);
    phase = "end context";
    Slot<void(__thiscall*)(void*)>(context, 3)(context);
    phase = "release context";
    Slot<int(__thiscall*)(void*)>(context, 1)(context);
    phase = "read texture handles";
    auto handles = *reinterpret_cast<void***>(reinterpret_cast<std::uintptr_t>(render_texture) + 0x50);
    void* texture = handles && handles[0] ? *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(handles[0]) + 0x0c) : nullptr;
    FatalityPreviewBridge::texture.store(texture, std::memory_order_release);
}
}
void Paint() {
    if (failed || !FatalityPreviewBridge::visible.load(std::memory_order_relaxed)) return;
    __try { PaintImpl(); }
    __except (CaptureFault(GetExceptionInformation())) {
        failed = true; FatalityPreviewBridge::texture.store(nullptr, std::memory_order_release);
        HMODULE owner = nullptr;
        char module[MAX_PATH]{};
        std::uintptr_t offset = 0;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(fault.instruction), &owner)) {
            GetModuleFileNameA(owner, module, MAX_PATH);
            offset = fault.instruction - reinterpret_cast<std::uintptr_t>(owner);
        }
        LOG_ERROR("model preview fault: phase=%s code=0x%08lX instruction=%p module=%s rva=0x%08lX access=%llu address=%p; preview disabled",
            phase, fault.code, reinterpret_cast<void*>(fault.instruction), module[0] ? module : "unknown",
            static_cast<unsigned long>(offset), static_cast<unsigned long long>(fault.operation), reinterpret_cast<void*>(fault.address));
    }
}
void BeforeReset() { FatalityPreviewBridge::texture.store(nullptr, std::memory_order_release); }
void Shutdown() {
    FatalityPreviewBridge::visible.store(false, std::memory_order_relaxed);
    FatalityPreviewBridge::texture.store(nullptr, std::memory_order_release);
    if (model && Interfaces::MemAlloc) Slot<void(__thiscall*)(void*, void*)>(Interfaces::MemAlloc, 5)(Interfaces::MemAlloc, model);
    if (render_texture && texture_retained) Slot<void(__thiscall*)(void*)>(render_texture, 11)(render_texture);
    texture_retained = false;
    model = render_texture = cubemap = nullptr;
}
}
