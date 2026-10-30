#include "agent_preview.hpp"

// Keep Win32/D3D declarations ahead of menu.hpp's standard-library namespace imports.
#include <Windows.h>
#include <d3d9.h>

#include "menu.hpp"
#include "../sdk/config_system/config_system.hpp"
#include "../sdk/source_preview.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>

namespace {
using source_preview::C_MergedMDL;
using source_preview::ITexture;

constexpr int k_preview_width = 235;
constexpr int k_preview_height = 370;
constexpr std::size_t k_merged_mdl_size = 0x75C;

constexpr const char* k_terrorist_models[] = {
    "models/player/custom_player/legacy/tm_phoenix.mdl",
    "models/player/custom_player/legacy/tm_anarchist_varianta.mdl",
    "models/player/custom_player/legacy/tm_anarchist_variantb.mdl",
    "models/player/custom_player/legacy/tm_anarchist_variantc.mdl",
    "models/player/custom_player/legacy/tm_anarchist_variantd.mdl",
    "models/player/custom_player/legacy/tm_balkan_varianta.mdl",
    "models/player/custom_player/legacy/tm_balkan_variantb.mdl",
    "models/player/custom_player/legacy/tm_balkan_variantc.mdl",
    "models/player/custom_player/legacy/tm_balkan_variantd.mdl",
    "models/player/custom_player/legacy/tm_balkan_variante.mdl",
    "models/player/custom_player/legacy/tm_balkan_variantf.mdl",
    "models/player/custom_player/legacy/tm_balkan_variantg.mdl",
    "models/player/custom_player/legacy/tm_balkan_varianth.mdl",
    "models/player/custom_player/legacy/tm_balkan_varianti.mdl",
    "models/player/custom_player/legacy/tm_balkan_variantj.mdl",
    "models/player/custom_player/legacy/tm_balkan_variantk.mdl",
    "models/player/custom_player/legacy/tm_balkan_variantl.mdl",
    "models/player/custom_player/legacy/tm_leet_varianta.mdl",
    "models/player/custom_player/legacy/tm_leet_variantb.mdl",
    "models/player/custom_player/legacy/tm_leet_variantc.mdl",
    "models/player/custom_player/legacy/tm_leet_variantd.mdl",
};

constexpr const char* k_counter_terrorist_models[] = {
    "models/player/custom_player/legacy/ctm_sas.mdl",
    "models/player/custom_player/legacy/ctm_fbi_varianta.mdl",
    "models/player/custom_player/legacy/ctm_fbi_variantb.mdl",
    "models/player/custom_player/legacy/ctm_fbi_variantc.mdl",
    "models/player/custom_player/legacy/ctm_fbi_variantd.mdl",
    "models/player/custom_player/legacy/ctm_fbi_variante.mdl",
    "models/player/custom_player/legacy/ctm_fbi_variantf.mdl",
    "models/player/custom_player/legacy/ctm_fbi_variantg.mdl",
    "models/player/custom_player/legacy/ctm_fbi_varianth.mdl",
    "models/player/custom_player/legacy/ctm_gign_varianta.mdl",
    "models/player/custom_player/legacy/ctm_gign_variantb.mdl",
    "models/player/custom_player/legacy/ctm_gign_variantc.mdl",
    "models/player/custom_player/legacy/ctm_gign_variantd.mdl",
    "models/player/custom_player/legacy/ctm_gsg9_varianta.mdl",
    "models/player/custom_player/legacy/ctm_gsg9_variantb.mdl",
    "models/player/custom_player/legacy/ctm_gsg9_variantc.mdl",
    "models/player/custom_player/legacy/ctm_gsg9_variantd.mdl",
    "models/player/custom_player/legacy/ctm_st6_varianta.mdl",
    "models/player/custom_player/legacy/ctm_st6_variantb.mdl",
    "models/player/custom_player/legacy/ctm_st6_variantc.mdl",
    "models/player/custom_player/legacy/ctm_st6_variantd.mdl",
};

static_assert(IM_ARRAYSIZE(k_terrorist_models) == 21);
static_assert(IM_ARRAYSIZE(k_counter_terrorist_models) == 21);

struct preview_state {
    std::mutex mutex;
    // Owned only between draw() and end_frame(), while ImGui consumes the raw texture.
    std::unique_lock<std::mutex> display_lock;

