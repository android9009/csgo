// Experimental Source 1 preview, based on Fatality's entity_rendering::paint.
#include "model_preview.hpp"
#include "../../sdk/model_preview.hpp"
#include "../../sdk/model_preview_render.hpp"
#include "../../sdk/interfaces/interfaces.hpp"
#include "../../third_party/fatality/preview_bridge.hpp"
#include "../../third_party/fatality/preview_catalog.hpp"
#include "../../sdk/skin_icon_file.hpp"
#include "../../utils/log.hpp"
#include <windows.h>
#include <d3d9.h>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace ModelPreview {
namespace {
using namespace SourceModelPreview;
template<typename T> T Slot(void* p, int index) { return reinterpret_cast<T>((*reinterpret_cast<void***>(p))[index]); }
void* render_texture{};
void* cubemap{};
void* editor_target{};
void* editor_models[sizeof(PreviewCatalog::weapons)/sizeof(*PreviewCatalog::weapons)]{};
bool editor_missing[sizeof(PreviewCatalog::weapons)/sizeof(*PreviewCatalog::weapons)]{};
bool editor_failed{}, editor_target_retained{};
bool* editor_allocation_flag{};
bool editor_allocation_old{}, editor_allocation_begun{};
void* model{};
// The engine destructor ABI is not verified. Keep a bounded cache instead of
// repeatedly destroying merged models while the pointer moves over the grid.
struct CachedModel { int team, agent, weapon; void* model; };
CachedModel model_cache[128]{};
size_t model_count{};
bool cache_limit_logged{};
bool missing_models[2][(std::max)(sizeof(PreviewCatalog::ct)/sizeof(*PreviewCatalog::ct), sizeof(PreviewCatalog::t)/sizeof(*PreviewCatalog::t))][sizeof(PreviewCatalog::weapons)/sizeof(*PreviewCatalog::weapons)]{};
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
struct RenderState {
    MatRenderContextABI* context{};
    RenderViewABI* view{};
    Frustum frustum{};
    bool begun{}, target_pushed{}, view_pushed{}, lighting_suppressed{}, cubemap_bound{};
};
RenderState active{};
constexpr int width = 235, height = 370;

// Restore only state whose corresponding push/bind completed successfully.
// A second engine exception is contained; preview stays disabled afterwards.
void RestoreAfterFault() {
    __try {
        if (editor_allocation_begun && Interfaces::MaterialSystem)
            Slot<void(__thiscall*)(void*)>(Interfaces::MaterialSystem, 95)(Interfaces::MaterialSystem);
        if (editor_allocation_flag) *editor_allocation_flag = editor_allocation_old;
        editor_allocation_begun = false; editor_allocation_flag = nullptr;
        if (active.lighting_suppressed && Interfaces::ModelRender)
            Slot<void(__thiscall*)(void*, bool)>(Interfaces::ModelRender, 24)(Interfaces::ModelRender, false);
        if (active.view_pushed && active.view && active.context)
            active.view->PopView(active.context, active.frustum);
        if (active.cubemap_bound && active.context) active.context->BindLocalCubemap(nullptr);
        if (active.target_pushed && active.context) active.context->PopRenderTargetAndViewport();
        if (active.begun && active.context) active.context->EndRender();
        if (active.context) active.context->Release();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LOG_ERROR("model preview: failed to restore render state after exception");
    }
    active = {};
}

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
    LOG_INFO("model preview render target initialized");
    return true;
}

