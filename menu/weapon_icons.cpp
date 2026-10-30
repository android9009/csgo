#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "weapon_icons.hpp"
#include "weapon_icons_atlas.hpp"
#include "imgui_impl_dx9.h"
#include "imgui_internal.h"
#include "../sdk/weapon_definitions.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {
static_assert(weapon_icons::atlas::icon_count == WEAPON_COUNT, "Weapon enum and icon atlas differ");
static_assert(sizeof(weapon_icons::atlas::rle) % 3 == 0, "Invalid weapon atlas RLE");

ImTextureID atlas_texture = nullptr;
int next_retry_frame = 0;

ImTextureID texture() {
    if (atlas_texture)
        return atlas_texture;
    if (!ImGui::GetCurrentContext())
        return nullptr;

    const int frame = ImGui::GetFrameCount();
    if (frame < next_retry_frame)
        return nullptr;
    next_retry_frame = frame + 60;

    constexpr std::size_t pixel_count = static_cast<std::size_t>(weapon_icons::atlas::width) *
        weapon_icons::atlas::height;
    std::vector<unsigned char> rgba(pixel_count * 4);
    std::size_t pixel = 0;
    for (std::size_t offset = 0; offset < sizeof(weapon_icons::atlas::rle); offset += 3) {
        const std::size_t count = static_cast<std::size_t>(weapon_icons::atlas::rle[offset]) |
            (static_cast<std::size_t>(weapon_icons::atlas::rle[offset + 1]) << 8);
        const unsigned char alpha = weapon_icons::atlas::rle[offset + 2];
        if (count == 0 || pixel + count > pixel_count) {
            next_retry_frame = frame + 300;
            return nullptr;
        }
        for (std::size_t i = 0; i < count; ++i, ++pixel) {
            rgba[pixel * 4] = 255;
            rgba[pixel * 4 + 1] = 255;
            rgba[pixel * 4 + 2] = 255;
            rgba[pixel * 4 + 3] = alpha;
        }
    }
    if (pixel != pixel_count) {
        next_retry_frame = frame + 300;
        return nullptr;
    }

    atlas_texture = ImGui_CreateTextureRGBA(weapon_icons::atlas::width, weapon_icons::atlas::height, rgba.data());
    return atlas_texture;
}
} // namespace

namespace weapon_icons {
void draw(ImDrawList* draw_list, int weapon, ImVec2 center, ImVec2 max_size, ImU32 color) {
    if (!draw_list || weapon < 0 || weapon >= WEAPON_COUNT || max_size.x <= 0.f || max_size.y <= 0.f)
        return;
    const ImTextureID image = texture();
    if (!image)
        return;

    const atlas::bounds_t& bounds = atlas::bounds[weapon];
    const float glyph_w = static_cast<float>(bounds.x1 - bounds.x0);
    const float glyph_h = static_cast<float>(bounds.y1 - bounds.y0);
    if (glyph_w <= 0.f || glyph_h <= 0.f)
        return;

    const float scale = ImMin(max_size.x / glyph_w, max_size.y / glyph_h);
    const ImVec2 size(glyph_w * scale, glyph_h * scale);
    const ImVec2 rect_min = center - size * 0.5f;
    const ImVec2 rect_max = center + size * 0.5f;

    const float cell_x = static_cast<float>((weapon % atlas::columns) * atlas::cell_width);
    const float cell_y = static_cast<float>((weapon / atlas::columns) * atlas::cell_height);
    const ImVec2 uv0((cell_x + bounds.x0) / atlas::width, (cell_y + bounds.y0) / atlas::height);
    const ImVec2 uv1((cell_x + bounds.x1) / atlas::width, (cell_y + bounds.y1) / atlas::height);

    // The D3D9 backend converts all ImGui vertex colors at the renderer
    // boundary; keep this image on the same path as every other control.
    draw_list->AddImage(image, rect_min, rect_max, uv0, uv1, color);
}

void on_device_reset() {
    if (atlas_texture) {
        ImGui_DestroyTexture(atlas_texture);
        atlas_texture = nullptr;
    }
    next_retry_frame = 0;
}
} // namespace weapon_icons
