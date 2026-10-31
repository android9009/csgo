#include "agent_preview.hpp"

// Keep Win32/COM/D3D declarations ahead of menu.hpp's standard-library namespace imports.
#include <Windows.h>
#include <d3d9.h>
#include <objbase.h>
#include <wincodec.h>

#include "menu.hpp"
#include "imgui_impl_dx9.h"
#include "../sdk/config_system/config_system.hpp"
#include "../sdk/source_files.hpp"
#include "../sdk/source_preview.hpp"
#include "../runtime/debug_console.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#endif

namespace {
using source_preview::C_MergedMDL;
using source_preview::ITexture;

constexpr int k_preview_width = 235;
constexpr int k_preview_height = 370;
constexpr std::size_t k_merged_mdl_size = 0x75C;

// Exact player_model_list indices from Fatality's skinchanger.h. Index zero is deliberately
// empty: entity_rendering.cpp replaces a zero selection with index 46.
constexpr const char* k_player_models[] = {
    nullptr,
    "models/player/custom_player/legacy/tm_leet_variantf.mdl",
    "models/player/custom_player/legacy/tm_leet_varianti.mdl",
    "models/player/custom_player/legacy/tm_leet_varianth.mdl",
    "models/player/custom_player/legacy/tm_leet_variantg.mdl",
    "models/player/custom_player/legacy/tm_leet_variantj.mdl",
    "models/player/custom_player/legacy/ctm_fbi_variantb.mdl",
    "models/player/custom_player/legacy/ctm_fbi_varianth.mdl",
    "models/player/custom_player/legacy/ctm_fbi_variantg.mdl",
    "models/player/custom_player/legacy/ctm_fbi_variantf.mdl",
    "models/player/custom_player/legacy/ctm_st6_variante.mdl",
    "models/player/custom_player/legacy/ctm_st6_variantm.mdl",
    "models/player/custom_player/legacy/ctm_st6_variantg.mdl",
    "models/player/custom_player/legacy/ctm_st6_variantk.mdl",
    "models/player/custom_player/legacy/ctm_st6_varianti.mdl",
    "models/player/custom_player/legacy/ctm_st6_variantj.mdl",
    "models/player/custom_player/legacy/ctm_st6_variantl.mdl",
    "models/player/custom_player/legacy/ctm_st6_variantn.mdl",
    "models/player/custom_player/legacy/ctm_swat_variante.mdl",
    "models/player/custom_player/legacy/ctm_swat_variantf.mdl",
    "models/player/custom_player/legacy/ctm_swat_variantg.mdl",
    "models/player/custom_player/legacy/ctm_swat_varianth.mdl",
    "models/player/custom_player/legacy/ctm_swat_varianti.mdl",
    "models/player/custom_player/legacy/ctm_swat_variantj.mdl",
    "models/player/custom_player/legacy/ctm_swat_variantk.mdl",
    "models/player/custom_player/legacy/tm_balkan_varianti.mdl",
    "models/player/custom_player/legacy/tm_balkan_variantf.mdl",
    "models/player/custom_player/legacy/tm_balkan_varianth.mdl",
    "models/player/custom_player/legacy/tm_balkan_variantg.mdl",
    "models/player/custom_player/legacy/tm_balkan_variantj.mdl",
    "models/player/custom_player/legacy/tm_balkan_variantk.mdl",
    "models/player/custom_player/legacy/tm_balkan_variantl.mdl",
    "models/player/custom_player/legacy/ctm_sas_variantg.mdl",
    "models/player/custom_player/legacy/ctm_sas_variantf.mdl",
    "models/player/custom_player/legacy/tm_phoenix_varianth.mdl",
    "models/player/custom_player/legacy/tm_phoenix_variantf.mdl",
    "models/player/custom_player/legacy/tm_phoenix_variantg.mdl",
    "models/player/custom_player/legacy/tm_phoenix_varianti.mdl",
    "models/player/custom_player/legacy/tm_professional_varf.mdl",
    "models/player/custom_player/legacy/tm_professional_varf1.mdl",
    "models/player/custom_player/legacy/tm_professional_varf2.mdl",
    "models/player/custom_player/legacy/tm_professional_varf3.mdl",
    "models/player/custom_player/legacy/tm_professional_varf4.mdl",
    "models/player/custom_player/legacy/tm_professional_varf5.mdl",
    "models/player/custom_player/legacy/tm_professional_varg.mdl",
    "models/player/custom_player/legacy/tm_professional_varh.mdl",
    "models/player/custom_player/legacy/tm_professional_vari.mdl",
    "models/player/custom_player/legacy/tm_professional_varj.mdl",
    "models/player/custom_player/legacy/tm_jungle_raider_variantf2.mdl",
    "models/player/custom_player/legacy/tm_jungle_raider_variantf.mdl",
    "models/player/custom_player/legacy/tm_jungle_raider_variante.mdl",
    "models/player/custom_player/legacy/tm_jungle_raider_variantd.mdl",
    "models/player/custom_player/legacy/tm_jungle_raider_variantc.mdl",
    "models/player/custom_player/legacy/tm_jungle_raider_variantb2.mdl",
    "models/player/custom_player/legacy/tm_jungle_raider_variantb.mdl",
    "models/player/custom_player/legacy/tm_jungle_raider_varianta.mdl",
    "models/player/custom_player/legacy/ctm_diver_varianta.mdl",
    "models/player/custom_player/legacy/ctm_diver_variantb.mdl",
    "models/player/custom_player/legacy/ctm_diver_variantc.mdl",
    "models/player/custom_player/legacy/ctm_gendarmerie_varianta.mdl",
    "models/player/custom_player/legacy/ctm_gendarmerie_variantb.mdl",
    "models/player/custom_player/legacy/ctm_gendarmerie_variantc.mdl",
    "models/player/custom_player/legacy/ctm_gendarmerie_variantd.mdl",
    "models/player/custom_player/legacy/ctm_gendarmerie_variante.mdl",
    "models/player/custom_player/legacy/tm_jumpsuit_varianta.mdl",
    "models/player/custom_player/legacy/tm_jumpsuit_variantb.mdl",
    "models/player/custom_player/legacy/tm_jumpsuit_variantc.mdl",
    "models/player/custom_player/legacy/tm_pirate.mdl",
    "models/player/custom_player/legacy/tm_pirate_varianta.mdl",
    "models/player/custom_player/legacy/tm_pirate_variantb.mdl",
    "models/player/custom_player/legacy/tm_pirate_variantc.mdl",
    "models/player/custom_player/legacy/tm_pirate_variantd.mdl",
    "models/player/custom_player/legacy/tm_separatist.mdl",
    "models/player/custom_player/legacy/tm_separatist_varianta.mdl",
    "models/player/custom_player/legacy/tm_separatist_variantb.mdl",
    "models/player/custom_player/legacy/tm_separatist_variantc.mdl",
    "models/player/custom_player/legacy/tm_separatist_variantd.mdl",
    "models/player/custom_player/legacy/tm_phoenix_heavy.mdl",
    "models/player/custom_player/legacy/tm_phoenix_varianta.mdl",
    "models/player/custom_player/legacy/tm_phoenix_variantb.mdl",
    "models/player/custom_player/legacy/tm_phoenix_variantc.mdl",
    "models/player/custom_player/legacy/tm_phoenix_variantd.mdl",
    "models/player/custom_player/legacy/tm_leet_varianta.mdl",
    "models/player/custom_player/legacy/tm_leet_variantb.mdl",
    "models/player/custom_player/legacy/tm_leet_variantc.mdl",
    "models/player/custom_player/legacy/tm_leet_variantd.mdl",
    "models/player/custom_player/legacy/tm_leet_variante.mdl",
    "models/player/custom_player/legacy/tm_professional.mdl",
    "models/player/custom_player/legacy/tm_professional_var1.mdl",
    "models/player/custom_player/legacy/tm_professional_var2.mdl",
    "models/player/custom_player/legacy/tm_professional_var3.mdl",
    "models/player/custom_player/legacy/tm_professional_var4.mdl",
};

static_assert(IM_ARRAYSIZE(k_player_models) == 92);

struct decoded_image {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgba;
};

struct icon_state {
    int agent = -1;
    ImTextureID texture = nullptr;
    int width = 0;
    int height = 0;
    int retry_frame = 0;
};

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
    bool is_setup = false;
    int rendered_side = -1;
    int rendered_agent = -1;

