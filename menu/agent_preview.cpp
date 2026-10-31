#include "agent_preview.hpp"

// Keep Win32/COM declarations ahead of menu.hpp's standard-library namespace imports.
#include <Windows.h>
#include <objbase.h>
#include <wincodec.h>

#include "menu.hpp"
#include "menu_resources.h"
#include "imgui_impl_dx9.h"
#include "../runtime/debug_console.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#endif

namespace {
struct decoded_image {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgba;
};

struct icon_state {
    ImTextureID texture = nullptr;
    int width = 0;
    int height = 0;
    int retry_frame = 0;
};

struct preview_state {
    std::mutex mutex;
    std::array<icon_state, 2> icons{};
};

preview_state& state() {
    static preview_state value;
    return value;
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

bool read_embedded_resource(int resource_id, std::vector<unsigned char>& bytes) {
    // Resolve the DLL from a code address instead of assuming its file name or the host process.
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&read_embedded_resource), &module) || !module) {
        return false;
    }

    const auto resource = FindResourceW(module, MAKEINTRESOURCEW(resource_id), RT_RCDATA);
    if (!resource)
        return false;

    const auto loaded = LoadResource(module, resource);
    if (!loaded)
        return false;

    const DWORD size = SizeofResource(module, resource);
    const auto data = static_cast<const unsigned char*>(LockResource(loaded));
    if (!data || size == 0 || size > 16 * 1024 * 1024)
        return false;

    bytes.assign(data, data + size);
    return true;
}

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

ImTextureID icon_for_side_locked(preview_state& s, int side, int& width, int& height) {
    auto& icon = s.icons[side];
    if (icon.texture) {
        width = icon.width;
        height = icon.height;
        return icon.texture;
    }

    const int frame = ImGui::GetFrameCount();
    if (frame < icon.retry_frame)
        return nullptr;
    icon.retry_frame = frame + 120;

    const int resource_id = side == 0 ? IDR_AGENT_ENEMIES : IDR_AGENT_TEAMMATES;
    AIRFLOW_DEBUG_STAGE("agent_preview.resource.read");
    std::vector<unsigned char> bytes;
    if (!read_embedded_resource(resource_id, bytes))
        return nullptr;

    AIRFLOW_DEBUG_STAGE("agent_preview.resource.decode");
    decoded_image decoded;
    if (!decode_png(bytes, decoded))
        return nullptr;

    AIRFLOW_DEBUG_STAGE("agent_preview.resource.texture");
    const auto texture = ImGui_CreateTextureRGBA(
        decoded.width, decoded.height, decoded.rgba.data());
    if (!texture)
        return nullptr;

    icon.texture = texture;
    icon.width = decoded.width;
    icon.height = decoded.height;
    width = icon.width;
    height = icon.height;
    AIRFLOW_DEBUG_LOGF("agent_preview embedded image loaded: side=%d (%dx%d)",
        side, width, height);
    return texture;
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
void draw(const ImVec2& min, const ImVec2& max, int side) {
    if (!ImGui::GetCurrentContext() || (side != 0 && side != 1) ||
        max.x - min.x < 1.f || max.y - min.y < 1.f) {
        return;
    }

    auto& s = state();
    std::scoped_lock lock(s.mutex);

    int width = 0;
    int height = 0;
    const auto texture = icon_for_side_locked(s, side, width, height);
    AIRFLOW_DEBUG_STAGE("agent_preview.resource.AddImage");
    add_aspect_fit_image(texture, width, height, min, max);
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
