#pragma once

#include <d3d9.h>
#include <cstdint>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#include "img/stb_image.h"

// Загрузка текстуры из памяти (PNG/JPG) без D3DX (legacy DirectX SDK).
static inline IDirect3DTexture9* LoadTextureFromMemory(IDirect3DDevice9* device,
                                                       const unsigned char* data,
                                                       int data_size) {
    if (!device || !data || data_size <= 0)
        return nullptr;

    int w = 0, h = 0, comp = 0;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* pixels = stbi_load_from_memory(data, data_size, &w, &h, &comp, 4);
    if (!pixels)
        return nullptr;

    IDirect3DTexture9* texture = nullptr;
    if (FAILED(device->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8,
                                     D3DPOOL_MANAGED, &texture, nullptr))) {
        stbi_image_free(pixels);
        return nullptr;
    }

    D3DLOCKED_RECT rect{};
    if (SUCCEEDED(texture->LockRect(0, &rect, nullptr, 0))) {
        const uint8_t* src = pixels;
        for (int y = 0; y < h; ++y) {
            uint8_t* dst = static_cast<uint8_t*>(rect.pBits) + y * rect.Pitch;
            const uint8_t* row = src + (size_t)y * w * 4;
            for (int x = 0; x < w; ++x) {
                dst[x * 4 + 0] = row[x * 4 + 2];  // B
                dst[x * 4 + 1] = row[x * 4 + 1];  // G
                dst[x * 4 + 2] = row[x * 4 + 0];  // R
                dst[x * 4 + 3] = row[x * 4 + 3];  // A
            }
        }
        texture->UnlockRect(0);
    }

    stbi_image_free(pixels);
    return texture;
}