    // Match Fatality's namespace-scope lifetime for view state reused by entity_rendering.
    source_preview::CViewSetup view_setup{};
    source_preview::VMatrix world_to_projection{};

    IDirect3DTexture9* display_texture = nullptr;

    // Legendware-style VPK portraits are only a fallback while the full-body target is absent.
    // Keep just the two possible selections instead of uploading all 92 images.
    std::array<icon_state, 2> icons{};
};

preview_state& state() {
    static preview_state value;
    return value;
}

int selected_agent(int side) {
    const int selected = side == 0
        ? g_cfg->skins.m_agents.m_selected
        : g_cfg->skins.m_agents.m_selected_ct;
    const int agent = std::clamp(selected, 0, static_cast<int>(IM_ARRAYSIZE(k_player_models)) - 1);
    return agent == 0 ? 46 : agent;
}

const char* agent_model(int agent) {
    return k_player_models[agent];
}

template <typename T>
class com_ptr {
public:
    com_ptr() = default;
    com_ptr(const com_ptr&) = delete;
    com_ptr& operator=(const com_ptr&) = delete;

    ~com_ptr() {
        if (value_)
            value_->Release();
    }

    T* get() const { return value_; }
    T** address() { return &value_; }
    T* operator->() const { return value_; }

private:
    T* value_ = nullptr;
};