bool ModelExists(const char* path) {
    auto* fs = Interfaces::BaseFileSystem;
    if (!fs) return true; // Preserve the previously working default when the filesystem is unavailable.
    using Open = void* (__thiscall*)(void*, const char*, const char*, const char*);
    using Close = void (__thiscall*)(void*, void*);
    void* file = SkinIconFile::Slot<Open>(fs, 2)(fs, path, "rb", "GAME");
    if (!file) return false;
    SkinIconFile::Slot<Close>(fs, 3)(fs, file);
    return true;
}
bool InitializeModel(bool ct, int agent_index, int weapon_index) {
    auto* alloc = Interfaces::MemAlloc;
    const int team = ct ? 0 : 1;
    phase = "allocate merged model";
    model = Slot<void*(__thiscall*)(void*, size_t)>(alloc, 1)(alloc, 0x75c);
    if (!model) return false;
    std::memset(model, 0, 0x75c);
    phase = "construct merged model";
    reinterpret_cast<ConstructModelFn>(ctor)(static_cast<MergedModel*>(model));
    phase = "inspect model interface";
    auto* merged_model = static_cast<MergedModel*>(model);
    auto** table = *reinterpret_cast<void***>(model);
    const bool in_game = Interfaces::Engine && Interfaces::Engine->IsInGame();
    LOG_INFO("model preview: model=%p vtable=%p slots=[%p,%p,%p,%p,%p,%p] mdlcache=%p in_game=%d", model, table,
        table[0], table[1], table[2], table[3], table[4], table[5], Interfaces::MDLCache, in_game ? 1 : 0);
    phase = "set animation model";
    merged_model->SetMDL("models/player/custom_player/uiplayer/animset_uiplayer.mdl");
    phase = "merge player model";
    const char* agent = PreviewCatalog::GetAgent(ct, static_cast<size_t>(agent_index)).model;
    LOG_INFO("model preview: loading %s model %s", ct ? "CT" : "T", agent);
    reinterpret_cast<SetMergedModelFn>(merged)(static_cast<MergedModel*>(model), agent, nullptr, nullptr, false);
    phase = "merge weapon model";
    reinterpret_cast<SetMergedModelFn>(merged)(static_cast<MergedModel*>(model), PreviewCatalog::weapons[weapon_index].model, nullptr, nullptr, false);
    *reinterpret_cast<int*>(reinterpret_cast<std::uintptr_t>(model) + 4 + 0x54) = 113;
    *reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(model) + 4 + 0x60) = 20.f;
    phase = "setup model bones";
    reinterpret_cast<SetupAttachmentBonesFn>(bones)(static_cast<MergedModel*>(model));
    model_cache[model_count++] = {team, agent_index, weapon_index, model};
    LOG_INFO("model preview %s model initialized agent=%d weapon=%s (experimental Source 1)", ct ? "CT" : "T", agent_index, PreviewCatalog::weapons[weapon_index].id);
    return true;
}

