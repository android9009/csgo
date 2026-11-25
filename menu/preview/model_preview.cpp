// Experimental Source 1 preview, based on Fatality's entity_rendering::paint.
// Do NOT include globals.hpp here - this preview runs on the shim SDK types
// (plain matrix3x4_t, studio types), which collide with the main tree's
// aligned/differently-laid-out definitions. The preamble provides the
// declarations hacks.hpp (via sdk/interfaces/interfaces.hpp) needs.
#include "../../sdk/hacks_preamble.hpp"
#include "model_preview.hpp"
#include "../../features/visuals/chams/chams_renderer.hpp"
#include "../../sdk/model_preview.hpp"
#include "../../sdk/model_preview_render.hpp"
#include "../../sdk/visual_preview_state.hpp"
#include "../../sdk/interfaces/interfaces.hpp"
#include "bridge.hpp"
#include "catalog.hpp"
#include "../../sdk/skin_icon_file.hpp"
#include "../../sdk/netvars.hpp"
#include "sequence.hpp"
#include "framing.hpp"
#include "subject.hpp"
#include "../../sdk/entity/entity.hpp"
#include "../../features/skins/skin_state.hpp"
#include "../../utils/log.hpp"
#include "../../utils/detourhook.hpp"
#include <array>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace ModelPreview {
namespace {
using namespace SourceModelPreview;
template<typename T> T Slot(void* p, int index) { return reinterpret_cast<T>((*reinterpret_cast<void***>(p))[index]); }
void* render_texture{};
void* cubemap{};
bool cubemap_retained{};
void* editor_target{};
void* editor_models[sizeof(PreviewCatalog::weapons)/sizeof(*PreviewCatalog::weapons)]{};
DWORD editor_material_refresh{};
int editor_material_model{-1}, editor_material_paint{-1}, editor_material_seed{-1};
float editor_material_wear{-1.f};
bool editor_missing[sizeof(PreviewCatalog::weapons)/sizeof(*PreviewCatalog::weapons)]{};
bool editor_failed{}, editor_target_retained{};
bool* editor_allocation_flag{};
bool editor_allocation_old{}, editor_allocation_begun{};
void* model{};
void* last_agent_model{};
// The engine destructor ABI is not verified. Keep a bounded cache instead of
// repeatedly destroying merged models while the pointer moves over the grid.
struct CachedModel { int team, agent; void* model; std::string path; };
CachedModel model_cache[128]{};
size_t model_count{};
bool cache_limit_logged{};
bool missing_models[2][(std::max)(sizeof(PreviewCatalog::ct)/sizeof(*PreviewCatalog::ct), sizeof(PreviewCatalog::t)/sizeof(*PreviewCatalog::t))]{};
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
    bool begun{}, target_pushed{}, view_pushed{}, lighting_suppressed{}, cubemap_bound{}, local_lights{};
};
RenderState active{};
constexpr int width = 235, height = 370;
// Observe only the synchronous preview draw; never retain engine bone pointers.
// IStudioRender::DrawModel slot/signature and DrawModelInfo prefix match the
// supplied Fatality IStudioRender.h / IVModelRender.h declarations.
VMT_Hook studio_observer;
thread_local const ViewSetup* capture_view{};
thread_local VisualPreview::Geometry* capture_geometry{};
struct StudioInfoPrefix {studiohdr_t* hdr;};
using StudioDrawFn=void(__thiscall*)(void*,void*,const StudioInfoPrefix&,matrix3x4_t*,float*,float*,const Vector&,int);
bool ProjectPreviewPoint(const ViewSetup& view,const Vector& point,VisualPreview::Point& out){
    constexpr float radians=.01745329252f;
    const float pitch=view.angles.x*radians,tangent=std::tan(view.fov*radians*.5f);
    const auto delta=point-view.origin;const float depth=delta.x*std::cos(pitch)-delta.z*std::sin(pitch);
    if(depth<=view.near_z)return false;
    out.x=.5f-delta.y/(2.f*depth*tangent);
    out.y=.5f-(delta.x*std::sin(pitch)+delta.z*std::cos(pitch))*(view.aspect_ratio>0.f ? view.aspect_ratio : static_cast<float>(view.width)/view.height)/(2.f*depth*tangent);
    return std::isfinite(out.x) && std::isfinite(out.y);
}
void ObserveBones(const StudioInfoPrefix& info,matrix3x4_t* matrices){
    if(!capture_view || !capture_geometry || !matrices || !info.hdr || capture_geometry->segments)return;
    auto* hdr=info.hdr;
    if(hdr->bones<20 || hdr->bones>256 || !std::strstr(hdr->name,"player") || std::strstr(hdr->name,"animset"))return;
    for(int i=0;i<hdr->bones && capture_geometry->segments<64;++i){
        const auto* bone=hdr->bone(i);if(!bone || !(bone->flags&0x100) || bone->parent<0 || bone->parent>=hdr->bones)continue;
        VisualPreview::Segment line;
        if(ProjectPreviewPoint(*capture_view,matrices[i].GetOrigin(),line.a) && ProjectPreviewPoint(*capture_view,matrices[bone->parent].GetOrigin(),line.b))capture_geometry->skeleton[capture_geometry->segments++]=line;
    }
    static bool logged=false;if(!logged && capture_geometry->segments){logged=true;LOG_INFO("preview skeleton: captured actual studio draw bones segments=%d",capture_geometry->segments);}
}
void __fastcall ObserveStudioDraw(void* self,void*,void* result,const StudioInfoPrefix& info,matrix3x4_t* matrices,float* flex,float* delayed,const Vector& origin,int flags){
    __try{ObserveBones(info,matrices);}__except(EXCEPTION_EXECUTE_HANDLER){}
    if(auto original=studio_observer.get_original<StudioDrawFn>())original(self,result,info,matrices,flex,delayed,origin,flags);
}


