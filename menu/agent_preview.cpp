#include "agent_preview.hpp"

// Keep Win32/COM declarations ahead of menu.hpp's standard-library namespace imports.
#include <Windows.h>
#include <objbase.h>
#include <wincodec.h>

#include "menu.hpp"
#include "imgui_impl_dx9.h"
#include "../sdk/config_system/config_system.hpp"
#include "../sdk/source_files.hpp"
#include "../runtime/debug_console.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#endif

namespace {
// Exact player_model_list indices from Fatality's skinchanger.h. Index zero is deliberately
// empty: the menu uses index 46 for the default agent.
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
    // Keep only the two visible selections, rather than uploading all 92 portraits to the GPU.
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

    // CS:GO has both naming schemes: newer portraits retain tm_/ctm_, while older ones do not.
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

ImTextureID icon_for_agent_locked(preview_state& s, int agent, int side, int& width, int& height) {
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
        AIRFLOW_DEBUG_STAGE("agent_preview.icon.vpk");
        std::vector<unsigned char> bytes;
        if (!read_vpk_file(path, bytes))
            continue;

        AIRFLOW_DEBUG_STAGE("agent_preview.icon.decode");
        decoded_image decoded;
        if (!decode_png(bytes, decoded))
            continue;

        AIRFLOW_DEBUG_STAGE("agent_preview.icon.texture");
        const auto texture = ImGui_CreateTextureRGBA(decoded.width, decoded.height, decoded.rgba.data());
        if (!texture)
            continue;

        icon.texture = texture;
        icon.width = decoded.width;
        icon.height = decoded.height;
        width = decoded.width;
        height = decoded.height;
        AIRFLOW_DEBUG_LOGF("agent_preview icon loaded: %s (%dx%d)", path.c_str(), width, height);
        return texture;
    }

    return nullptr;
}
} // namespace

namespace agent_preview {
void draw(const ImVec2& min, const ImVec2& max, int side) {
    if (!ImGui::GetCurrentContext() || (side != 0 && side != 1) ||
        max.x - min.x < 1.f || max.y - min.y < 1.f) {
        return;
    }

    auto& s = state();
    std::scoped_lock lock(s.mutex);
    const int agent = selected_agent(side);

    int width = 0;
    int height = 0;
    const auto texture = icon_for_agent_locked(s, agent, side, width, height);
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

    AIRFLOW_DEBUG_STAGE("agent_preview.icon.AddImage");
    ImGui::GetWindowDrawList()->AddImage(texture, image_min, image_max,
        ImVec2(0.f, 0.f), ImVec2(1.f, 1.f),
        ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, alpha)));
}

void on_device_reset() {
    auto& s = state();
    std::scoped_lock lock(s.mutex);
    for (auto& icon : s.icons) {
        if (icon.texture)
            ImGui_DestroyTexture(icon.texture);
        icon = icon_state{};
    }
}
} // namespace agent_preview