bool decode_png_with_wic(const std::vector<unsigned char>& bytes, decoded_image& image) {
    if (bytes.empty())
        return false;

    const auto memory = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    if (!memory)
        return false;

    const auto destination = GlobalLock(memory);
    if (!destination) {
        GlobalFree(memory);
        return false;
    }
    std::memcpy(destination, bytes.data(), bytes.size());
    GlobalUnlock(memory);

    com_ptr<IStream> stream;
    if (FAILED(CreateStreamOnHGlobal(memory, TRUE, stream.address()))) {
        GlobalFree(memory);
        return false;
    }

    com_ptr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_IWICImagingFactory, reinterpret_cast<void**>(factory.address())))) {
        return false;
    }

    com_ptr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromStream(stream.get(), nullptr,
        WICDecodeMetadataCacheOnLoad, decoder.address()))) {
        return false;
    }

    com_ptr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, frame.address())))
        return false;

    com_ptr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(converter.address())) ||
        FAILED(converter->Initialize(frame.get(), GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
        return false;
    }

    UINT width = 0;
    UINT height = 0;
    if (FAILED(converter->GetSize(&width, &height)) || width == 0 || height == 0 ||
        width > 4096 || height > 4096 ||
        static_cast<std::uint64_t>(width) * height > 16 * 1024 * 1024) {
        return false;
    }

    image.width = static_cast<int>(width);
    image.height = static_cast<int>(height);
    image.rgba.resize(static_cast<std::size_t>(width) * height * 4);
    return SUCCEEDED(converter->CopyPixels(nullptr, width * 4,
        static_cast<UINT>(image.rgba.size()), image.rgba.data()));
}

bool decode_png(const std::vector<unsigned char>& bytes, decoded_image& image) {
    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com_result) && com_result != RPC_E_CHANGED_MODE)
        return false;

    const bool decoded = decode_png_with_wic(bytes, image);
    if (SUCCEEDED(com_result))
        CoUninitialize();
    return decoded;
}

