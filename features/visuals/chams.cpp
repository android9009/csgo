#include "chams.hpp"
#include "source.hpp"

#include <Windows.h>
#include "MinHook.h"
#include "../../runtime/debug_console.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace {
static_assert(sizeof(void*) == 4, "Source 1 game/preview chams require the x86 ABI");

struct vector3 {
    float x, y, z;
};

struct matrix3x4_t {
    float m[3][4];
};

struct studiohdr_t {
    int id;
    int version;
    int checksum;
    char name[64];
};

// Only the leading fields used by the preview filter are declared. The draw
// function receives a reference to the engine's full DrawModelInfo_t.
struct draw_model_info_t {
    studiohdr_t* studio_hdr;
    void* hardware_data;
    void* decals;
    int skin;
    int body;
    int hitbox_set;
    void* client_entity;
};

struct model_t {
    void* handle;
    char name[MAX_PATH];
};

struct model_render_info_t {
    vector3 origin;
    vector3 angles;
    char padding[4];
    void* renderable;
    const model_t* model;
    const matrix3x4_t* model_to_world;
    const matrix3x4_t* lighting_offset;
    const vector3* lighting_origin;
    int flags;
    int entity_index;
    int skin;
    int body;
    int hitbox_set;
    unsigned short instance;
};

// Only the leading StudioRender field is needed by the engine-DME filter.
struct draw_model_state_t {
    studiohdr_t* studio_hdr;
};

using studio_draw_t = void(__thiscall*)(void*, void*, const draw_model_info_t&,
    matrix3x4_t*, float*, float*, const vector3&, int);
using model_execute_t = void(__thiscall*)(void*, void*, const draw_model_state_t&,
    const model_render_info_t&, matrix3x4_t*);

studio_draw_t original_studio_draw = nullptr;
model_execute_t original_model_execute = nullptr;
void* studio_render = nullptr;
void* model_render = nullptr;
void* render_view = nullptr;
void* material_system = nullptr;
void* key_values_system = nullptr;
void* key_values_init = nullptr;
void* key_values_load = nullptr;
void* materials[visuals_chams::mode_count] = {};
std::atomic<bool> hooks_ready = false;
std::atomic<ULONGLONG> next_initialize_attempt = 0;
thread_local int engine_model_draw_depth = 0;
thread_local bool in_game_chams = false;

constexpr int k_shadow_depth_texture = 0x200;
constexpr int k_material_var_nofog = 1 << 14;
constexpr int k_material_var_ignorez = 1 << 15;
constexpr int k_material_var_translucent = 1 << 21;

struct pass_pair {
    bool configured = false;
    visuals_chams::pass_settings player;
    visuals_chams::pass_settings behind_walls;
    char sprite_path[128] = "models/weapons/customization/paints/custom/money";
};

struct preview_settings {
    bool active = false;
    int side = -1;
    char model[96] = {};
    pass_pair passes;
};

std::mutex settings_mutex;
std::mutex hook_initialize_mutex;
pass_pair game_settings[2];
preview_settings preview;
char applied_sprite_path[128] = {};

bool readable_range(const void* address, std::size_t size) {
    if (!address || !size)
        return false;
    auto current = reinterpret_cast<std::uintptr_t>(address);
    const auto end = current + size;
    if (end < current)
        return false;
    while (current < end) {
        MEMORY_BASIC_INFORMATION information{};
        if (!VirtualQuery(reinterpret_cast<const void*>(current), &information,
                          sizeof(information)) ||
            information.State != MEM_COMMIT ||
            (information.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
            return false;
        }
        const auto begin = reinterpret_cast<std::uintptr_t>(information.BaseAddress);
        const auto region_end = begin + information.RegionSize;
        if (region_end <= begin || current < begin || current >= region_end)
            return false;
        current = std::min<std::uintptr_t>(end, region_end);
    }
    return true;
}

template <typename Fn>
Fn virtual_function(void* instance, std::size_t index) {
    if (!readable_range(instance, sizeof(void*)))
        return nullptr;
    void** table = *reinterpret_cast<void***>(instance);
    if (!readable_range(table, (index + 1) * sizeof(void*)))
        return nullptr;
    return reinterpret_cast<Fn>(table[index]);
}

bool module_text(HMODULE module, const std::uint8_t*& text, std::size_t& size) {
    if (!module)
        return false;
    const auto* base = reinterpret_cast<const std::uint8_t*>(module);
    if (!readable_range(base, sizeof(IMAGE_DOS_HEADER)))
        return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE ||
        !readable_range(base + dos->e_lfanew, sizeof(IMAGE_NT_HEADERS))) {
        return false;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return false;
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (std::memcmp(section->Name, ".text", 5) != 0)
            continue;
        const auto virtual_size = static_cast<std::size_t>(section->Misc.VirtualSize);
        const auto raw_size = static_cast<std::size_t>(section->SizeOfRawData);
        size = virtual_size && raw_size ? std::min(virtual_size, raw_size)
                                        : std::max(virtual_size, raw_size);
        text = base + section->VirtualAddress;
        return text && size;
    }
    return false;
}

