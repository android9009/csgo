// Direct3D 9 backend for Darkside's rounded back-buffer blur.
// The GUI records the same callback/image sequence as the original renderer; only
// texture creation and the separable blur passes are API-specific here.

#include "blur.hpp"

#include <algorithm>
#include <cmath>

#include "../sdk/includes/imgui/shader_blur_x.h"
#include "../sdk/includes/imgui/shader_blur_y.h"

namespace {
constexpr int k_iterations = 4;

struct resources_t {
    IDirect3DTexture9* a = nullptr;
    IDirect3DTexture9* b = nullptr;
    IDirect3DPixelShader9* horizontal = nullptr;
    IDirect3DPixelShader9* vertical = nullptr;

    IDirect3DSurface9* saved_target = nullptr;
    D3DMATRIX saved_projection{};
    DWORD saved_scissor = FALSE;
    UINT width = 0;
    UINT height = 0;
    UINT blur_width = 0;
    UINT blur_height = 0;
    D3DFORMAT format = D3DFMT_UNKNOWN;
    HRESULT result = S_FALSE;
    bool active = false;
    bool projection_saved = false;

    void release_targets() {
        if (a) { a->Release(); a = nullptr; }
        if (b) { b->Release(); b = nullptr; }
        width = height = blur_width = blur_height = 0;
        format = D3DFMT_UNKNOWN;
    }

    void release_all() {
        release_targets();
        if (horizontal) { horizontal->Release(); horizontal = nullptr; }
        if (vertical) { vertical->Release(); vertical = nullptr; }
        if (saved_target) { saved_target->Release(); saved_target = nullptr; }
        active = false;
    }
};

resources_t resources;

void set_render_target(IDirect3DTexture9* texture) {
    if (!texture)
        return;
    IDirect3DSurface9* surface = nullptr;
    if (SUCCEEDED(texture->GetSurfaceLevel(0, &surface)) && surface) {
        blur::device->SetRenderTarget(0, surface);
        surface->Release();
    }
}

HRESULT create_pipeline() {
    if (resources.horizontal && resources.vertical)
        return S_OK;
    if (!blur::device)
        return E_FAIL;

    HRESULT hr = S_OK;
    if (!resources.horizontal) {
        hr = blur::device->CreatePixelShader(reinterpret_cast<const DWORD*>(blur_x),
            &resources.horizontal);
        if (FAILED(hr))
            return hr;
    }
    if (!resources.vertical) {
        hr = blur::device->CreatePixelShader(reinterpret_cast<const DWORD*>(blur_y),
            &resources.vertical);
        if (FAILED(hr))
            return hr;
    }
    return S_OK;
}

HRESULT create_targets(UINT width, UINT height, D3DFORMAT format) {
    if (resources.a && resources.b && resources.width == width &&
        resources.height == height && resources.format == format)
        return S_OK;

    resources.release_targets();
    if (!blur::device || !width || !height)
        return E_FAIL;

    const UINT blur_width = (std::max)(width / 2u, 1u);
    const UINT blur_height = (std::max)(height / 2u, 1u);

    auto create = [&](IDirect3DTexture9** texture) {
        return blur::device->CreateTexture(blur_width, blur_height, 1,
            D3DUSAGE_RENDERTARGET, format, D3DPOOL_DEFAULT, texture, nullptr);
    };

    HRESULT hr = create(&resources.a);
    if (FAILED(hr))
        return hr;
    hr = create(&resources.b);
    if (FAILED(hr)) {
        resources.release_targets();
        return hr;
    }

    resources.width = width;
    resources.height = height;
    resources.blur_width = blur_width;
    resources.blur_height = blur_height;
    resources.format = format;
    return S_OK;
}

HRESULT prepare() {
    if (!blur::device)
        return E_FAIL;

    HRESULT hr = create_pipeline();
    if (FAILED(hr))
        return hr;

    IDirect3DSurface9* back_buffer = nullptr;
    hr = blur::device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back_buffer);
    if (FAILED(hr) || !back_buffer)
        return FAILED(hr) ? hr : E_FAIL;

    D3DSURFACE_DESC desc{};
    hr = back_buffer->GetDesc(&desc);
    back_buffer->Release();
    if (FAILED(hr))
        return hr;

    return create_targets(desc.Width, desc.Height, desc.Format);
}