bool read_vpk_file(const std::string& path, std::vector<unsigned char>& bytes) {
    const auto file_system = source_files::base_file_system();
    if (!file_system)
        return false;

    const auto handle = file_system->Open(path.c_str(), "r", "GAME");
    if (!handle)
        return false;

    const unsigned int size = file_system->Size(handle);
    if (size == 0 || size > 64 * 1024 * 1024) {
        file_system->Close(handle);
        return false;
    }

    bytes.resize(size);
    const int read = file_system->Read(bytes.data(), static_cast<int>(size), handle);
    file_system->Close(handle);
    return read == static_cast<int>(size);
}

std::vector<std::string> icon_paths(int agent, int side) {
    std::string model = agent_model(agent);
    const auto slash = model.find_last_of("/\\");
    if (slash != std::string::npos)
        model.erase(0, slash + 1);
    const auto extension = model.rfind(".mdl");
    if (extension != std::string::npos)
        model.erase(extension);

    std::string short_model = model;
    if (short_model.rfind("ctm_", 0) == 0)
        short_model.erase(0, 4);
    else if (short_model.rfind("tm_", 0) == 0)
        short_model.erase(0, 3);

    constexpr const char* prefix = "resource/flash/econ/characters/customplayer_";
    std::vector<std::string> paths;
    const auto add = [&paths](const std::string& path) {
        if (std::find(paths.begin(), paths.end(), path) == paths.end())
            paths.push_back(path);
    };

    add(prefix + model + ".png");
    add(prefix + short_model + ".png");
    add(prefix + model + "_large.png");
    add(prefix + short_model + "_large.png");

    if (side == 0) {
        add("resource/flash/econ/characters/customplayer_tm_phoenix.png");
        add("resource/flash/econ/characters/customplayer_phoenix.png");
    } else {
        add("resource/flash/econ/characters/customplayer_ctm_sas.png");
        add("resource/flash/econ/characters/customplayer_sas.png");
        add("resource/flash/econ/characters/customplayer_st6.png");
    }
    return paths;
}

ImTextureID icon_for_agent_locked(preview_state& s, int agent, int side,
                                  int& width, int& height) {
    auto& icon = s.icons[side];
    if (icon.agent != agent) {
        if (icon.texture)
            ImGui_DestroyTexture(icon.texture);
        icon = icon_state{};
        icon.agent = agent;
    }

    if (icon.texture) {
        width = icon.width;
        height = icon.height;
        return icon.texture;
    }

    const int frame = ImGui::GetFrameCount();
    if (frame < icon.retry_frame)
        return nullptr;
    icon.retry_frame = frame + 120;

    for (const auto& path : icon_paths(agent, side)) {
        AIRFLOW_DEBUG_STAGE("agent_preview.fallback.vpk");
        std::vector<unsigned char> bytes;
        if (!read_vpk_file(path, bytes))
            continue;

        AIRFLOW_DEBUG_STAGE("agent_preview.fallback.decode");
        decoded_image decoded;
        if (!decode_png(bytes, decoded))
            continue;

        AIRFLOW_DEBUG_STAGE("agent_preview.fallback.texture");
        const auto texture = ImGui_CreateTextureRGBA(
            decoded.width, decoded.height, decoded.rgba.data());
        if (!texture)
            continue;

        icon.texture = texture;
        icon.width = decoded.width;
        icon.height = decoded.height;
        width = icon.width;
        height = icon.height;
        AIRFLOW_DEBUG_LOGF("agent_preview fallback icon loaded: %s (%dx%d)",
            path.c_str(), width, height);
        return texture;
    }

    return nullptr;
}

void release_icons_locked(preview_state& s) {
    for (auto& icon : s.icons) {
        if (icon.texture)
            ImGui_DestroyTexture(icon.texture);
        icon = icon_state{};
    }
}

void release_display_locked(preview_state& s) {
    if (s.display_texture) {
        AIRFLOW_DEBUG_STAGE("agent_preview.display.raw_Release");
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
        AIRFLOW_DEBUG_STAGE("agent_preview.model.Free");
        if (const auto allocator = source_preview::memalloc())
            allocator->Free(s.preview_model);
        s.preview_model = nullptr;
    }
    s.model_side = -1;
    s.model_agent = -1;
    s.is_setup = false;
    s.rendered_side = -1;
    s.rendered_agent = -1;
}