bool function_in_module(HMODULE module, const void* function) {
    const std::uint8_t* text = nullptr;
    std::size_t size = 0;
    if (!module_text(module, text, size) || !function)
        return false;
    const auto address = reinterpret_cast<std::uintptr_t>(function);
    const auto begin = reinterpret_cast<std::uintptr_t>(text);
    return address >= begin && address - begin < size && readable_range(function, 1);
}

template <std::size_t N>
const std::uint8_t* find_pattern(HMODULE module, const int (&pattern)[N]) {
    const std::uint8_t* text = nullptr;
    std::size_t size = 0;
    if (!module_text(module, text, size) || N > size)
        return nullptr;
    for (std::size_t offset = 0; offset + N <= size; ++offset) {
        bool matches = true;
        for (std::size_t i = 0; i < N; ++i) {
            if (pattern[i] >= 0 && text[offset + i] != static_cast<std::uint8_t>(pattern[i])) {
                matches = false;
                break;
            }
        }
        if (matches && readable_range(text + offset, N))
            return text + offset;
    }
    return nullptr;
}

void* create_interface(HMODULE module, const char* name) {
    if (!module)
        return nullptr;
    using create_interface_t = void* (__cdecl*)(const char*, int*);
    const auto factory = reinterpret_cast<create_interface_t>(
        GetProcAddress(module, "CreateInterface"));
    return factory ? factory(name, nullptr) : nullptr;
}

// Build 2023 IKeyValuesSystem: allocation/free are slots 2/3, not the legacy 1/2 pair.
using key_values_alloc_t = void* (__thiscall*)(void*, int);
using key_values_free_t = void(__thiscall*)(void*, void*);
using key_values_init_t = void(__thiscall*)(void*, const char*, int, int);
using key_values_load_t = bool(__thiscall*)(void*, const char*, const char*,
    void*, void*, void*, void*);
using create_material_t = void* (__thiscall*)(void*, const char*, void*);
using find_material_t = void* (__thiscall*)(void*, const char*, const char*, bool,
    const char*);

void* make_material(const char* name, const char* shader, const char* definition) {
    const auto allocate = virtual_function<key_values_alloc_t>(key_values_system, 2);
    const auto init = reinterpret_cast<key_values_init_t>(key_values_init);
    const auto load = reinterpret_cast<key_values_load_t>(key_values_load);
    const auto create = virtual_function<create_material_t>(material_system, 83);
    if (!allocate || !init || !load || !create)
        return nullptr;

    // Overallocate to cover both the older 40-byte and final 44-byte KeyValues
    // layouts. The engine constructor initializes the actual object; ownership
    // is transferred to CreateMaterial just like in the Source references.
    void* values = allocate(key_values_system, 64);
    if (!readable_range(values, 64))
        return nullptr;
    std::memset(values, 0, 64);
    init(values, shader, 0, 0);
    if (!load(values, name, definition, nullptr, nullptr, nullptr, nullptr)) {
        const auto release = virtual_function<key_values_free_t>(key_values_system, 3);
        if (release)
            release(key_values_system, values);
        return nullptr;
    }
    return create(material_system, name, values);
}

struct material_definition {
    const char* name;
    const char* shader;
    const char* text;
};

