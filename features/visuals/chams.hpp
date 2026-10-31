#pragma once

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

// side: 0 = Enemies, 1 = Teammates. Game settings persist independently of the
// currently selected preview tab.
void set_side_passes(int side, const pass_settings& player, const pass_settings& behind_walls,
                     const char* sprite_path);
void set_preview_passes(int side, const pass_settings& player, const pass_settings& behind_walls,
                        const char* sprite_path);
void set_preview_model(const char* model_path, bool active);

// Retry-safe. Installs no hook until the Source 1 interfaces, material helpers
// and the entity/team backend have all been acquired.
bool initialize();
}