void DrawTarget(void* target, bool weapon_only) {
    auto* ms = Interfaces::MaterialSystem;
    phase = "get render context";
    auto* context = static_cast<MatRenderContextABI*>(Slot<void*(__thiscall*)(void*)>(ms, 115)(ms));
    if (!context) return;
    active = {};
    active.context = context;
    phase = "begin context";
    context->BeginRender();
    active.begun = true;
    phase = "push render target and viewport";
    context->PushRenderTargetAndViewport();
    active.target_pushed = true;
    phase = "bind render target";
    context->SetRenderTarget(static_cast<Texture*>(target));
    phase = "bind cubemap";
    context->BindLocalCubemap(static_cast<Texture*>(cubemap));
    active.cubemap_bound = true;
    phase = "set lighting origin";
    context->SetLightingOrigin(Vector{-65.f, 10.f, 100.f});
    phase = "set rendering parameter";
    context->SetIntRenderingParameter(10, 0);
    ViewSetup view{};
    view.width = weapon_only ? 452 : width; view.height = weapon_only ? 128 : height;
    Vector center{}; float radius = 32.f;
    if (weapon_only) {
        phase = "weapon bounding sphere";
        if (!static_cast<MergedModel*>(model)->BoundingSphere(center, radius) || !std::isfinite(radius) || radius < 1.f || radius > 200.f ||
            !std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z)) { center = {}; radius = 32.f; }
        view.fov = 50.f; view.origin = Vector{-(std::max)(70.f, radius * 9.f), 0.f, 0.f};
        view.angles = QAngle{0.f, 0.f, 0.f}; view.near_z = 1.f; view.far_z = 4000.f;
    } else {
        view.fov = 12.f; view.origin = Vector{-FatalityPreviewBridge::distance.load(std::memory_order_relaxed), 0.f, 55.f};
        view.angles = QAngle{2.f, 0.f, 0.f}; view.near_z = 20.f; view.far_z = 1000.f;
    }
    view.flags = 4; // bloom and tone mapping, as in Fatality
    auto* rv = reinterpret_cast<RenderViewABI*>(Interfaces::RenderView);
    active.view = rv;
    phase = "push 3D view";
    rv->Push3DView(context, view, clear_color_depth_stencil, static_cast<Texture*>(target), active.frustum);
    active.view_pushed = true;
    phase = "clear color";
    context->ClearColor4ub(0, 0, 0, 255);
    phase = "clear buffers";
    context->ClearBuffers(true, true, true);
    struct Light { float x,y,z,w; };
    Light light[6]; for (auto& l : light) l = {0.68f, 1.53f, 1.7f, 1.f};
    phase = "set ambient light";
    context->SetAmbientLightCube(light);
    phase = "set studio ambient light";
    Slot<void(__thiscall*)(void*, const Light*)>(Interfaces::StudioRender, 20)(Interfaces::StudioRender, light);
    phase = "set studio local lights";
    Slot<void(__thiscall*)(void*, int, void*)>(Interfaces::StudioRender, 22)(Interfaces::StudioRender, 0, nullptr);
    constexpr float to_rad = 0.01745329252f;
    const float yaw = (weapon_only ? FatalityPreviewBridge::editor_yaw.load(std::memory_order_relaxed) : FatalityPreviewBridge::yaw.load(std::memory_order_relaxed)) * to_rad;
    const float pitch = (weapon_only ? FatalityPreviewBridge::editor_pitch.load(std::memory_order_relaxed) : 0.f) * to_rad;
    const float roll = (weapon_only ? FatalityPreviewBridge::editor_roll.load(std::memory_order_relaxed) : 0.f) * to_rad;
    const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch), cr = std::cos(roll), sr = std::sin(roll);
    matrix3x4_t matrix{};
    matrix[0][0]=cy*cp; matrix[0][1]=cy*sp*sr-sy*cr; matrix[0][2]=cy*sp*cr+sy*sr;
    matrix[1][0]=sy*cp; matrix[1][1]=sy*sp*sr+cy*cr; matrix[1][2]=sy*sp*cr-cy*sr;
    matrix[2][0]=-sp; matrix[2][1]=cp*sr; matrix[2][2]=cp*cr;
    if (weapon_only) {
        for (int i=0; i<3; ++i) matrix[i][3] = -(matrix[i][0]*center.x + matrix[i][1]*center.y + matrix[i][2]*center.z);
    }
    static DWORD last_time{};
    const DWORD now = GetTickCount();
    if (!weapon_only && last_time) {
        const DWORD elapsed = now - last_time;
        *reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(model) + 4 + 0x60) += (elapsed < 100 ? elapsed : 100) * 0.0005f;
    }
    last_time = now;
    auto* mr = Interfaces::ModelRender;
    phase = "suppress engine lighting";
    Slot<void(__thiscall*)(void*, bool)>(mr, 24)(mr, true);
    active.lighting_suppressed = true;
    phase = "draw merged model";
    static_cast<MergedModel*>(model)->Draw(matrix);
    phase = "restore engine lighting";
    Slot<void(__thiscall*)(void*, bool)>(mr, 24)(mr, false);
    active.lighting_suppressed = false;
    phase = "pop 3D view";
    rv->PopView(context, active.frustum);
    active.view_pushed = false;
    phase = "unbind cubemap";
    context->BindLocalCubemap(nullptr);
    active.cubemap_bound = false;
    phase = "pop render target";
    context->PopRenderTargetAndViewport();
    active.target_pushed = false;
    phase = "end context";
    context->EndRender();
    active.begun = false;
    phase = "release context";
    context->Release();
    active.context = nullptr;
    phase = "read texture handles";
    auto handles = *reinterpret_cast<void***>(reinterpret_cast<std::uintptr_t>(target) + 0x50);
    void* texture = handles && handles[0] ? *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(handles[0]) + 0x0c) : nullptr;
    if (weapon_only) FatalityPreviewBridge::editor_texture.store(texture, std::memory_order_release);
    else FatalityPreviewBridge::texture.store(texture, std::memory_order_release);
}