material_definition definition_for(int mode) {
    using namespace visuals_chams;
    switch (mode) {
    case textured:
        return { "darkside_preview_textured", "VertexLitGeneric", R"("VertexLitGeneric"
{
    "$basetexture" "vgui/white"
    "$nofog" "1"
    "$model" "1"
    "$nocull" "0"
    "$selfillum" "1"
    "$halflambert" "1"
    "$znearer" "0"
    "$flat" "1"
    "$ignorez" "0"
})" };
    case normal:
        return { "darkside_preview_normal", "VertexLitGeneric", R"("VertexLitGeneric"
{
    "$basetexture" "vgui/white"
    "$nofog" "1"
    "$model" "1"
    "$nocull" "0"
    "$selfillum" "1"
    "$halflambert" "1"
    "$znearer" "0"
    "$flat" "1"
})" };
    case shaded:
        return { "darkside_preview_shaded", "VertexLitGeneric", R"("VertexLitGeneric"
{
    "$basetexture" "vgui/white"
    "$phong" "1"
    "$phongexponent" "0"
    "$phongboost" "0"
    "$rimlight" "1"
    "$rimlightexponent" "0"
    "$rimlightboost" "0"
    "$model" "1"
    "$nocull" "0"
    "$nofog" "0"
    "$ignorez" "0"
    "$halflambert" "0"
    "$basemapalphaphongmask" "1"
})" };
    case flat:
        return { "darkside_preview_flat", "UnlitGeneric", R"("UnlitGeneric"
{
    "$basetexture" "vgui/white"
    "$ignorez" "0"
    "$envmap" ""
    "$nofog" "1"
    "$model" "1"
    "$nocull" "0"
    "$selfillum" "1"
    "$halflambert" "1"
    "$znearer" "0"
    "$flat" "1"
})" };
    case metallic:
        return { "darkside_preview_metallic", "VertexLitGeneric", R"("VertexLitGeneric"
{
    "$basetexture" "vgui/white"
    "$phong" "1"
    "$basemapalphaphongmask" "1"
    "$phongexponent" "15"
    "$normalmapalphaenvmask" "1"
    "$envmap" "env_cubemap"
    "$envmaptint" "[0.6 0.6 0.6]"
    "$phongboost" "0.6"
    "$phongfresnelranges" "[0.5 0.5 1.0]"
    "$nofog" "1"
    "$ignorez" "0"
    "$model" "1"
    "$nocull" "0"
    "$selfillum" "1"
    "$halflambert" "1"
    "$znearer" "0"
    "$flat" "1"
})" };
    case glow:
        return { "darkside_preview_glow", "VertexLitGeneric", R"("VertexLitGeneric"
{
    "$basetexture" "vgui/white"
    "$additive" "1"
    "$envmap" "models/effects/cube_white"
    "$envmaptint" "[1 1 1]"
    "$envmapfresnel" "1"
    "$envmapfresnelminmaxexp" "[0 1 2]"
    "$alpha" "1"
    "$nofog" "1"
    "$ignorez" "0"
    "$model" "1"
})" };
    case rim_glow:
        return { "darkside_preview_rim_glow", "VertexLitGeneric", R"("VertexLitGeneric"
{
    "$basetexture" "vgui/white"
    "$additive" "1"
    "$envmap" "models/effects/cube_white"
    "$envmaptint" "[1 1 1]"
    "$envmapfresnel" "1"
    "$envmapfresnelminmaxexp" "[0 1 2]"
    "$alpha" "0.8"
    "$nofog" "1"
    "$model" "1"
})" };
    case bubble:
        return { "darkside_preview_bubble", "VertexLitGeneric", R"("VertexLitGeneric"
{
    "$basetexture" "vgui/white"
    "$additive" "1"
    "$nofog" "1"
    "$ignorez" "0"
    "$model" "1"
    "$basemapalphaphongmask" "1"
    "$envmap" "env_cubemap"
    "$envmapfresnel" "1"
    "$envmaptint" "[.2 .2 .2]"
})" };
    case fade_up:
        return { "darkside_preview_fade_up", "VertexLitGeneric", R"("VertexLitGeneric"
{
    "$basetexture" "vgui/white"
    "$basemapalphaphongmask" "1"
    "$envmap" "env_cubemap"
    "$envmapfresnel" "1"
    "$envmaptint" "[.2 .2 .2]"
    "$model" "1"
    "$nofog" "1"
})" };
    case contour:
        return { "darkside_preview_contour", "VertexLitGeneric", R"("VertexLitGeneric"
{
    "$basetexture" "vgui/white"
    "$bumpmap" "vgui/white"
    "$selfillum" "1"
    "$selfIllumFresnel" "1"
    "$selfIllumFresnelMinMaxExp" "[0 0.18 0.1]"
    "$selfillumtint" "[0.3 0.001 0.1]"
    "$model" "1"
    "$nofog" "1"
})" };
    case flow:
        return { "darkside_preview_flow", "UnlitGeneric", R"("UnlitGeneric"
{
    "$basetexture" "dev/zone_warning"
    "$model" "1"
    "$nofog" "1"
    "proxies"
    {
        "TextureScroll"
        {
            "texturescrollvar" "$basetexturetransform"
            "texturescrollrate" "0.1"
            "texturescrollangle" "90"
        }
    }
})" };
    case acrylic:
        return { "darkside_preview_acrylic", "VertexLitGeneric", R"("VertexLitGeneric"
{
    "$basetexture" "vgui/white"
    "$bumpmap" "de_nuke/hr_nuke/pool_water_normals_002"
    "$bumptransform" "center 0.5 0.5 scale 0.25 0.25 rotate 0.0 translate 0.0 0.0"
    "$nofog" "1"
    "$model" "1"
    "$color2" "[1.0 1.0 1.0]"
    "$halflambert" "1"
    "$envmap" "env_cubemap"
    "$envmaptint" "[0.1 0.1 0.1]"
    "$envmapfresnel" "1.0"
    "$envmapfresnelminmaxexp" "[1.0 1.0 1.0]"
    "$phong" "1"
    "$phongexponent" "1024"
    "$phongboost" "4.0"
    "$phongfresnelranges" "[1.0 1.0 1.0]"
    "$rimlight" "1"
    "$rimlightexponent" "4.0"
    "$rimlightboost" "2.0"
})" };
    case custom_sprite:
        return { "darkside_preview_custom_sprite", "VertexLitGeneric", R"("VertexLitGeneric"
{
    "$basetexture" "models/weapons/customization/paints/custom/money"
    "$basemapalphaphongmask" "1"
    "$envmap" "env_cubemap"
    "$envmapfresnel" "1"
    "$envmaptint" "[.2 .2 .2]"
    "$model" "1"
    "$nofog" "1"
})" };
    default:
        return {};
    }
}

