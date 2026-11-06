#pragma once

#include <array>

// Shared material/pass definitions. Gameplay uses IVModelRender::DrawModelExecute;
// the same settings are consumed by the Panorama IStudioRender preview path.
namespace visuals_chams {
enum mode : int {
    disabled = 0,
    textured,
    normal,
    shaded,
    flat,
    metallic,
    glow,
    rim_glow,
    bubble,
    fade_up,
    contour,
    flow,
    acrylic,
    legacy,
    custom_sprite,
    mode_count
};

// Names come from the old Airflow and Fatality sources. Do not reorder without
// migrating the saved per-side values at the same time.
inline constexpr const char* mode_names[mode_count] = {
    "Disabled",
    "Textured",
    "Normal",
    "Shaded",
    "Flat",
    "Metallic",
    "Glow",
    "Rim Glow",
    "Bubble",
    "Fade Up",
    "Contour",
    "Flow",
    "Acrylic",
    "Legacy",
    "Custom Sprite"
};

struct pass_settings {
    int material = disabled;
    float color[4] = { 1.f, 1.f, 1.f, 1.f };
    float secondary[4] = { 1.f, 1.f, 1.f, 1.f };
};

struct pass_list {
    std::array<pass_settings, mode_count> values{};
    int count = 0;
};

struct side_overlay_passes {
    bool enabled = false;
    pass_list player;
    pass_list behind_walls;
    pass_list on_shot;
    pass_list history;
    pass_list ragdolls;
};

struct self_overlay_passes {
    bool enabled = false;
    pass_list local_player;
    pass_list local_fake;
    pass_list viewmodel;
    pass_list weapon;
    pass_list attachments;
};

struct special_passes {
    pass_settings on_shot;
    pass_settings history;
    pass_settings ragdolls;
};

struct self_passes {
    pass_settings local_player;
    pass_settings viewmodel;
    pass_settings weapon;
    pass_settings attachments;
    pass_settings glow;
    self_overlay_passes overlay;
};

// side: 0 = Enemies, 1 = Teammates. Game settings persist independently of the
// currently selected preview tab.
void set_side_passes(int side, const pass_settings& player, const pass_settings& behind_walls,
                     const char* sprite_path);
void set_side_special_passes(int side, const special_passes& passes);
void set_side_overlay_passes(int side, const side_overlay_passes& passes);
void set_self_passes(const self_passes& passes, const char* sprite_path);
void set_preview_passes(int side, const pass_settings& player, const pass_settings& behind_walls,
                        const char* sprite_path);
void set_preview_overlay_passes(int side, const side_overlay_passes& passes,
                                const char* sprite_path);
void set_preview_model(const char* model_path, bool active);

// Retry-safe. Installs the DME/Studio hooks once the Source 1 interfaces and at
// least one usable material are available; entity data is resolved lazily.
bool initialize();
}