void RestoreTints();
void* UpdateWeaponMaterials(int index);

// Restore only state whose corresponding push/bind completed successfully.
// A second engine exception is contained; preview stays disabled afterwards.
void RestoreAfterFault() {
    __try {
        capture_view=nullptr;capture_geometry=nullptr;
        PreviewMaterial::Restore();
        RestoreTints(); // element tints are scoped to one preview draw
        if(active.local_lights && Interfaces::StudioRender)
            Slot<void(__thiscall*)(void*,int,void*)>(Interfaces::StudioRender,22)(Interfaces::StudioRender,0,nullptr);
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
    if(!studio_observer.is_hooked())LOG_INFO("preview skeleton observer attached=%d",studio_observer.attach(Interfaces::StudioRender,29,reinterpret_cast<void*>(&ObserveStudioDraw)));
    phase = "read material allocation flag";
    auto* allocation_disabled = reinterpret_cast<bool*>(reinterpret_cast<std::uintptr_t>(ms) + 0x9ec);
    const bool old = *allocation_disabled;
    *allocation_disabled = false;
    phase = "begin render target allocation";
    Slot<void(__thiscall*)(void*)>(ms, 94)(ms);
    phase = "get back buffer format";
    // IMAGE_FORMAT_RGBA8888 retains an alpha channel, unlike the back-buffer format.
    const int format = 0;
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
    for(const char* name:{"editor/cubemap.hdr","editor/cubemap","models/effects/cube_white"}){
        auto* candidate=Slot<void*(__thiscall*)(void*,const char*,const char*,bool,int)>(ms,91)(ms,name,"CubeMap textures",false,0);
        if(!candidate)continue;
        const auto* actual=Slot<const char*(__thiscall*)(void*)>(candidate,0)(candidate);
        if(!actual || std::strstr(actual,"error") || Slot<int(__thiscall*)(void*)>(candidate,3)(candidate)<=0)continue;
        cubemap=candidate;Slot<void(__thiscall*)(void*)>(cubemap,10)(cubemap);cubemap_retained=true;
        LOG_INFO("preview cubemap retained: %s",actual);break;
    }
    if(!cubemap)LOG_WARN("preview cubemap unavailable; reflections may differ from game");
    LOG_INFO("model preview render target initialized");
    return true;
}

bool ModelExists(const char* path) {
    auto* fs = Interfaces::BaseFileSystem;
    if (!fs) return true; // Preserve the previously working default when the filesystem is unavailable.
    static std::unordered_map<std::string,bool> known;
    if(const auto found=known.find(path);found!=known.end())return found->second;
    using Open = void* (__thiscall*)(void*, const char*, const char*, const char*);
    using Close = void (__thiscall*)(void*, void*);
    void* file = SkinIconFile::Slot<Open>(fs, 2)(fs, path, "rb", "GAME");
    if (!file) {known.emplace(path,false);return false;}
    SkinIconFile::Slot<Close>(fs, 3)(fs, file);
    known.emplace(path,true);return true;
}
const char* AvailableAgentPath(bool ct,int index) {
    const char* requested=PreviewCatalog::GetAgent(ct,static_cast<size_t>(index)).model;
    if (ModelExists(requested) || index != 0) return requested;
    // Default team models differ across legacy installations. Never substitute
    // an opposite-team model or silently replace a named premium agent.
    const char* ct_defaults[]={"models/player/custom_player/legacy/ctm_sas_variantf.mdl","models/player/ctm_sas.mdl"};
    const char* t_defaults[]={"models/player/custom_player/legacy/tm_phoenix.mdl","models/player/custom_player/legacy/tm_leet_variantf.mdl","models/player/tm_leet.mdl","models/player/tm_phoenix.mdl"};
    if (ct) { for (const auto* path : ct_defaults) if (ModelExists(path)) return path; }
    else { for (const auto* path : t_defaults) if (ModelExists(path)) return path; }
    return requested;
}
int AgentSequence() {
    static int sequence=-1;
    if (sequence<0) {
        const auto bytes=SkinIconFile::Read(Interfaces::BaseFileSystem,
            "models/player/custom_player/uiplayer/animset_uiplayer.mdl",32u*1024u*1024u);
        const int selected=PreviewSequence::Select(bytes,"unarmed");
        // Never fall back to 113: it is not a verified standing pose.
        // Sequence zero is the model's reference pose if no named standing
        // idle exists; do not choose an arbitrary seated cinematic instead.
        sequence=selected<0 ? 0 : selected;
        LOG_INFO("agent preview: standing empty-handed static pose sequence=%d matched=%d; weapon merging disabled",sequence,selected>=0 ? 1 : 0);
    }
    return sequence;
}
bool InitializeModel(bool ct, int agent_index, const char* agent) {
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
    LOG_INFO("model preview: loading %s model %s", ct ? "CT" : "T", agent);
    reinterpret_cast<SetMergedModelFn>(merged)(static_cast<MergedModel*>(model), agent, nullptr, nullptr, false);
    const auto root=reinterpret_cast<std::uintptr_t>(model)+4;
    *reinterpret_cast<int*>(root+0x54)=AgentSequence();
    *reinterpret_cast<float*>(root+0x60)=20.f;
    *reinterpret_cast<float*>(root+0x67c)=20.f;
    *reinterpret_cast<bool*>(root+0x675)=false;
    phase = "setup model bones";
    reinterpret_cast<SetupAttachmentBonesFn>(bones)(static_cast<MergedModel*>(model));
    model_cache[model_count++] = {team, agent_index, model, agent};
    LOG_INFO("model preview %s model initialized agent=%d unarmed/static (experimental Source 1)",ct ? "CT" : "T",agent_index);
    return true;
}

// --- Paint kit element recoloring -------------------------------------------
// IMaterial vtable slots in classic Source order. The runtime name checks in
// TintMaterial verify this layout against the running client before any
// parameter call; a mismatch disables the recolor path instead of guessing.
constexpr int mat_name = 23, mat_param_index = 49, mat_param_name = 50,
              mat_set_vector3 = 60, mat_set_vector4 = 61, mat_default_param = 63;
struct TintState { void* material{}; int param{}; bool vec4{}; float value[4]{}; };
std::vector<TintState> tint_state;
bool tint_failed{};
bool tint_skipped_logged{};
bool InClientModule(std::uintptr_t p) {
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA("client.dll"));
    if (!base) return false;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    return p >= base && p < base + nt->OptionalHeader.SizeOfImage;
}
void RestoreTints() {
    using SetV3Fn = void(__thiscall*)(void*, const char*, const Vector&);
    using SetV4Fn = void(__thiscall*)(void*, const char*, const Vector&, float);
    for (const auto& entry : tint_state) {
        auto** table = *reinterpret_cast<void***>(entry.material);
        if (!table) continue;
        if (entry.vec4)
            reinterpret_cast<SetV4Fn>(table[mat_set_vector4])(entry.material, "color",
                Vector{entry.value[0], entry.value[1], entry.value[2]}, entry.value[3]);
        else
            reinterpret_cast<SetV3Fn>(table[mat_set_vector3])(entry.material, "rgb",
                Vector{entry.value[0], entry.value[1], entry.value[2]});
    }
    tint_state.clear();
}
// Paint-kit element tint storage. The portrait weapon display that recoloring
// served is gone, but the wide editor strip still caches the live weapon's
// generated materials here and shutdown clears these buffers, so the storage
// stays even though nothing recolors from it anymore.
struct PendingTint { void* material{}; std::string expect_name; };
std::array<PendingTint, 8> pending_tints{};
int pending_tint_count{};
struct OwnerMaterialCache { void* materials[16]{}; int count{}; int paint{}; };
OwnerMaterialCache owner_material_cache[sizeof(PreviewCatalog::weapons) / sizeof(*PreviewCatalog::weapons)]{};
void DrawTarget(void* target, bool weapon_only, bool wide_weapon = true, int weapon_index = -1) {
    const bool fit_agent = !weapon_only && FatalityPreviewBridge::render_skins.load(std::memory_order_relaxed);
    Vector agent_min{-18.f, -18.f, 0.f}, agent_max{18.f, 18.f, 80.f};
    if (!weapon_only && Interfaces::ModelInfo) {
        const bool ct = FatalityPreviewBridge::render_ct.load(std::memory_order_relaxed);
        const int index = FatalityPreviewBridge::agent[ct ? 0 : 1].load(std::memory_order_relaxed);
        const auto* cached = static_cast<const CachedModel*>(nullptr);
        for(size_t i=0;i<model_count;++i)if(model_cache[i].model==model){cached=&model_cache[i];break;}
        const auto path = cached ? cached->path.c_str() : AvailableAgentPath(ct, index);
        const int model_index = Interfaces::ModelInfo->GetModelIndex(path);
        auto* source = model_index >= 0 ? Interfaces::ModelInfo->GetModel(model_index) : nullptr;
        auto* hdr = source ? Interfaces::ModelInfo->GetStudioModel(source) : nullptr;
        if (hdr) {
            const auto lo = hdr->bb_min, hi = hdr->bb_max;
            if (std::isfinite(lo.x) && std::isfinite(lo.y) && std::isfinite(lo.z) &&
                std::isfinite(hi.x) && std::isfinite(hi.y) && std::isfinite(hi.z) &&
                hi.x > lo.x && hi.y > lo.y && hi.z - lo.z >= 60.f && hi.z - lo.z <= 110.f &&
                hi.x - lo.x < 120.f && hi.y - lo.y < 120.f) {
                agent_min = lo; agent_max = hi;
            }
        }
    }
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
    // wide_weapon = the dormant 452x128 editor strip; portrait = the SKINS
    // tab's 235x370 PREVIEW viewport, where the weapon fills the width.
    view.width = weapon_only ? (wide_weapon ? 452 : width) : width;
    view.height = weapon_only ? (wide_weapon ? 128 : height) : height;
    Vector center{}; float radius = 32.f;
    if (weapon_only) {
        view.aspect_ratio = static_cast<float>(view.width) / view.height;
        Vector lo{}, hi{}; bool have_bounds = false;
        if (weapon_index >= 0 && weapon_index < static_cast<int>(PreviewCatalog::WeaponCount()) && Interfaces::ModelInfo) {
            const int model_index = Interfaces::ModelInfo->GetModelIndex(PreviewCatalog::weapons[weapon_index].model);
            auto* source = model_index >= 0 ? Interfaces::ModelInfo->GetModel(model_index) : nullptr;
            auto* hdr = source ? Interfaces::ModelInfo->GetStudioModel(source) : nullptr;
            if (hdr) {
                const auto a = hdr->bb_min, b = hdr->bb_max;
                if (std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z) &&
                    std::isfinite(b.x) && std::isfinite(b.y) && std::isfinite(b.z) &&
                    b.x > a.x && b.y > a.y && b.z > a.z &&
                    b.x - a.x < 300.f && b.y - a.y < 300.f && b.z - a.z < 300.f) { lo = a; hi = b; have_bounds = true; }
            }
        }
        if (have_bounds) {
            // Same framing family as the agent display: aim at the bounding
            // box center and sit far enough back that the full model fits
            // through a 360-degree turntable rotation. The narrow FOV keeps
            // perspective stable, so the long barrel does not stretch toward
            // and away from the camera while it turns (the old 50-degree
            // sphere framing made the weapon fly around and jitter).
            center = Vector{(lo.x + hi.x) * .5f, (lo.y + hi.y) * .5f, (lo.z + hi.z) * .5f};
            const float half_x = (hi.x - lo.x) * .5f, half_y = (hi.y - lo.y) * .5f, half_z = (hi.z - lo.z) * .5f;
            view.fov = 12.f;
            const float distance = PreviewFraming::TurntableDistance(half_x, half_y, half_z, view.aspect_ratio, 12.f, .9f);
            view.origin = Vector{-distance, 0.f, 0.f};
        } else {
            phase = "weapon bounding sphere";
            if (!static_cast<MergedModel*>(model)->BoundingSphere(center, radius) || !std::isfinite(radius) || radius < 1.f || radius > 200.f ||
                !std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z)) { center = {}; radius = 32.f; }
            // The weapon is not in the precache (no studio-header bounds), so
            // fall back to its bounding sphere but keep the SAME turntable
            // framing family as the box path: narrow FOV and a distance that
            // covers the full 360-degree rotation about the sphere center. The
            // old wide-FOV sphere framing let the long barrel sweep toward and
            // away from the camera as it turned, which read as the weapon
            // sliding around the frame instead of spinning in place.
            view.fov = 12.f;
            const float distance = PreviewFraming::TurntableDistance(radius, radius, radius, view.aspect_ratio, 12.f, .9f);
            view.origin = Vector{-distance, 0.f, 0.f};
        }
        view.angles = QAngle{0.f, 0.f, 0.f}; view.near_z = 1.f; view.far_z = 4000.f;
    } else {
        // Keep the model root on a fixed turntable axis. Asymmetric studio
        // bounds must not move that axis around when yaw changes.
        center = Vector{0.f,0.f,(agent_min.z + agent_max.z) * .5f};
        view.aspect_ratio = fit_agent ? FatalityPreviewBridge::skins_aspect.load(std::memory_order_relaxed) : FatalityPreviewBridge::visuals_aspect.load(std::memory_order_relaxed);
        if (!std::isfinite(view.aspect_ratio) || view.aspect_ratio < .2f || view.aspect_ratio > 4.f)
            view.aspect_ratio = static_cast<float>(width) / height;
        const float distance = PreviewFraming::TurntableDistance(
            (std::max)(std::abs(agent_min.x),std::abs(agent_max.x)),
            (std::max)(std::abs(agent_min.y),std::abs(agent_max.y)),
            (agent_max.z-agent_min.z)*.5f,view.aspect_ratio,12.f,fit_agent ? .94f : .80f);
        view.fov = 12.f; view.origin = Vector{-distance,0.f,fit_agent ? 0.f : -(agent_max.z-agent_min.z)*.04f};
        view.angles = QAngle{0.f, 0.f, 0.f}; view.near_z = 1.f; view.far_z = 2000.f;
    }
    view.flags = 4; // bloom and tone mapping, as in Fatality
    auto* rv = reinterpret_cast<RenderViewABI*>(Interfaces::RenderView);
    active.view = rv;
    phase = "push 3D view";
    rv->Push3DView(context, view, clear_color_depth_stencil, static_cast<Texture*>(target), active.frustum);
    active.view_pushed = true;
    phase = "clear color";
    context->ClearColor4ub(0, 0, 0, 0);
    phase = "clear buffers";
    context->ClearBuffers(true, true, true);
    struct Light { float x,y,z,w; };
    const auto appearance=VisualPreview::GetMaterial();
    const bool studio_chams=!weapon_only && !fit_agent && appearance.enabled && appearance.kind>0;
    Light light[6];for(auto& l:light)l={.68f,1.53f,1.7f,1.f};
    if(studio_chams){
        // Directional ambient cube: readable normal shading instead of a
        // uniform overbright cyan fill which made every shader look unlit.
        const Light neutral[6]={{.22f,.23f,.26f,1.f},{.16f,.17f,.20f,1.f},
            {.19f,.20f,.23f,1.f},{.11f,.12f,.15f,1.f},{.32f,.32f,.34f,1.f},{.07f,.08f,.10f,1.f}};
        std::memcpy(light,neutral,sizeof(light));
    }
    phase = "set ambient light";
    context->SetAmbientLightCube(light);
    phase = "set studio ambient light";
    Slot<void(__thiscall*)(void*, const Light*)>(Interfaces::StudioRender, 20)(Interfaces::StudioRender, light);
    phase = "set studio local lights";
    Slot<void(__thiscall*)(void*, int, void*)>(Interfaces::StudioRender, 22)(Interfaces::StudioRender, 0, nullptr);
    // Source LightDesc_t (IVModelRender.h); the entire descriptor, including
    // attenuation optimization fields, must be present for SetLocalLights.
    struct StudioLight {
        int type{};Vector color{},position{},direction{};
        float range{},falloff{},attenuation0{1.f},attenuation1{},attenuation2{},theta{},phi{};
        float theta_dot{},phi_dot{},one_over_dot{};unsigned flags{};float range_squared{};
    };
    static_assert(sizeof(StudioLight)==88,"CS:GO x86 LightDesc_t ABI");
    StudioLight keys[2];
    if(studio_chams){
        keys[0].type=2;keys[0].color={1.1f,1.02f,.92f};keys[0].direction={.707107f,.0f,-.707107f};
        keys[1].type=2;keys[1].color={.25f,.32f,.45f};keys[1].direction={-.6f,.8f,0.f};
        Slot<void(__thiscall*)(void*,int,const StudioLight*)>(Interfaces::StudioRender,22)(Interfaces::StudioRender,2,keys);
        active.local_lights=true;
    }
    constexpr float to_rad = 0.01745329252f;
    const bool editor_cam = weapon_only && wide_weapon;
    const float yaw = (editor_cam ? FatalityPreviewBridge::editor_yaw.load(std::memory_order_relaxed)
                                  : FatalityPreviewBridge::yaw.load(std::memory_order_relaxed)) * to_rad;
    const float pitch = (editor_cam ? FatalityPreviewBridge::editor_pitch.load(std::memory_order_relaxed) : 0.f) * to_rad;
    const float roll = (editor_cam ? FatalityPreviewBridge::editor_roll.load(std::memory_order_relaxed) : 0.f) * to_rad;
    const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch), cr = std::cos(roll), sr = std::sin(roll);
    matrix3x4_t matrix{};
    matrix[0][0]=cy*cp; matrix[0][1]=cy*sp*sr-sy*cr; matrix[0][2]=cy*sp*cr+sy*sr;
    matrix[1][0]=sy*cp; matrix[1][1]=sy*sp*sr+cy*cr; matrix[1][2]=sy*sp*cr-cy*sr;
    matrix[2][0]=-sp; matrix[2][1]=cp*sr; matrix[2][2]=cp*cr;
    for (int i=0; i<3; ++i) matrix[i][3] = -(matrix[i][0]*center.x + matrix[i][1]*center.y + matrix[i][2]*center.z);
    if (editor_cam) {
        // Normalized screen drag; camera looks along +X, so Y/Z span its image plane.
        matrix[1][3] -= FatalityPreviewBridge::editor_pan_x.load(std::memory_order_relaxed) * radius * 2.f;
        matrix[2][3] -= FatalityPreviewBridge::editor_pan_y.load(std::memory_order_relaxed) * radius * 2.f;
    }
    VisualPreview::Geometry projected{};
    if (!weapon_only && !fit_agent) {
        projected.box = {1.f,1.f,0.f,0.f};
        constexpr float radians = 0.01745329252f;
        const float pitch_angle=view.angles.x*radians;
        const float tangent=std::tan(view.fov*radians*.5f);
        const float aspect=view.aspect_ratio>0.f ? view.aspect_ratio : static_cast<float>(view.width)/view.height;
        bool valid=true;
        for(int corner=0;corner<8;++corner) {
            const Vector local{corner&1 ? agent_max.x : agent_min.x,corner&2 ? agent_max.y : agent_min.y,corner&4 ? agent_max.z : agent_min.z};
            Vector world;
            for(int axis=0;axis<3;++axis)world[axis]=matrix[axis][0]*local.x+matrix[axis][1]*local.y+matrix[axis][2]*local.z+matrix[axis][3];
            const auto delta=world-view.origin;
            const float depth=delta.x*std::cos(pitch_angle)-delta.z*std::sin(pitch_angle);
            if(depth<=view.near_z){valid=false;break;}
            const float x=.5f-delta.y/(2.f*depth*tangent);
            const float y=.5f-(delta.x*std::sin(pitch_angle)+delta.z*std::cos(pitch_angle))*aspect/(2.f*depth*tangent);
            projected.box.left=(std::min)(projected.box.left,x);projected.box.right=(std::max)(projected.box.right,x);
            projected.box.top=(std::min)(projected.box.top,y);projected.box.bottom=(std::max)(projected.box.bottom,y);
        }
        projected.valid=valid;
        capture_view=&view;capture_geometry=&projected;
    }
    // Intentionally frozen: no weapon-dependent pose or idle/intro playback.
    auto* mr = Interfaces::ModelRender;
    phase = "suppress engine lighting";
    Slot<void(__thiscall*)(void*, bool)>(mr, 24)(mr, true);
    active.lighting_suppressed = true;
    phase = "draw merged model";
    if (!weapon_only && !fit_agent && appearance.enabled && appearance.kind > 0) {
        phase = "apply preview chams";
        if(!PreviewMaterial::Draw(appearance,false,[&](){PreviewMaterial::OverrideStudio();static_cast<MergedModel*>(model)->Draw(matrix);}))
            static_cast<MergedModel*>(model)->Draw(matrix);
    } else static_cast<MergedModel*>(model)->Draw(matrix);
    if(!weapon_only && !fit_agent){capture_view=nullptr;capture_geometry=nullptr;VisualPreview::SetGeometry(projected);}
    if(active.local_lights){Slot<void(__thiscall*)(void*,int,void*)>(Interfaces::StudioRender,22)(Interfaces::StudioRender,0,nullptr);active.local_lights=false;}
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
    // The portrait SKINS-tab display stores into the main preview texture;
    // the wide editor strip keeps its own.
    if (weapon_only && wide_weapon) FatalityPreviewBridge::editor_texture.store(texture, std::memory_order_release);
    else FatalityPreviewBridge::texture.store(texture, std::memory_order_release);
}