using increment_ref_t = void(__thiscall*)(void*);
using find_var_t = void* (__thiscall*)(void*, const char*, bool*, bool);
using material_float_t = void(__thiscall*)(void*, float);
using material_color_t = void(__thiscall*)(void*, float, float, float);
using material_flag_t = void(__thiscall*)(void*, int, bool);
using material_var_vec_t = void(__thiscall*)(void*, float, float, float);
using material_var_string_t = void(__thiscall*)(void*, const char*);
using studio_forced_t = void(__thiscall*)(void*, void*, int, int);
using studio_color_t = void(__thiscall*)(void*, const float*);
using studio_alpha_t = void(__thiscall*)(void*, float);
using render_color_t = void(__thiscall*)(void*, const float*);
using render_alpha_t = void(__thiscall*)(void*, float);

void increment_ref(void* material) {
    const auto function = virtual_function<increment_ref_t>(material, 12);
    if (function)
        function(material);
}

void* find_var(void* material, const char* name) {
    const auto function = virtual_function<find_var_t>(material, 11);
    return function ? function(material, name, nullptr, false) : nullptr;
}

void set_var_vector(void* material, const char* name, const float* value) {
    if (void* variable = find_var(material, name)) {
        const auto function = virtual_function<material_var_vec_t>(variable, 11);
        if (function)
            function(variable, value[0], value[1], value[2]);
    }
}

void set_var_string(void* material, const char* name, const char* value) {
    if (void* variable = find_var(material, name)) {
        const auto function = virtual_function<material_var_string_t>(variable, 6);
        if (function)
            function(variable, value);
    }
}

void set_material_color(void* material, float r, float g, float b) {
    const auto function = virtual_function<material_color_t>(material, 28);
    if (function)
        function(material, r, g, b);
}

void set_material_alpha(void* material, float alpha) {
    const auto function = virtual_function<material_float_t>(material, 27);
    if (function)
        function(material, alpha);
}

void set_material_flag(void* material, int flag, bool enabled) {
    const auto function = virtual_function<material_flag_t>(material, 29);
    if (function)
        function(material, flag, enabled);
}

void force_material(void* material, bool game = false) {
    if (game) {
        const auto function = virtual_function<studio_forced_t>(model_render, 1);
        if (function)
            function(model_render, material, 0, 0);
        return;
    }
    const auto function = virtual_function<studio_forced_t>(studio_render, 33);
    if (function)
        function(studio_render, material, 0, -1);
}

void set_draw_color(const float* color, bool game = false) {
    if (game) {
        const auto function = virtual_function<render_color_t>(render_view, 6);
        if (function)
            function(render_view, color);
        return;
    }
    const auto function = virtual_function<studio_color_t>(studio_render, 27);
    if (function)
        function(studio_render, color);
}