    bool requested_active = false;
    int requested_side = 0;
    int requested_agent = 0;

    ITexture* preview_texture = nullptr;
    ITexture* cubemap_texture = nullptr;
    C_MergedMDL* preview_model = nullptr;
    int model_side = -1;
    int model_agent = -1;
    int rendered_side = -1;
    int rendered_agent = -1;

    IDirect3DTexture9* display_texture = nullptr;
};

preview_state& state() {
    static preview_state value;
    return value;
}

int selected_agent(int side) {
    const int selected = side == 0
        ? g_cfg->skins.m_agents.m_selected
        : g_cfg->skins.m_agents.m_selected_ct;
    return std::clamp(selected, 0, static_cast<int>(IM_ARRAYSIZE(k_terrorist_models)) - 1);
}

const char* agent_model(int side, int agent) {
    return side == 0 ? k_terrorist_models[agent] : k_counter_terrorist_models[agent];
}

void release_display_locked(preview_state& s) {
    if (s.display_texture) {
        s.display_texture->Release();
        s.display_texture = nullptr;
    }
}

void release_display_and_unlock(preview_state& s) {
    release_display_locked(s);
    if (s.display_lock.owns_lock())
        s.display_lock.unlock();
    s.display_lock = std::unique_lock<std::mutex>{};
}

void release_model_locked(preview_state& s) {
    if (s.preview_model) {
        if (const auto allocator = source_preview::memalloc())
            allocator->Free(s.preview_model);
        s.preview_model = nullptr;
    }
    s.model_side = -1;
    s.model_agent = -1;
    s.rendered_side = -1;
    s.rendered_agent = -1;
}

void release_texture_locked(ITexture*& texture) {
    if (texture) {
        texture->DecrementReferenceCount();
        texture = nullptr;
    }
}

class render_context_scope {
public:
    explicit render_context_scope(source_preview::IMaterialSystem* material_system)
        : context_(material_system ? material_system->GetRenderContext() : nullptr) {
        if (context_)
            context_->BeginRender();
    }

    render_context_scope(const render_context_scope&) = delete;
    render_context_scope& operator=(const render_context_scope&) = delete;

    ~render_context_scope() {
        if (context_) {
            context_->EndRender();
            context_->Release();
        }
    }