void PaintImpl() {
    if (!render_texture && !Initialize()) { failed = true; LOG_WARN("model preview initialization failed"); return; }
    const bool ct = FatalityPreviewBridge::render_ct.load(std::memory_order_relaxed);
    const bool skins = FatalityPreviewBridge::render_skins.load(std::memory_order_relaxed);
    const int team = ct ? 0 : 1;
    int agent_index = skins ? FatalityPreviewBridge::agent[team].load(std::memory_order_relaxed) : 0;
    int weapon_index = skins ? FatalityPreviewBridge::weapon.load(std::memory_order_relaxed) : 0;
    if (agent_index < 0 || static_cast<size_t>(agent_index) >= PreviewCatalog::AgentCount(ct)) agent_index = 0;
    if (weapon_index < 0 || static_cast<size_t>(weapon_index) >= PreviewCatalog::WeaponCount()) weapon_index = 0;
    model = nullptr;
    for (size_t i = 0; i < model_count; ++i)
        if (model_cache[i].team == team && model_cache[i].agent == agent_index && model_cache[i].weapon == weapon_index) { model = model_cache[i].model; break; }
    if (!model) {
        // Check availability before asking the engine to merge a potentially absent model.
        if (missing_models[team][agent_index][weapon_index]) {
            FatalityPreviewBridge::texture.store(nullptr, std::memory_order_release);
            return;
        }
        if (!ModelExists(PreviewCatalog::GetAgent(ct, static_cast<size_t>(agent_index)).model) ||
            !ModelExists(PreviewCatalog::weapons[weapon_index].model)) {
            missing_models[team][agent_index][weapon_index] = true;
            LOG_WARN("model preview: requested agent=%d weapon=%s is unavailable", agent_index, PreviewCatalog::weapons[weapon_index].id);
            FatalityPreviewBridge::texture.store(nullptr, std::memory_order_release);
            return;
        }
        if (model_count >= sizeof(model_cache)/sizeof(*model_cache)) {
            if (!cache_limit_logged) { LOG_WARN("model preview: model cache limit reached; restart required"); cache_limit_logged = true; }
            FatalityPreviewBridge::texture.store(nullptr, std::memory_order_release);
            return;
        }
        if (!InitializeModel(ct, agent_index, weapon_index)) { failed = true; LOG_WARN("model preview model initialization failed"); return; }
    }
    DrawTarget(render_texture, false);
}
// The editor renders a default-material model only. It never accesses a live
// weapon entity or pretends that the currently selected paint is applied.
void PaintWeaponImpl() {
    if (!render_texture) return; // normal preview owns common initialization
    if (!editor_target) {
        auto* ms = Interfaces::MaterialSystem;
        phase = "weapon target allocation";
        auto* disabled = reinterpret_cast<bool*>(reinterpret_cast<std::uintptr_t>(ms) + 0x9ec);
        editor_allocation_old = *disabled; editor_allocation_flag = disabled; *disabled = false;
        Slot<void(__thiscall*)(void*)>(ms, 94)(ms); editor_allocation_begun = true;
        const int format = Slot<int(__thiscall*)(void*)>(ms, 36)(ms);
        editor_target = Slot<void*(__thiscall*)(void*, const char*, int, int, int, int, int, unsigned, unsigned)>(ms, 97)(
            ms, "_fatality_weapon_preview_port", 452, 128, 0, format, 0, 0x4u | 0x8u, 1u);
        Slot<void(__thiscall*)(void*)>(ms, 95)(ms); editor_allocation_begun = false;
        *disabled = editor_allocation_old; editor_allocation_flag = nullptr;
        Slot<void(__thiscall*)(void*)>(ms, 136)(ms);
        if (!editor_target) { editor_failed = true; return; }
        Slot<void(__thiscall*)(void*)>(editor_target, 10)(editor_target);
        editor_target_retained = true;
    }
    const int index = FatalityPreviewBridge::editor_weapon.load(std::memory_order_relaxed);
    if (index < 0 || static_cast<size_t>(index) >= PreviewCatalog::WeaponCount() || editor_missing[index]) {
        FatalityPreviewBridge::editor_texture.store(nullptr, std::memory_order_release); return;
    }
    if (!editor_models[index]) {
        const auto path = PreviewCatalog::weapons[index].model;
        if (!ModelExists(path)) { editor_missing[index] = true; LOG_WARN("weapon preview: missing %s", path); return; }
        phase = "allocate weapon model";
        void* ptr = Slot<void*(__thiscall*)(void*, size_t)>(Interfaces::MemAlloc, 1)(Interfaces::MemAlloc, 0x75c);
        if (!ptr) { editor_failed = true; return; }
        std::memset(ptr, 0, 0x75c);
        phase = "construct weapon model";
        reinterpret_cast<ConstructModelFn>(ctor)(static_cast<MergedModel*>(ptr));
        phase = "set weapon model";
        static_cast<MergedModel*>(ptr)->SetMDL(path);
        editor_models[index] = ptr;
        LOG_INFO("weapon preview: model loaded %s (default material only)", path);
    }
    model = editor_models[index];
    DrawTarget(editor_target, true);
}

}
void Paint() {
    if (failed || !FatalityPreviewBridge::visible.load(std::memory_order_relaxed)) return;
    __try { PaintImpl(); }
    __except (CaptureFault(GetExceptionInformation())) {
        failed = true; FatalityPreviewBridge::texture.store(nullptr, std::memory_order_release);
        RestoreAfterFault();
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
    if (!failed && !editor_failed && FatalityPreviewBridge::weapon_editor_visible.load(std::memory_order_relaxed)) {
        __try { PaintWeaponImpl(); }
        __except (CaptureFault(GetExceptionInformation())) {
            editor_failed = true;
            FatalityPreviewBridge::editor_texture.store(nullptr, std::memory_order_release);
            RestoreAfterFault();
            LOG_ERROR("weapon preview fault: phase=%s code=0x%08lX instruction=%p; weapon preview disabled", phase, fault.code, reinterpret_cast<void*>(fault.instruction));
        }
    }
}
void BeforeReset() {
    FatalityPreviewBridge::texture.store(nullptr, std::memory_order_release);
    FatalityPreviewBridge::editor_texture.store(nullptr, std::memory_order_release);
}
void Shutdown() {
    FatalityPreviewBridge::visible.store(false, std::memory_order_relaxed);
    FatalityPreviewBridge::texture.store(nullptr, std::memory_order_release);
    FatalityPreviewBridge::editor_texture.store(nullptr, std::memory_order_release);
    FatalityPreviewBridge::weapon_editor_visible.store(false, std::memory_order_relaxed);
    if (Interfaces::MemAlloc) {
        for (void*& item : editor_models) {
            if (item) Slot<void(__thiscall*)(void*, void*)>(Interfaces::MemAlloc, 5)(Interfaces::MemAlloc, item);
            item = nullptr;
        }
        for (size_t i = 0; i < model_count; ++i) {
            if (model_cache[i].model) Slot<void(__thiscall*)(void*, void*)>(Interfaces::MemAlloc, 5)(Interfaces::MemAlloc, model_cache[i].model);
            model_cache[i] = {};
        }
        model_count = 0;
        cache_limit_logged = false;
        std::memset(missing_models, 0, sizeof(missing_models));
    }
    if (render_texture && texture_retained) Slot<void(__thiscall*)(void*)>(render_texture, 11)(render_texture);
    texture_retained = false;
    if (editor_target && editor_target_retained) Slot<void(__thiscall*)(void*)>(editor_target, 11)(editor_target);
    editor_target = nullptr; editor_target_retained = false; editor_failed = false;
    std::memset(editor_missing, 0, sizeof(editor_missing));
    model = render_texture = cubemap = nullptr;
}
}