void set_draw_alpha(float alpha, bool game = false) {
    if (game) {
        const auto function = virtual_function<render_alpha_t>(render_view, 4);
        if (function)
            function(render_view, alpha);
        return;
    }
    const auto function = virtual_function<studio_alpha_t>(studio_render, 28);
    if (function)
        function(studio_render, alpha);
}

void reset_modulation(bool game = false) {
    constexpr float white[3] = { 1.f, 1.f, 1.f };
    set_draw_color(white, game);
    set_draw_alpha(1.f, game);
}

struct draw_call {
    void* results = nullptr;
    const draw_model_info_t* info = nullptr;
    matrix3x4_t* bones = nullptr;
    float* flex_weights = nullptr;
    float* delayed_flex_weights = nullptr;
    const vector3* model_origin = nullptr;
    int flags = 0;
    bool game = false;
    void* context = nullptr;
    const draw_model_state_t* state = nullptr;
    const model_render_info_t* render_info = nullptr;
};

void draw_original(const draw_call& call) {
    if (call.game) {
        original_model_execute(model_render, call.context, *call.state, *call.render_info, call.bones);
        return;
    }
    original_studio_draw(studio_render, call.results, *call.info, call.bones,
        call.flex_weights, call.delayed_flex_weights, *call.model_origin, call.flags);
}

void draw_single_material(const draw_call& call,
    const visuals_chams::pass_settings& pass, bool hidden,
    const char* sprite_path) {
    using namespace visuals_chams;
    void* material = pass.material > disabled && pass.material < mode_count
        ? materials[pass.material]
        : nullptr;
    if (!material) {
        force_material(nullptr, call.game);
        draw_original(call);
        return;
    }

    if (pass.material == custom_sprite && sprite_path && sprite_path[0] &&
        std::strncmp(applied_sprite_path, sprite_path, sizeof(applied_sprite_path)) != 0) {
        set_var_string(material, "$basetexture", sprite_path);
        std::strncpy(applied_sprite_path, sprite_path, sizeof(applied_sprite_path) - 1);
        applied_sprite_path[sizeof(applied_sprite_path) - 1] = '\0';
    }

    set_material_color(material, 1.f, 1.f, 1.f);
    // Alpha is applied once through IStudioRender, not multiplied by a second
    // material alpha modulation. The shader's own $alpha remains intact.
    set_material_alpha(material, 1.f);
    set_material_flag(material, k_material_var_translucent, true);
    set_material_flag(material, k_material_var_nofog, true);
    set_material_flag(material, k_material_var_ignorez, hidden);

    constexpr float white[3] = { 1.f, 1.f, 1.f };
    const float* modulation = pass.color;
    switch (pass.material) {
    case glow: {
        set_var_vector(material, "$envmaptint", pass.secondary);
        // Old Airflow's full fill setting: clamp(1.0 - 0.15) -> exponent .75.
        constexpr float fresnel[3] = { 0.f, 1.f, 0.75f };
        set_var_vector(material, "$envmapfresnelminmaxexp", fresnel);
        break;
    }
    case rim_glow:
        set_var_vector(material, "$envmaptint", pass.secondary);
        set_material_color(material, 0.f, 0.f, 0.f);
        modulation = white;
        break;
    case contour:
        set_var_vector(material, "$selfillumtint", pass.secondary);
        break;
    case legacy:
        set_var_vector(material, "$envmaptint", pass.color);
        modulation = white;
        break;
    case fade_up:
        set_var_vector(material, "$envmaptint", pass.color);
        set_material_color(material, 0.f, 0.f, 0.f);
        break;
    default:
        // Old Airflow also modulates envmap tint for Shaded/Metallic/Bubble,
        // Custom Sprite and Acrylic. Materials without this var simply skip it.
        set_var_vector(material, "$envmaptint", pass.color);
        break;
    }

    set_draw_color(modulation, call.game);
    set_draw_alpha(std::clamp(pass.color[3], 0.f, 1.f), call.game);
    force_material(material, call.game);
    draw_original(call);
    force_material(nullptr, call.game);
}

void draw_pass(const draw_call& call, const visuals_chams::pass_settings& pass,
               bool hidden, const char* sprite_path) {
    using namespace visuals_chams;
    if (pass.material == glow || pass.material == rim_glow || pass.material == legacy) {
        auto base = pass;
        base.material = flat;
        draw_single_material(call, base, hidden, sprite_path);
    }
    draw_single_material(call, pass, hidden, sprite_path);
}