    source_preview::IMatRenderContext* get() const { return context_; }
    explicit operator bool() const { return context_ != nullptr; }

private:
    source_preview::IMatRenderContext* context_ = nullptr;
};

void angle_matrix(const source_preview::QAngle& angles, source_preview::matrix3x4_t& matrix) {
    constexpr float pi = 3.14159265358979323846f;
    const auto radians = [pi](float degrees) { return degrees * (pi / 180.f); };
    const float sp = std::sin(radians(angles.x));
    const float cp = std::cos(radians(angles.x));
    const float sy = std::sin(radians(angles.y));
    const float cy = std::cos(radians(angles.y));
    const float sr = std::sin(radians(angles.z));
    const float cr = std::cos(radians(angles.z));

    matrix[0][0] = cp * cy;
    matrix[1][0] = cp * sy;
    matrix[2][0] = -sp;
    matrix[0][1] = sr * sp * cy + cr * -sy;
    matrix[1][1] = sr * sp * sy + cr * cy;
    matrix[2][1] = sr * cp;
    matrix[0][2] = cr * sp * cy + -sr * -sy;
    matrix[1][2] = cr * sp * sy + -sr * cy;
    matrix[2][2] = cr * cp;
    matrix[0][3] = 0.f;
    matrix[1][3] = 0.f;
    matrix[2][3] = 0.f;
}

bool create_model_locked(preview_state& s) {
    const auto allocator = source_preview::memalloc();
    if (!allocator || !source_preview::model_api_ready())
        return false;

    const auto model = static_cast<C_MergedMDL*>(allocator->Alloc(k_merged_mdl_size));
    if (!model)
        return false;

    model->create();
    model->SetMDL("models/player/custom_player/uiplayer/animset_uiplayer.mdl");
    model->SetMergedMDL(agent_model(s.requested_side, s.requested_agent));
    model->SetMergedMDL("models/weapons/w_snip_ssg08.mdl");
    model->SetSequence(113, false);
    model->SetupBonesForAttachmentQueries();
    model->RootMDL.flTime = 20.f;

    s.preview_model = model;
    s.model_side = s.requested_side;
    s.model_agent = s.requested_agent;
    return true;
}

// This is Fatality's entity_rendering::paint path, reduced only by excluding its unrelated
// chams/world-ESP branches. It must run after original FrameStageNotify at FRAME_RENDER_END.
void paint_locked(preview_state& s) {
    if (!s.requested_active)
        return;

    const auto material_system = source_preview::material_system();
    const auto render_view = source_preview::render_view();
    const auto model_render = source_preview::model_render();
    const auto studio_render = source_preview::studio_render();
    const auto globals = source_preview::global_vars();
    if (!material_system || !render_view || !model_render || !studio_render || !globals)
        return;

    if (!s.preview_texture) {
        material_system->ForceBeginRenderTargetAllocation();
        s.preview_texture = material_system->CreateNamedRenderTargetTextureEx(
            "DarksideESPAgentPreview", k_preview_width, k_preview_height,
            source_preview::RT_SIZE_NO_CHANGE, material_system->GetBackBufferFormat(),
            source_preview::MATERIAL_RT_DEPTH_SHARED,
            source_preview::TEXTUREFLAGS_CLAMPS | source_preview::TEXTUREFLAGS_CLAMPT,
            source_preview::CREATERENDERTARGETFLAGS_HDR);
        material_system->ForceEndRenderTargetAllocation();
        material_system->FinishRenderTargetAllocation();
    }

    if (!s.cubemap_texture) {
        s.cubemap_texture = material_system->FindTexture(
            "editor/cubemap.hdr", source_preview::TEXTURE_GROUP_CUBE_MAP);
    }

    if ((s.model_side != s.requested_side || s.model_agent != s.requested_agent) &&
        s.preview_model)
        release_model_locked(s);

    if (!s.preview_model && !create_model_locked(s))
        return;
    if (!s.preview_texture)
        return;

    s.preview_model->RootMDL.flTime += globals->frametime * 0.5f;

    source_preview::CViewSetup view_setup{};
    view_setup.x = 0;
    view_setup.y = 0;
    view_setup.width = k_preview_width;
    view_setup.height = k_preview_height;
    view_setup.m_bOrtho = false;
    view_setup.fov = 12.f;
    view_setup.origin = source_preview::Vector(-329.f, 0.f, 52.f);
    view_setup.angles = source_preview::QAngle(2.f, 0.f, 0.f);
    view_setup.zNear = 20.f;
    view_setup.zFar = 1000.f;
    view_setup.m_bDoBloomAndToneMapping = true;

    source_preview::VMatrix world_to_view{}, view_to_projection{};
    source_preview::VMatrix world_to_projection{}, world_to_pixels{};
    render_view->GetMatricesForView(view_setup, &world_to_view, &view_to_projection,
        &world_to_projection, &world_to_pixels);

    render_context_scope context_scope(material_system);
    const auto context = context_scope.get();
    if (!context)
        return;

    context->PushRenderTargetAndViewport();
    context->SetRenderTarget(s.preview_texture);
    context->BindLocalCubemap(s.cubemap_texture);
    context->SetLightingOrigin(source_preview::Vector(-65.f, 10.f, 100.f));
    context->SetIntRenderingParameter(10, 0);

    source_preview::Frustum dummy_frustum{};
    render_view->Push3DView(context, view_setup,
        source_preview::VIEW_CLEAR_COLOR | source_preview::VIEW_CLEAR_DEPTH |
        source_preview::VIEW_CLEAR_STENCIL,
        s.preview_texture, dummy_frustum);

    static constexpr std::array light_col1 = {
        source_preview::Vector4D(0.68f, 1.53f, 1.7f, 1.f),
        source_preview::Vector4D(0.68f, 1.53f, 1.7f, 1.f),
        source_preview::Vector4D(0.68f, 1.53f, 1.7f, 1.f),
        source_preview::Vector4D(0.68f, 1.53f, 1.7f, 1.f),
        source_preview::Vector4D(0.68f, 1.53f, 1.7f, 1.f),
        source_preview::Vector4D(0.68f, 1.53f, 1.7f, 1.f),
    };

    context->ClearColor4ub(0, 0, 0, 0);
    context->ClearBuffers(true, true, true);
    context->SetAmbientLightCube(light_col1.data());
    studio_render->SetAmbientLightColors2(light_col1.data());
    studio_render->SetLocalLights2(0, nullptr);

    source_preview::matrix3x4_t player_view{};
    angle_matrix(source_preview::QAngle(0.f, 180.f, 0.f), player_view);

    model_render->SuppressEngineLighting(true);
    s.preview_model->Draw(player_view);
    model_render->SuppressEngineLighting(false);

    render_view->PopView(context, dummy_frustum);
    context->BindLocalCubemap(nullptr);
    context->PopRenderTargetAndViewport();

    s.rendered_side = s.requested_side;
    s.rendered_agent = s.requested_agent;
}
} // namespace