bool redraw_preview=true;
// The display weapon is merged cheat-side, so the engine never poses it.
// Pin the sequence and frame time the same way the agent preview does: with
// the zero-initialized animation state left alone the model pose drifts
// between draws and the display flies around and jitters.
void PinStaticPose(void* merged_model) {
    if (!merged_model) return;
    const auto root = reinterpret_cast<std::uintptr_t>(merged_model) + 4;
    *reinterpret_cast<int*>(root + 0x54) = 0;
    *reinterpret_cast<float*>(root + 0x60) = 20.f;
    *reinterpret_cast<bool*>(root + 0x675) = false;
    *reinterpret_cast<float*>(root + 0x67c) = 20.f;
    if (bones) reinterpret_cast<SetupAttachmentBonesFn>(bones)(static_cast<MergedModel*>(merged_model));
}
void PaintImpl() {
    if (!render_texture && !Initialize()) { failed = true; LOG_WARN("model preview initialization failed"); return; }
    const bool skins = FatalityPreviewBridge::render_skins.load(std::memory_order_relaxed);
    // The SKINS-tab preview always shows the agent. The weapon portrait display
    // was removed: rendering the selected weapon into the main viewport read as
    // a separate "weapon preview" and the user wants the agent in it at all
    // times. The wide editor strip (its own texture) is unaffected.
    bool ct = FatalityPreviewBridge::render_ct.load(std::memory_order_relaxed);
    std::string local_path;
    if (!skins) {
        const int subject = FatalityPreviewBridge::visuals_subject.load(std::memory_order_relaxed);
        CBaseEntity* local = nullptr;
        if (Interfaces::Engine && Interfaces::Engine->IsInGame() && Interfaces::EntityList)
            local = static_cast<CBaseEntity*>(Interfaces::EntityList->GetClientEntity(Interfaces::Engine->GetLocalPlayer()));
        const int local_team = local ? local->m_iTeamNum() : 0;
        ct = PreviewSubject::CounterTerrorist(local_team, subject);
        if (subject == 2 && (local_team == 2 || local_team == 3) && Interfaces::ModelInfo) {
            const auto* path = local->GetModelName();
            // Copy engine-owned text; never retain an entity/model pointer.
            if (path && PreviewSubject::PlayerModel(path)) local_path = path;
        }
    }
    const int team = ct ? 0 : 1;
    int agent_index = skins ? FatalityPreviewBridge::agent[team].load(std::memory_order_relaxed) : 0;
    if (agent_index < 0 || static_cast<size_t>(agent_index) >= PreviewCatalog::AgentCount(ct)) agent_index = 0;
    std::string agent_path = local_path.empty() ? AvailableAgentPath(ct, agent_index) : local_path;
    // Missing live models fall back to a valid model on the same side.
    if (!local_path.empty() && !ModelExists(agent_path.c_str())) {
        local_path.clear(); agent_path = AvailableAgentPath(ct, 0);
    }
    model = nullptr;
    for (size_t i = 0; i < model_count; ++i)
        if (model_cache[i].team == team && model_cache[i].path == agent_path) { model = model_cache[i].model; break; }
    if (!model) {
        // Check availability before asking the engine to merge a potentially absent model.
        if (local_path.empty() && missing_models[team][agent_index]) {
            FatalityPreviewBridge::texture.store(nullptr, std::memory_order_release);
            return;
        }
        if (!ModelExists(agent_path.c_str())) {
            if (local_path.empty()) missing_models[team][agent_index] = true;
            LOG_WARN("model preview: requested agent=%d is unavailable",agent_index);
            FatalityPreviewBridge::texture.store(nullptr, std::memory_order_release);
            return;
        }
        if (model_count >= sizeof(model_cache)/sizeof(*model_cache)) {
            if (!cache_limit_logged) { LOG_WARN("model preview: model cache limit reached; restart required"); cache_limit_logged = true; }
            FatalityPreviewBridge::texture.store(nullptr, std::memory_order_release);
            return;
        }
        if (!InitializeModel(ct, agent_index, agent_path.c_str())) { failed = true; LOG_WARN("model preview model initialization failed"); return; }
    }
    if (model != last_agent_model) {
        // Each cached combination has its own merged model. Reset the known
        // Fatality sequence and playback basis when switching, before drawing.
        const auto root = reinterpret_cast<std::uintptr_t>(model) + 4;
        *reinterpret_cast<int*>(root + 0x54) = AgentSequence();
        *reinterpret_cast<float*>(root + 0x60) = 20.f;
        *reinterpret_cast<bool*>(root + 0x675) = false;
        *reinterpret_cast<float*>(root + 0x67c) = 20.f;
        reinterpret_cast<SetupAttachmentBonesFn>(bones)(static_cast<MergedModel*>(model));
        last_agent_model = model;
    }
    static void* drawn_model{};
    static float drawn_yaw{};
    static bool drawn_skins{};
    static float drawn_aspect{};
    static VisualPreview::Material drawn_material{};
    static DWORD last_draw{};
    const float yaw=FatalityPreviewBridge::yaw.load(std::memory_order_relaxed);
    const float aspect=skins ? FatalityPreviewBridge::skins_aspect.load(std::memory_order_relaxed) : FatalityPreviewBridge::visuals_aspect.load(std::memory_order_relaxed);
    const auto material=VisualPreview::GetMaterial();
    if(!redraw_preview && drawn_model==model && drawn_yaw==yaw && drawn_skins==skins &&
       drawn_aspect==aspect && drawn_material==material && !(material.enabled && material.kind==5 && GetTickCount()-last_draw>=33) && FatalityPreviewBridge::texture.load(std::memory_order_acquire))return;
    DrawTarget(render_texture, false);
    drawn_model=model;drawn_yaw=yaw;drawn_skins=skins;drawn_aspect=aspect;drawn_material=material;last_draw=GetTickCount();redraw_preview=false;
}
// Experimental read-only bridge: reuse an existing game's generated material owner.
// No synthetic CEconItemView, no generated-material calls, no live inventory mutation.
// SetMDL's owner path duplicates references into the preview's own CMDL. This was
// disassembled in the supplied client: RVA 0x9aebc2 -> owner virtual slot 3.
bool LiveMaterialBuildCompatible() {
    auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleA("client.dll"));
    if (!base) return false;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 || nt->FileHeader.TimeDateStamp != 0x65272025 ||
        nt->OptionalHeader.SizeOfImage <= 0xd139a8) return false;
    const unsigned char copy_call[] = {0x85,0xc9,0x74,0x06,0x8b,0x01,0x53,0xff,0x50,0x0c};
    return std::memcmp(base + 0x9aebc2, copy_call, sizeof(copy_call)) == 0;
}
// Reason codes: 1 outside game, 2 netvars, 3 unsupported definition,
// 4 local player, 5 no matching item, 6 owner ABI, 7 generated materials absent.
void* FindLiveMaterialOwner(int index, int paint, int seed, float wear, int* reason=nullptr) {
    if(reason) *reason=1;
    if (!Interfaces::Engine || !Interfaces::EntityList || !Interfaces::Engine->IsInGame() ||
        !PropManager::Get().IsReady()) return nullptr;
    if(reason) *reason=2;
    auto& net = PropManager::Get();
    const auto weapons = net.GetOffset("DT_BaseCombatCharacter", "m_hMyWeapons");
    const auto item = net.GetOffset("DT_BaseAttributableItem", "m_Item");
    const auto definition = net.GetOffset("DT_BaseAttributableItem", "m_iItemDefinitionIndex");
    const auto paint_offset = net.GetOffset("DT_BaseAttributableItem", "m_nFallbackPaintKit");
    const auto seed_offset = net.GetOffset("DT_BaseAttributableItem", "m_nFallbackSeed");
    const auto wear_offset = net.GetOffset("DT_BaseAttributableItem", "m_flFallbackWear");
    for (const auto offset : {weapons,item,definition,paint_offset,seed_offset,wear_offset})
        if (offset <= 0 || offset > 0x10000) return nullptr;
    const int wanted = SkinState::Definition(PreviewCatalog::weapons[index].id);
    if(reason) *reason=3;
    if (!wanted) return nullptr;
    if(reason) *reason=4;
    auto* local = static_cast<unsigned char*>(Interfaces::EntityList->GetClientEntity(Interfaces::Engine->GetLocalPlayer()));
    if (!local) return nullptr;
    if(reason) *reason=5;
    const auto* handles = reinterpret_cast<const std::uint32_t*>(local + weapons);
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA("client.dll"));
    for (int i=0; i<64; ++i) {
        if (!handles[i] || handles[i] == 0xffffffffu) continue;
        auto* entity = static_cast<unsigned char*>(Interfaces::EntityList->GetClientEntityFromHandle(handles[i]));
        if (!entity || *reinterpret_cast<short*>(entity + definition) != wanted ||
            *reinterpret_cast<int*>(entity + paint_offset) != paint ||
            *reinterpret_cast<int*>(entity + seed_offset) != seed ||
            std::fabs(*reinterpret_cast<float*>(entity + wear_offset) - wear) > .00001f) continue;
        if(reason) *reason=6;
        auto* owner = entity + item + 0x10;
        const auto* table = *reinterpret_cast<std::uintptr_t**>(owner);
        // Confirm inherited material-owner operations, not merely a plausible pointer.
        if (!table || table[1] != base + 0x1de6f0 || table[3] != base + 0x1ac360) continue;
        if(reason) *reason=7;
        const int count = *reinterpret_cast<int*>(owner + 0x10);
        auto** materials = *reinterpret_cast<void***>(owner + 4);
        if (count <= 0 || count > 32 || !materials) continue;
        bool any = false;
        for (int slot=0; slot<count; ++slot) any = any || materials[slot] != nullptr;
        if (any) { if(reason) *reason=0; return owner; } // Never cached beyond this render call.
    }
    return nullptr;
}
// Returns the live material owner (only valid for this frame) so callers can
// reuse its generated materials for the element tints.
void* UpdateWeaponMaterials(int index) {
    const auto now = GetTickCount();
    const int paint = FatalityPreviewBridge::editor_paint.load(std::memory_order_relaxed);
    const int seed = FatalityPreviewBridge::editor_seed.load(std::memory_order_relaxed);
    const float wear = FatalityPreviewBridge::editor_wear.load(std::memory_order_relaxed);
    const bool changed = editor_material_model != index || editor_material_paint != paint ||
        editor_material_seed != seed || editor_material_wear != wear;
    if (!changed && now - editor_material_refresh < 500) return nullptr;
    editor_material_model = index; editor_material_refresh = now;
    editor_material_paint = paint; editor_material_seed = seed; editor_material_wear = wear;
    auto* preview = static_cast<MergedModel*>(editor_models[index]);
    void* owner = nullptr;
    int status = 0;
    if (paint > 0) {
        status = 3;
        if (LiveMaterialBuildCompatible()) {
            status = 1;
            owner = FindLiveMaterialOwner(index, paint, seed, wear);
            if (owner) status = 2;
        }
    }
    // Rebinding also clears the previous copied finish when the request no longer matches.
    const auto* table = *reinterpret_cast<std::uintptr_t**>(preview);
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA("client.dll"));
    if (owner && table[4] != base + 0x9aec30) { owner = nullptr; status = 3; }
    // SetMDL without an owner does not clear old custom materials. Release only
    // the preview-owned references, using the pinned build's clear operation.
    if (LiveMaterialBuildCompatible() && table[4] == base + 0x9aec30) {
        using ClearFn = void(__thiscall*)(void*, bool);
        reinterpret_cast<ClearFn>(base + 0x1acc80)(reinterpret_cast<unsigned char*>(preview) + 4, false);
    }
    preview->SetMDL(PreviewCatalog::weapons[index].model, owner, nullptr);
    PinStaticPose(preview); // re-merge resets the pose; keep the display static
    const int old = FatalityPreviewBridge::editor_material_status.exchange(status, std::memory_order_relaxed);
    if (old != status) LOG_INFO("weapon preview: experimental live material status=%d paint=%d", status, paint);
    // Cache the generated material pointers (engine-stable) for the tints.
    auto& cache = owner_material_cache[index];
    cache.count = 0;
    cache.paint = paint;
    if (owner) {
        auto* bytes = static_cast<unsigned char*>(owner);
        const int material_count = *reinterpret_cast<int*>(bytes + 0x10);
        auto** materials = *reinterpret_cast<void***>(bytes + 4);
        if (material_count > 0 && material_count <= 16 && materials) {
            for (int i = 0; i < material_count; ++i) {
                void* m = materials[i];
                if (!m) continue;
                auto** mtable = *reinterpret_cast<void***>(m);
                if (!mtable || !InClientModule(reinterpret_cast<std::uintptr_t>(mtable[mat_name]))) continue;
                cache.materials[cache.count++] = m;
            }
        }
    }
    return owner;
}
void PaintWeaponImpl() {
    if (!render_texture) return; // normal preview owns common initialization
    if (!editor_target) {
        auto* ms = Interfaces::MaterialSystem;
        phase = "weapon target allocation";
        auto* disabled = reinterpret_cast<bool*>(reinterpret_cast<std::uintptr_t>(ms) + 0x9ec);
        editor_allocation_old = *disabled; editor_allocation_flag = disabled; *disabled = false;
        Slot<void(__thiscall*)(void*)>(ms, 94)(ms); editor_allocation_begun = true;
        const int format = 0; // IMAGE_FORMAT_RGBA8888: preserve transparent background.
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
        PinStaticPose(ptr);
        editor_models[index] = ptr;
        LOG_INFO("weapon preview: model loaded %s (default material only)", path);
    }
    phase = "copy live weapon materials (experimental)";
    UpdateWeaponMaterials(index);
    model = editor_models[index];
    DrawTarget(editor_target, true, true, index);
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
    redraw_preview=true;
    VisualPreview::SetGeometry({});
    FatalityPreviewBridge::texture.store(nullptr, std::memory_order_release);
    FatalityPreviewBridge::editor_texture.store(nullptr, std::memory_order_release);
}
void Shutdown() {
    studio_observer.detach();
    VisualPreview::SetGeometry({});
    redraw_preview=true;
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
        last_agent_model = nullptr;
    }
    if (render_texture && texture_retained) Slot<void(__thiscall*)(void*)>(render_texture, 11)(render_texture);
    texture_retained = false;
    if(cubemap && cubemap_retained)Slot<void(__thiscall*)(void*)>(cubemap,11)(cubemap);
    cubemap_retained=false;
    if (editor_target && editor_target_retained) Slot<void(__thiscall*)(void*)>(editor_target, 11)(editor_target);
    editor_target = nullptr; editor_target_retained = false; editor_failed = false;
    tint_state.clear();
    tint_failed = false;
    tint_skipped_logged = false;
    pending_tints = {};
    pending_tint_count = 0;
    for (auto& cache : owner_material_cache) cache = OwnerMaterialCache{};
    editor_material_refresh = 0; editor_material_model = -1;
    FatalityPreviewBridge::editor_material_status.store(0, std::memory_order_relaxed);
    std::memset(editor_missing, 0, sizeof(editor_missing));
    model = render_texture = cubemap = nullptr;
}
}