char normalized_character(char value) {
    if (value == '\\')
        value = '/';
    if (value >= 'A' && value <= 'Z')
        value = static_cast<char>(value - 'A' + 'a');
    return value;
}

bool model_name_matches(const char* loaded, const char* requested) {
    if (!loaded || !requested)
        return false;
    // studiohdr_t::name is exactly 64 bytes and the requested preview path is
    // capped at 96. Never probe loaded[i + 1] beyond either fixed buffer.
    char loaded_normalized[65] = {};
    char requested_normalized[97] = {};
    for (std::size_t i = 0; i < 64 && loaded[i]; ++i)
        loaded_normalized[i] = normalized_character(loaded[i]);
    for (std::size_t i = 0; i < 96 && requested[i]; ++i)
        requested_normalized[i] = normalized_character(requested[i]);
    return loaded_normalized[0] && std::strcmp(loaded_normalized, requested_normalized) == 0;
}

bool player_model_name(const char* name, std::size_t maximum) {
    if (!name || !maximum)
        return false;
    char normalized[MAX_PATH + 1] = {};
    maximum = std::min<std::size_t>(maximum, MAX_PATH);
    std::size_t length = 0;
    while (length < maximum && name[length]) {
        normalized[length] = normalized_character(name[length]);
        ++length;
    }
    normalized[length] = '\0';
    return std::strncmp(normalized, "models/player/", 14) == 0 &&
           std::strstr(normalized, ".mdl") != nullptr;
}

void __fastcall hooked_studio_draw(void* self, void*, void* results,
    const draw_model_info_t& info, matrix3x4_t* bones, float* flex_weights,
    float* delayed_flex_weights, const vector3& model_origin, int flags) {
    if (!hooks_ready.load(std::memory_order_acquire) || engine_model_draw_depth > 0 ||
        (flags & k_shadow_depth_texture) != 0 || !info.studio_hdr) {
        original_studio_draw(self, results, info, bones, flex_weights,
            delayed_flex_weights, model_origin, flags);
        return;
    }

    preview_settings snapshot;
    {
        std::scoped_lock lock(settings_mutex);
        snapshot = preview;
    }
    const auto& passes = snapshot.passes;
    if (!snapshot.active || !passes.configured ||
        !model_name_matches(info.studio_hdr->name, snapshot.model) ||
        (passes.player.material == visuals_chams::disabled &&
         passes.behind_walls.material == visuals_chams::disabled)) {
        original_studio_draw(self, results, info, bones, flex_weights,
            delayed_flex_weights, model_origin, flags);
        return;
    }

    const draw_call call{ results, &info, bones, flex_weights, delayed_flex_weights,
        &model_origin, flags };
    if (passes.behind_walls.material != visuals_chams::disabled)
        draw_pass(call, passes.behind_walls, true, passes.sprite_path);
    if (passes.player.material != visuals_chams::disabled)
        draw_pass(call, passes.player, false, passes.sprite_path);
    else {
        // Behind Walls is a separate pass; the normal Player model still needs
        // to be drawn above it when the Player combo is Disabled.
        reset_modulation();
        force_material(nullptr);
        draw_original(call);
    }

    reset_modulation();
    force_material(nullptr);
}

void __fastcall hooked_model_execute(void* self, void*, void* context,
    const draw_model_state_t& state, const model_render_info_t& info,
    matrix3x4_t* bones) {
    struct engine_draw_scope {
        engine_draw_scope() { ++engine_model_draw_depth; }
        ~engine_draw_scope() { --engine_model_draw_depth; }
    } scope;
    if (!hooks_ready.load(std::memory_order_acquire) || in_game_chams) {
        original_model_execute(self, context, state, info, bones);
        return;
    }

    const int side = visuals_source::player_side(info.entity_index);
    const char* model_name = info.model ? info.model->name :
        (state.studio_hdr ? state.studio_hdr->name : nullptr);
    const std::size_t model_name_size = info.model ? MAX_PATH : 64;
    if (side < 0 || !player_model_name(model_name, model_name_size)) {
        original_model_execute(self, context, state, info, bones);
        return;
    }

    pass_pair snapshot;
    {
        std::scoped_lock lock(settings_mutex);
        snapshot = game_settings[side];
    }
    if (!snapshot.configured ||
        (snapshot.player.material == visuals_chams::disabled &&
         snapshot.behind_walls.material == visuals_chams::disabled)) {
        original_model_execute(self, context, state, info, bones);
        return;
    }

    in_game_chams = true;
    draw_call call;
    call.bones = bones;
    call.game = true;
    call.context = context;
    call.state = &state;
    call.render_info = &info;
    reset_modulation(true);
    if (snapshot.behind_walls.material != visuals_chams::disabled)
        draw_pass(call, snapshot.behind_walls, true, snapshot.sprite_path);
    if (snapshot.player.material != visuals_chams::disabled)
        draw_pass(call, snapshot.player, false, snapshot.sprite_path);
    else {
        force_material(nullptr, true);
        draw_original(call);
    }
    force_material(nullptr, true);
    reset_modulation(true);
    in_game_chams = false;
}