void release_texture_locked(ITexture*& texture) {
    if (texture) {
        AIRFLOW_DEBUG_STAGE("agent_preview.texture.DecrementReferenceCount");
        texture->DecrementReferenceCount();
        texture = nullptr;
    }
}

#ifdef _DEBUG
bool debug_readable(const void* address, std::size_t size) {
    if (!address || !size)
        return false;

    MEMORY_BASIC_INFORMATION information{};
    if (VirtualQuery(address, &information, sizeof(information)) != sizeof(information) ||
        information.State != MEM_COMMIT ||
        (information.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
        return false;
    }

    const auto offset = reinterpret_cast<std::uintptr_t>(address) -
        reinterpret_cast<std::uintptr_t>(information.BaseAddress);
    return offset <= information.RegionSize && size <= information.RegionSize - offset;
}

void* debug_read_pointer(const void* address) {
    if (!debug_readable(address, sizeof(void*)))
        return nullptr;
    return *static_cast<void* const*>(address);
}
#endif

class render_context_scope {
public:
    explicit render_context_scope(source_preview::IMaterialSystem* material_system) {
        AIRFLOW_DEBUG_STAGE("agent_preview.context.GetRenderContext");
        context_ = material_system ? material_system->GetRenderContext() : nullptr;
        if (context_) {
            AIRFLOW_DEBUG_STAGE("agent_preview.context.BeginRender");
            context_->BeginRender();
        }
    }

    render_context_scope(const render_context_scope&) = delete;
    render_context_scope& operator=(const render_context_scope&) = delete;

    ~render_context_scope() {
        if (context_) {
            // CMatRenderContextPtr::~CMatRenderContextPtr() calls EndRender(), then its
            // SafeRelease() calls EndRender() again before releasing the interface.
            AIRFLOW_DEBUG_STAGE("agent_preview.context.EndRender.1");
            context_->EndRender();
            AIRFLOW_DEBUG_STAGE("agent_preview.context.EndRender.2");
            context_->EndRender();
            AIRFLOW_DEBUG_STAGE("agent_preview.context.Release");
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
    AIRFLOW_DEBUG_STAGE("agent_preview.model.interfaces");
    const auto allocator = source_preview::memalloc();
    if (!allocator || !source_preview::model_api_ready())
        return false;

    AIRFLOW_DEBUG_STAGE("agent_preview.model.Alloc");
    const auto model = static_cast<C_MergedMDL*>(allocator->Alloc(k_merged_mdl_size));
    if (!model)
        return false;

    AIRFLOW_DEBUG_STAGE("agent_preview.model.create");
    model->create();
    AIRFLOW_DEBUG_STAGE("agent_preview.model.SetMDL");
    model->SetMDL("models/player/custom_player/uiplayer/animset_uiplayer.mdl");
    AIRFLOW_DEBUG_STAGE("agent_preview.model.SetMergedMDL.player");
    model->SetMergedMDL(agent_model(s.requested_agent));
    AIRFLOW_DEBUG_STAGE("agent_preview.model.SetMergedMDL.weapon");
    model->SetMergedMDL("models/weapons/w_snip_ssg08.mdl");
    AIRFLOW_DEBUG_STAGE("agent_preview.model.SetSequence");
    model->SetSequence(113, false);
    AIRFLOW_DEBUG_STAGE("agent_preview.model.SetupBonesForAttachmentQueries");
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
    // Do not touch Source's renderer while the preview is closed. The Legendware fallback also
    // remains completely dormant until draw() explicitly requests the active agent.
    if (!s.requested_active)
        return;

    AIRFLOW_DEBUG_STAGE("agent_preview.paint.interfaces");
    const auto material_system = source_preview::material_system();
    const auto render_view = source_preview::render_view();
    const auto model_render = source_preview::model_render();
    const auto studio_render = source_preview::studio_render();
    const auto globals = source_preview::global_vars();
    if (!material_system || !render_view || !model_render || !studio_render || !globals ||
        !source_preview::model_api_ready()) {
        return;
    }

    if (!s.preview_texture) {
        const auto texture_name = std::to_string(std::rand());
        AIRFLOW_DEBUG_STAGE("agent_preview.target.ForceBeginRenderTargetAllocation");
        material_system->ForceBeginRenderTargetAllocation();
        AIRFLOW_DEBUG_STAGE("agent_preview.target.GetBackBufferFormat");
        const auto back_buffer_format = material_system->GetBackBufferFormat();
        AIRFLOW_DEBUG_STAGE("agent_preview.target.CreateNamedRenderTargetTextureEx");
        s.preview_texture = material_system->CreateNamedRenderTargetTextureEx(
            texture_name.c_str(), k_preview_width, k_preview_height,
            source_preview::RT_SIZE_NO_CHANGE, back_buffer_format,
            source_preview::MATERIAL_RT_DEPTH_SHARED,
            source_preview::TEXTUREFLAGS_CLAMPS | source_preview::TEXTUREFLAGS_CLAMPT,
            source_preview::CREATERENDERTARGETFLAGS_HDR);
        AIRFLOW_DEBUG_STAGE("agent_preview.target.ForceEndRenderTargetAllocation");
        material_system->ForceEndRenderTargetAllocation();
        AIRFLOW_DEBUG_STAGE("agent_preview.target.FinishRenderTargetAllocation");
        material_system->FinishRenderTargetAllocation();
    }

    if (!s.cubemap_texture) {
        AIRFLOW_DEBUG_STAGE("agent_preview.cubemap.FindTexture");
        s.cubemap_texture = material_system->FindTexture(
            "editor/cubemap.hdr", source_preview::TEXTURE_GROUP_CUBE_MAP);
    }

    if ((s.model_side != s.requested_side || s.model_agent != s.requested_agent) &&
        s.preview_model) {
        AIRFLOW_DEBUG_STAGE("agent_preview.model.release_changed");
        release_model_locked(s);
    }

    if (!s.preview_model && !create_model_locked(s))
        return;
    if (!s.preview_texture)
        return;

    s.is_setup = false;
    s.rendered_side = -1;
    s.rendered_agent = -1;

    AIRFLOW_DEBUG_STAGE("agent_preview.model.flTime");
    s.preview_model->RootMDL.flTime += globals->frametime * 0.5f;

    AIRFLOW_DEBUG_STAGE("agent_preview.view.setup");
    auto& view_setup = s.view_setup;
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

    AIRFLOW_DEBUG_STAGE("agent_preview.view.GetMatricesForView");
    source_preview::VMatrix world_to_view{}, view_to_projection{}, world_to_pixels{};
    render_view->GetMatricesForView(view_setup, &world_to_view, &view_to_projection,
        &s.world_to_projection, &world_to_pixels);

    render_context_scope context_scope(material_system);
    const auto context = context_scope.get();
    if (!context)
        return;

#ifdef _DEBUG
    static bool context_logged = false;
    if (!context_logged) {
        context_logged = true;
        AIRFLOW_DEBUG_STAGE("agent_preview.context.inspect");
        const auto context_table = *reinterpret_cast<void***>(context);
        AIRFLOW_DEBUG_LOGF(
            "agent_preview context=%p vtable=%p slot116_six_arg=%p slot119_no_arg=%p slot120=%p",
            context, context_table, context_table ? context_table[116] : nullptr,
            context_table ? context_table[119] : nullptr,
            context_table ? context_table[120] : nullptr);
        const auto current_target = context->GetRenderTarget();
        const auto current_table = debug_read_pointer(current_target);
        AIRFLOW_DEBUG_LOGF(
            "agent_preview current_target=%p current_vtable=%p preview_texture=%p "
            "preview_vtable=%p",
            current_target, current_table, s.preview_texture,
            debug_read_pointer(s.preview_texture));
        const auto render_view_table = debug_read_pointer(render_view);
        AIRFLOW_DEBUG_LOGF(
            "agent_preview render_view=%p vtable=%p slot43=%p slot45=%p slot55=%p slot56=%p "
            "cubemap=%p cubemap_vtable=%p",
            render_view, render_view_table,
            render_view_table ? static_cast<void**>(render_view_table)[43] : nullptr,
            render_view_table ? static_cast<void**>(render_view_table)[45] : nullptr,
            render_view_table ? static_cast<void**>(render_view_table)[55] : nullptr,
            render_view_table ? static_cast<void**>(render_view_table)[56] : nullptr,
            s.cubemap_texture, debug_read_pointer(s.cubemap_texture));
    }
#endif
    AIRFLOW_DEBUG_STAGE("agent_preview.context.PushRenderTargetAndViewport");
    context->PushRenderTargetAndViewport();
    AIRFLOW_DEBUG_STAGE("agent_preview.context.SetRenderTarget");
    context->SetRenderTarget(s.preview_texture);
    AIRFLOW_DEBUG_STAGE("agent_preview.context.BindLocalCubemap");
    context->BindLocalCubemap(s.cubemap_texture);
    AIRFLOW_DEBUG_STAGE("agent_preview.context.SetLightingOrigin");
    context->SetLightingOrigin(source_preview::Vector(-65.f, 10.f, 100.f));
    AIRFLOW_DEBUG_STAGE("agent_preview.context.SetIntRenderingParameter");
    context->SetIntRenderingParameter(10, 0);

    AIRFLOW_DEBUG_STAGE("agent_preview.view.Push3DView");
    source_preview::Frustum dummy_frustum{};
    render_view->Push3DView(context, view_setup,
        source_preview::VIEW_CLEAR_COLOR | source_preview::VIEW_CLEAR_DEPTH |
        source_preview::VIEW_CLEAR_STENCIL,
        s.preview_texture, dummy_frustum, nullptr);

    static constexpr std::array light_col1 = {
        source_preview::Vector4D(0.68f, 1.53f, 1.7f, 1.f),
        source_preview::Vector4D(0.68f, 1.53f, 1.7f, 1.f),
        source_preview::Vector4D(0.68f, 1.53f, 1.7f, 1.f),
        source_preview::Vector4D(0.68f, 1.53f, 1.7f, 1.f),
        source_preview::Vector4D(0.68f, 1.53f, 1.7f, 1.f),
        source_preview::Vector4D(0.68f, 1.53f, 1.7f, 1.f),
    };

    AIRFLOW_DEBUG_STAGE("agent_preview.context.ClearColor4ub");
    context->ClearColor4ub(0, 0, 0, 0);
    AIRFLOW_DEBUG_STAGE("agent_preview.context.ClearBuffers");
    context->ClearBuffers(true, true, true);
    AIRFLOW_DEBUG_STAGE("agent_preview.context.SetAmbientLightCube");
    context->SetAmbientLightCube(light_col1.data());
    AIRFLOW_DEBUG_STAGE("agent_preview.studio.SetAmbientLightColors2");
    studio_render->SetAmbientLightColors2(light_col1.data());
    AIRFLOW_DEBUG_STAGE("agent_preview.studio.SetLocalLights2");
    studio_render->SetLocalLights2(0, nullptr);

    source_preview::matrix3x4_t player_view{};
    angle_matrix(source_preview::QAngle(0.f, 180.f, 0.f), player_view);

    AIRFLOW_DEBUG_STAGE("agent_preview.model.SuppressEngineLighting.true");
    model_render->SuppressEngineLighting(true);
    AIRFLOW_DEBUG_STAGE("agent_preview.model.Draw");
    s.preview_model->Draw(player_view);
    AIRFLOW_DEBUG_STAGE("agent_preview.model.SuppressEngineLighting.false");
    model_render->SuppressEngineLighting(false);

    AIRFLOW_DEBUG_STAGE("agent_preview.view.PopView");
    render_view->PopView(context, dummy_frustum);
    AIRFLOW_DEBUG_STAGE("agent_preview.context.UnbindLocalCubemap");
    context->BindLocalCubemap(nullptr);
    AIRFLOW_DEBUG_STAGE("agent_preview.context.PopRenderTargetAndViewport");
    context->PopRenderTargetAndViewport();

    s.is_setup = true;
    s.rendered_side = s.model_side;
    s.rendered_agent = s.model_agent;
}

void add_aspect_fit_image(ImTextureID texture, int width, int height,
                          const ImVec2& min, const ImVec2& max) {
    if (!texture || width <= 0 || height <= 0)
        return;

    const ImVec2 available = max - min;
    const float scale = std::min(available.x / width, available.y / height);
    if (scale <= 0.f)
        return;

    const ImVec2 size(width * scale, height * scale);
    const ImVec2 image_min = min + (available - size) * 0.5f;
    const ImVec2 image_max = image_min + size;
    const float alpha = ImGui::GetStyle().Alpha;
    ImGui::GetWindowDrawList()->AddImage(texture, image_min, image_max,
        ImVec2(0.f, 0.f), ImVec2(1.f, 1.f),
        ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, alpha)));
}
} // namespace

