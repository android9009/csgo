#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "fonts.hpp"

#include <Windows.h>

#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "../menu/gui.hpp"
#include "../menu/hashes.hpp"
#include "../menu/menu_resources.h"

namespace {
HMODULE current_module_handle() {
    HMODULE module = nullptr;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&current_module_handle), &module);
    return module;
}

ImFont* add_embedded_font(int resource_id, float size, ImFontConfig config,
    const ImWchar* ranges, bool required_slot = true) {
    HMODULE module = current_module_handle();
    HRSRC resource = module
        ? FindResourceW(module, MAKEINTRESOURCEW(resource_id), MAKEINTRESOURCEW(10)) : nullptr;
    HGLOBAL loaded = resource ? LoadResource(module, resource) : nullptr;
    void* data = loaded ? LockResource(loaded) : nullptr;
    const DWORD data_size = resource ? SizeofResource(module, resource) : 0;

    config.FontDataOwnedByAtlas = false;
    if (data && data_size)
        return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(data,
            static_cast<int>(data_size), size, &config, ranges);

    if (!required_slot)
        return nullptr;

    config.MergeMode = false;
    config.SizePixels = size;
    return ImGui::GetIO().Fonts->AddFontDefault(&config);
}
} // namespace

bool initialize_menu_fonts() {
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    ImGui::StyleColorsDark();
    io.Fonts->Clear();

    static const ImWchar ui_ranges[] = {
        0x0020, 0x00FF,
        0x0100, 0x017F,
        0x0400, 0x052F,
        0
    };
    static const ImWchar icon_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };

    ImFontConfig text_config{};
    text_config.OversampleH = 3;
    text_config.PixelSnapH = true;
    text_config.RasterizerMultiply = 1.0f;

    ImFontConfig ui_config = text_config;
    ui_config.OversampleH = 1;

    // Darkside addresses these ten slots directly. Keep the original order and
    // merge Font Awesome into slots 0 and 6 without creating extra font entries.
    ImFont* menu_body = add_embedded_font(IDR_FONT_INTER_MEDIUM, 15.0f, ui_config, ui_ranges);

    ImFontConfig icon_config{};
    icon_config.MergeMode = true;
    icon_config.PixelSnapH = true;
    add_embedded_font(IDR_FONT_FA_SOLID, 14.0f, icon_config, icon_ranges, false);

    add_embedded_font(IDR_FONT_SST_BOLD, 30.0f, text_config, ui_ranges);          // 1
    add_embedded_font(IDR_FONT_SPACE_GROTESK, 40.0f, text_config, ui_ranges);     // 2
    add_embedded_font(IDR_FONT_INTER_MEDIUM, 17.0f, text_config, ui_ranges);      // 3
    add_embedded_font(IDR_FONT_INTER_SEMIBOLD, 17.0f, text_config, ui_ranges);    // 4
    add_embedded_font(IDR_FONT_MUSEO_BOLD, 17.0f, text_config, ui_ranges);        // 5
    add_embedded_font(IDR_FONT_INTER_SEMIBOLD, 12.5f, text_config, ui_ranges);    // 6

    ImFontConfig caption_icons = icon_config;
    add_embedded_font(IDR_FONT_FA_SOLID, 13.0f, caption_icons, icon_ranges, false);

    add_embedded_font(IDR_FONT_INTER_MEDIUM, 13.0f, ui_config, ui_ranges);        // 7
    add_embedded_font(IDR_FONT_INTER_SEMIBOLD, 22.0f, text_config, ui_ranges);    // 8
    add_embedded_font(IDR_FONT_INTER_MEDIUM, 17.0f, text_config, ui_ranges);      // 9

    io.FontDefault = menu_body ? menu_body : (io.Fonts->Fonts.Size ? io.Fonts->Fonts[0] : nullptr);
    io.Fonts->Build();
    gui.apply_theme(0);
    gui.apply_style_colors();
    return ImGui_ImplDX9_CreateDeviceObjects();
}