bool acquire_interfaces() {
    const HMODULE studio_module = GetModuleHandleW(L"studiorender.dll");
    const HMODULE engine_module = GetModuleHandleW(L"engine.dll");
    const HMODULE material_module = GetModuleHandleW(L"materialsystem.dll");
    const HMODULE client_module = GetModuleHandleW(L"client.dll");
    const HMODULE vstdlib_module = GetModuleHandleW(L"vstdlib.dll");
    if (!studio_module || !engine_module || !material_module || !client_module ||
        !vstdlib_module) {
        return false;
    }

    studio_render = create_interface(studio_module, "VStudioRender026");
    model_render = create_interface(engine_module, "VEngineModel016");
    render_view = create_interface(engine_module, "VEngineRenderView014");
    material_system = create_interface(material_module, "VMaterialSystem080");
    using key_values_system_factory_t = void* (__cdecl*)();
    const auto key_values_factory = reinterpret_cast<key_values_system_factory_t>(
        GetProcAddress(vstdlib_module, "KeyValuesSystem"));
    key_values_system = key_values_factory ? key_values_factory() : nullptr;
    if (!studio_render || !model_render || !render_view || !material_system || !key_values_system)
        return false;

    const auto key_values_allocate = virtual_function<key_values_alloc_t>(key_values_system, 2);
    const auto key_values_free = virtual_function<key_values_free_t>(key_values_system, 3);
    if (!function_in_module(vstdlib_module, reinterpret_cast<const void*>(key_values_allocate)) ||
        !function_in_module(vstdlib_module, reinterpret_cast<const void*>(key_values_free)))
        return false;

    const auto studio_target = virtual_function<studio_draw_t>(studio_render, 29);
    const auto model_target = virtual_function<model_execute_t>(model_render, 21);
    const auto create = virtual_function<create_material_t>(material_system, 83);
    if (!function_in_module(studio_module, reinterpret_cast<const void*>(studio_target)) ||
        !function_in_module(engine_module, reinterpret_cast<const void*>(model_target)) ||
        !function_in_module(material_module, reinterpret_cast<const void*>(create))) {
        return false;
    }

    static constexpr int load_pattern[] = {
        0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x83, 0xEC,
        0x34, 0x53, 0x8B, 0x5D, 0x0C, 0x89
    };
    static constexpr int init_call_pattern[] = {
        0xE8, -1, -1, -1, -1, 0x8B, 0xF0, 0xEB, 0x02,
        0x33, 0xF6, 0x8B, 0x45, 0x08, 0x8B, 0x78
    };
    const auto* load = find_pattern(client_module, load_pattern);
    const auto* init_call = find_pattern(client_module, init_call_pattern);
    if (!load || !init_call)
        return false;
    std::int32_t relative = 0;
    std::memcpy(&relative, init_call + 1, sizeof(relative));
    const auto* init = init_call + 5 + relative;
    if (!function_in_module(client_module, load) ||
        !function_in_module(client_module, init)) {
        return false;
    }
    key_values_load = const_cast<std::uint8_t*>(load);
    key_values_init = const_cast<std::uint8_t*>(init);
    return true;
}

bool create_materials() {
    using namespace visuals_chams;
    for (int mode = textured; mode < mode_count; ++mode) {
        if (mode == legacy || materials[mode])
            continue;
        const auto definition = definition_for(mode);
        if (!definition.name)
            return false;
        materials[mode] = make_material(definition.name, definition.shader, definition.text);
        if (!materials[mode])
            return false;
        increment_ref(materials[mode]);
    }
    if (!materials[legacy]) {
        const auto find = virtual_function<find_material_t>(material_system, 84);
        if (!find)
            return false;
        materials[legacy] = find(material_system, "dev/glow_armsrace", "Model textures",
            false, nullptr);
        if (!materials[legacy])
            return false;
        increment_ref(materials[legacy]);
    }
    return true;
}
} // namespace