namespace agent_preview {
void* frame_stage_notify_target() {
    return reinterpret_cast<void*>(source_preview::module_address(
        "client.dll", source_preview::offset::frame_stage_notify));
}

void on_frame_stage_render_end() {
    AIRFLOW_DEBUG_STAGE("agent_preview.frame.lock");
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
    if (!ImGui::GetCurrentContext() || (side != 0 && side != 1) ||
        max.x - min.x < 1.f || max.y - min.y < 1.f) {
        return;
    }

    AIRFLOW_DEBUG_STAGE("agent_preview.draw.lock");
    auto& s = state();
    std::unique_lock lock(s.mutex);
    AIRFLOW_DEBUG_STAGE("agent_preview.draw.request");
    s.requested_active = true;
    s.requested_side = side;
    s.requested_agent = selected_agent(side);

    const bool full_body_ready = s.preview_texture && s.is_setup &&
        s.rendered_side == s.requested_side && s.rendered_agent == s.requested_agent;
    if (full_body_ready) {
        AIRFLOW_DEBUG_STAGE("agent_preview.draw.raw_texture");
        const auto handles = s.preview_texture->pTextureHandles;
        const auto raw = handles && handles[0] ? handles[0]->lpRawTexture : nullptr;
        if (raw) {
            release_display_locked(s);
            AIRFLOW_DEBUG_STAGE("agent_preview.draw.raw_AddRef");
            raw->AddRef();
            s.display_texture = raw;

            AIRFLOW_DEBUG_STAGE("agent_preview.draw.full_body");
            add_aspect_fit_image(reinterpret_cast<ImTextureID>(raw),
                k_preview_width, k_preview_height, min, max);

            // Keep the model/target lock until end_frame(), after D3D9 consumes the texture.
            s.display_lock = std::move(lock);
            return;
        }
    }

    // A missing model/interface/target must not leave an empty card. This is the Legendware
    // VPK path; it is a temporary portrait fallback, never a substitute for a ready 3D render.
    int width = 0;
    int height = 0;
    const auto fallback = icon_for_agent_locked(
        s, s.requested_agent, s.requested_side, width, height);
    AIRFLOW_DEBUG_STAGE("agent_preview.draw.fallback");
    add_aspect_fit_image(fallback, width, height, min, max);
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
    AIRFLOW_DEBUG_STAGE("agent_preview.reset");
    auto& s = state();
    if (s.display_lock.owns_lock())
        release_display_and_unlock(s);

    std::scoped_lock lock(s.mutex);
    s.requested_active = false;
    release_display_locked(s);
    release_model_locked(s);
    release_texture_locked(s.preview_texture);
    release_texture_locked(s.cubemap_texture);
    release_icons_locked(s);
    s.is_setup = false;
    s.rendered_side = -1;
    s.rendered_agent = -1;
}
} // namespace agent_preview