namespace agent_preview {
void* frame_stage_notify_target() {
    return reinterpret_cast<void*>(source_preview::module_address(
        "client.dll", source_preview::offset::frame_stage_notify));
}

void on_frame_stage_render_end() {
    auto& s = state();
    std::scoped_lock lock(s.mutex);
    paint_locked(s);
}

void begin_frame(IDirect3DDevice9*) {
    auto& s = state();
    // Normally end_frame() already released this. Keep the stale-owner cleanup for an early
    // renderer exit while preserving the direct material-texture path.
    if (s.display_lock.owns_lock())
        release_display_and_unlock(s);

    std::scoped_lock lock(s.mutex);
    release_display_locked(s);
    s.requested_active = false;
}

void draw(const ImVec2& min, const ImVec2& max, int side) {
    if (side != 0 && side != 1)
        return;
    if (max.x - min.x < 1.f || max.y - min.y < 1.f)
        return;

    auto& s = state();
    std::unique_lock lock(s.mutex);
    s.requested_active = true;
    s.requested_side = side;
    s.requested_agent = selected_agent(side);

    if (!s.preview_texture || s.rendered_side != side ||
        s.rendered_agent != s.requested_agent)
        return;

    const auto handles = s.preview_texture->pTextureHandles;
    const auto raw = handles && handles[0] ? handles[0]->lpRawTexture : nullptr;
    if (!raw)
        return;

    release_display_locked(s);
    raw->AddRef();
    s.display_texture = raw;

    const float alpha = ImGui::GetStyle().Alpha;
    ImGui::GetWindowDrawList()->AddImage(reinterpret_cast<ImTextureID>(raw), min, max,
        ImVec2(0.f, 0.f), ImVec2(1.f, 1.f),
        ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, alpha)));

    // Keep the model/target lock until end_frame(), after the D3D9 draw data is submitted.
    s.display_lock = std::move(lock);
}

void end_frame() {
    auto& s = state();
    if (s.display_lock.owns_lock()) {
        release_display_and_unlock(s);
        return;
    }

    std::scoped_lock lock(s.mutex);
    release_display_locked(s);
}

void on_device_reset() {
    auto& s = state();
    if (s.display_lock.owns_lock())
        release_display_and_unlock(s);

    std::scoped_lock lock(s.mutex);
    s.requested_active = false;
    release_display_locked(s);
    release_model_locked(s);
    release_texture_locked(s.preview_texture);
    release_texture_locked(s.cubemap_texture);
    s.rendered_side = -1;
    s.rendered_agent = -1;
}
} // namespace agent_preview