namespace visuals_chams {
namespace {
void configure_pair(pass_pair& target, int side, const pass_settings& player,
                    const pass_settings& behind_walls, const char* sprite_path) {
    target.configured = side == 0 || side == 1;
    target.player = player;
    target.behind_walls = behind_walls;
    const auto sanitize = [](pass_settings& pass) {
        if (pass.material <= disabled || pass.material >= mode_count)
            pass.material = disabled;
        for (int i = 0; i < 4; ++i) {
            pass.color[i] = std::clamp(pass.color[i], 0.f, 1.f);
            pass.secondary[i] = std::clamp(pass.secondary[i], 0.f, 1.f);
        }
    };
    sanitize(target.player);
    sanitize(target.behind_walls);
    if (sprite_path && sprite_path[0]) {
        std::strncpy(target.sprite_path, sprite_path, sizeof(target.sprite_path) - 1);
        target.sprite_path[sizeof(target.sprite_path) - 1] = '\0';
    }
}
}

void set_side_passes(int side, const pass_settings& player, const pass_settings& behind_walls,
                     const char* sprite_path) {
    if (side != 0 && side != 1)
        return;
    std::scoped_lock lock(settings_mutex);
    configure_pair(game_settings[side], side, player, behind_walls, sprite_path);
}

void set_preview_passes(int side, const pass_settings& player, const pass_settings& behind_walls,
                        const char* sprite_path) {
    if (side != 0 && side != 1)
        return;
    std::scoped_lock lock(settings_mutex);
    preview.side = side;
    configure_pair(preview.passes, side, player, behind_walls, sprite_path);
}

void set_preview_model(const char* model_path, bool active) {
    std::scoped_lock lock(settings_mutex);
    preview.active = active;
    if (model_path && model_path[0]) {
        std::strncpy(preview.model, model_path, sizeof(preview.model) - 1);
        preview.model[sizeof(preview.model) - 1] = '\0';
    }
}

bool initialize() {
    if (hooks_ready.load(std::memory_order_acquire))
        return true;
    const ULONGLONG now = GetTickCount64();
    if (now < next_initialize_attempt.load(std::memory_order_relaxed))
        return false;
    next_initialize_attempt.store(now + 2000, std::memory_order_relaxed);
    std::scoped_lock lock(hook_initialize_mutex);
    if (hooks_ready.load(std::memory_order_relaxed))
        return true;

    if ((!studio_render || !model_render || !render_view || !material_system || !key_values_system ||
         !key_values_init || !key_values_load) && !acquire_interfaces()) {
        return false;
    }
    if (!visuals_source::initialize() || !create_materials())
        return false;

    const MH_STATUS minhook_status = MH_Initialize();
    if (minhook_status != MH_OK && minhook_status != MH_ERROR_ALREADY_INITIALIZED)
        return false;

    const auto studio_target = virtual_function<studio_draw_t>(studio_render, 29);
    const auto model_target = virtual_function<model_execute_t>(model_render, 21);
    if (!studio_target || !model_target)
        return false;

    auto* studio_address = reinterpret_cast<void*>(studio_target);
    auto* model_address = reinterpret_cast<void*>(model_target);
    if (MH_CreateHook(studio_address, reinterpret_cast<void*>(&hooked_studio_draw),
                      reinterpret_cast<void**>(&original_studio_draw)) != MH_OK) {
        return false;
    }
    if (MH_CreateHook(model_address, reinterpret_cast<void*>(&hooked_model_execute),
                      reinterpret_cast<void**>(&original_model_execute)) != MH_OK) {
        MH_RemoveHook(studio_address);
        original_studio_draw = nullptr;
        return false;
    }
    const MH_STATUS model_enable = MH_EnableHook(model_address);
    if (model_enable != MH_OK) {
        MH_RemoveHook(studio_address);
        MH_RemoveHook(model_address);
        original_studio_draw = nullptr;
        original_model_execute = nullptr;
        return false;
    }
    const MH_STATUS studio_enable = MH_EnableHook(studio_address);
    if (studio_enable != MH_OK) {
        MH_DisableHook(model_address);
        MH_RemoveHook(studio_address);
        MH_RemoveHook(model_address);
        original_studio_draw = nullptr;
        original_model_execute = nullptr;
        return false;
    }

    hooks_ready.store(true, std::memory_order_release);
    AIRFLOW_DEBUG_LOG("visuals game DME and Panorama StudioRender chams hooks installed");
    return true;
}
} // namespace visuals_chams