void begin_pass(const ImDrawList*, const ImDrawCmd*) {
    resources.result = S_FALSE;
    resources.active = false;
    resources.projection_saved = false;
    if (!blur::device || !resources.a)
        return;

    if (resources.saved_target) {
        resources.saved_target->Release();
        resources.saved_target = nullptr;
    }

    HRESULT hr = blur::device->GetRenderTarget(0, &resources.saved_target);
    if (FAILED(hr) || !resources.saved_target) {
        resources.result = FAILED(hr) ? hr : E_FAIL;
        return;
    }

    IDirect3DSurface9* destination = nullptr;
    hr = resources.a->GetSurfaceLevel(0, &destination);
    if (FAILED(hr) || !destination) {
        resources.result = FAILED(hr) ? hr : E_FAIL;
        return;
    }

    hr = blur::device->StretchRect(resources.saved_target, nullptr, destination, nullptr,
        D3DTEXF_LINEAR);
    destination->Release();
    if (FAILED(hr)) {
        resources.result = hr;
        return;
    }

    blur::device->GetRenderState(D3DRS_SCISSORTESTENABLE, &resources.saved_scissor);
    blur::device->GetVertexShaderConstantF(0, &resources.saved_projection.m[0][0], 4);
    resources.projection_saved = true;

    const float width = static_cast<float>(resources.blur_width);
    const float height = static_cast<float>(resources.blur_height);
    const D3DMATRIX projection{ { {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        -1.0f / width, 1.0f / height, 0.0f, 1.0f
    } } };
    blur::device->SetVertexShaderConstantF(0, &projection.m[0][0], 4);
    blur::device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    blur::device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    blur::device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    blur::device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    blur::device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    resources.active = true;
    resources.result = S_OK;
}

void horizontal_pass(const ImDrawList*, const ImDrawCmd*) {
    if (!resources.active || !blur::device || !resources.horizontal)
        return;
    blur::device->SetPixelShader(resources.horizontal);
    const float parameters[4] = { 1.0f / resources.blur_width, 0.0f, 0.0f, 0.0f };
    blur::device->SetPixelShaderConstantF(0, parameters, 1);
    set_render_target(resources.b);
}

void vertical_pass(const ImDrawList*, const ImDrawCmd*) {
    if (!resources.active || !blur::device || !resources.vertical)
        return;
    blur::device->SetPixelShader(resources.vertical);
    const float parameters[4] = { 1.0f / resources.blur_height, 0.0f, 0.0f, 0.0f };
    blur::device->SetPixelShaderConstantF(0, parameters, 1);
    set_render_target(resources.a);
}

void end_pass(const ImDrawList*, const ImDrawCmd*) {
    if (!blur::device)
        return;
    blur::device->SetPixelShader(nullptr);
    if (resources.saved_target) {
        blur::device->SetRenderTarget(0, resources.saved_target);
        resources.saved_target->Release();
        resources.saved_target = nullptr;
    }
    if (resources.projection_saved)
        blur::device->SetVertexShaderConstantF(0, &resources.saved_projection.m[0][0], 4);
    blur::device->SetRenderState(D3DRS_SCISSORTESTENABLE, resources.saved_scissor);
    resources.projection_saved = false;
    resources.active = false;
}

void submit(ImDrawList* draw, ImVec2 min, ImVec2 max, float alpha, float rounding,
    ImColor tint) {
    if (!draw || !blur::device || !std::isfinite(alpha) || alpha <= 0.0f ||
        max.x <= min.x || max.y <= min.y)
        return;

    resources.result = prepare();
    if (FAILED(resources.result) || !resources.a || !resources.b)
        return;

    alpha = std::clamp(alpha, 0.0f, 1.0f);
    if (rounding < 0.0f)
        rounding = ImGui::GetStyle().WindowRounding;

    draw->AddCallback(begin_pass, nullptr);
    for (int i = 0; i < k_iterations; ++i) {
        draw->AddCallback(horizontal_pass, nullptr);
        draw->AddImage(reinterpret_cast<ImTextureID>(resources.a),
            ImVec2(-1.0f, -1.0f), ImVec2(1.0f, 1.0f));
        draw->AddCallback(vertical_pass, nullptr);
        draw->AddImage(reinterpret_cast<ImTextureID>(resources.b),
            ImVec2(-1.0f, -1.0f), ImVec2(1.0f, 1.0f));
    }
    draw->AddCallback(end_pass, nullptr);
    draw->AddCallback(ImDrawCallback_ResetRenderState, nullptr);

    const float width = static_cast<float>(resources.width);
    const float height = static_cast<float>(resources.height);
    draw->AddImageRounded(reinterpret_cast<ImTextureID>(resources.a), min, max,
        ImVec2(min.x / width, min.y / height), ImVec2(max.x / width, max.y / height),
        IM_COL32(255, 255, 255, IM_F32_TO_INT8_SAT(alpha)), rounding,
        ImDrawFlags_RoundCornersAll);

    const ImVec4& color = tint.Value;
    if (color.w > 0.0f) {
        draw->AddRectFilled(min, max,
            ImColor(color.x, color.y, color.z, color.w * alpha), rounding,
            ImDrawFlags_RoundCornersAll);
    }
}
} // namespace

namespace blur {
HRESULT last_result() {
    return resources.result;
}

void on_device_reset() {
    resources.release_targets();
}

void shutdown() {
    resources.release_all();
    device = nullptr;
}
} // namespace blur

void draw_blur(ImDrawList* draw, float alpha, float rounding) {
    if (draw) {
        submit(draw, draw->GetClipRectMin(), draw->GetClipRectMax(), alpha, rounding,
            ImColor(1.0f, 1.0f, 1.0f, 0.0f));
    }
}

void draw_blur_rounded(ImDrawList* draw, ImVec2 min, ImVec2 max, float alpha,
    float rounding, ImColor tint) {
    submit(draw, min, max, alpha, rounding, tint);
}
