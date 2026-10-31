#include "agent_preview.hpp"

// Keep Win32/D3D declarations ahead of menu.hpp's standard-library namespace imports.
#include <Windows.h>
#include <d3d9.h>

#include "menu.hpp"
#include "../runtime/debug_console.hpp"
#include "../sdk/config_system/config_system.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

namespace {
struct preview_state {
    std::mutex mutex;
    bool drawn = false;
    bool requested_active = false;
    int requested_side = -1;
    RECT requested_rect{};

    // None of these pointers is used until the reference ABI checks below have
    // succeeded for the loaded DLLs. There is no non-Panorama renderer.
    HMODULE client_module = nullptr;
    HMODULE core_module = nullptr;
    void* panorama_wrapper = nullptr;
    void* engine = nullptr;
    void* context = nullptr;
    bool abi_checked = false;
    bool abi_valid = false;
    bool abi_failure_logged = false;
    ULONGLONG next_engine_retry = 0;
    ULONGLONG next_context_check = 0;

    bool panel_created = false;
    bool panel_visible = false;
    bool panel_ready = false;
    bool panel_hide_sent = false;
    int panel_side = -1;
    int panel_agent = -1;
    unsigned panel_background = 0;
    RECT panel_rect{};
    ULONGLONG next_panel_check = 0;
    ULONGLONG next_hide_attempt = 0;

    IDirect3DDevice9* capture_device = nullptr;
    IDirect3DTexture9* capture_texture = nullptr;
    D3DFORMAT capture_format = D3DFMT_UNKNOWN;
    int capture_width = 0;
    int capture_height = 0;
    bool prefer_full_frame = false;
    ULONGLONG capture_retry_after = 0;

    bool capture_valid = false;
    bool capture_full_frame = false;
    int captured_side = -1;
    RECT captured_rect{};
    ImVec2 captured_uv0{ 0.f, 0.f };
    ImVec2 captured_uv1{ 1.f, 1.f };
};

preview_state& state() {
    static preview_state value;
    return value;
}

bool valid_rect(const RECT& rect) {
    return rect.right - rect.left >= 8 && rect.bottom - rect.top >= 8;
}

bool same_rect(const RECT& a, const RECT& b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

bool same_size(const RECT& a, const RECT& b) {
    return a.right - a.left == b.right - b.left && a.bottom - a.top == b.bottom - b.top;
}

RECT make_rect(const ImVec2& min, const ImVec2& max) {
    // Panorama layout and the D3D9 copy must use the same integer rectangle.
    return RECT{
        static_cast<LONG>(std::floor(min.x)),
        static_cast<LONG>(std::floor(min.y)),
        static_cast<LONG>(std::ceil(max.x)),
        static_cast<LONG>(std::ceil(max.y))
    };
}

constexpr const char* k_terrorist_models[] = {
    "models/player/custom_player/legacy/tm_phoenix.mdl",
    "models/player/custom_player/legacy/tm_anarchist_varianta.mdl",
    "models/player/custom_player/legacy/tm_anarchist_variantb.mdl",
    "models/player/custom_player/legacy/tm_anarchist_variantc.mdl",
    "models/player/custom_player/legacy/tm_anarchist_variantd.mdl",
    "models/player/custom_player/legacy/tm_balkan_varianta.mdl",
    "models/player/custom_player/legacy/tm_balkan_variantb.mdl",
    "models/player/custom_player/legacy/tm_balkan_variantc.mdl",
    "models/player/custom_player/legacy/tm_balkan_variantd.mdl",
    "models/player/custom_player/legacy/tm_balkan_variante.mdl",
    "models/player/custom_player/legacy/tm_balkan_variantf.mdl",
    "models/player/custom_player/legacy/tm_balkan_variantg.mdl",
    "models/player/custom_player/legacy/tm_balkan_varianth.mdl",
    "models/player/custom_player/legacy/tm_balkan_varianti.mdl",
    "models/player/custom_player/legacy/tm_balkan_variantj.mdl",
    "models/player/custom_player/legacy/tm_balkan_variantk.mdl",
    "models/player/custom_player/legacy/tm_balkan_variantl.mdl",
    "models/player/custom_player/legacy/tm_leet_varianta.mdl",
    "models/player/custom_player/legacy/tm_leet_variantb.mdl",
    "models/player/custom_player/legacy/tm_leet_variantc.mdl",
    "models/player/custom_player/legacy/tm_leet_variantd.mdl",
};

constexpr const char* k_counter_terrorist_models[] = {
    "models/player/custom_player/legacy/ctm_sas.mdl",
    "models/player/custom_player/legacy/ctm_fbi_varianta.mdl",
    "models/player/custom_player/legacy/ctm_fbi_variantb.mdl",
    "models/player/custom_player/legacy/ctm_fbi_variantc.mdl",
    "models/player/custom_player/legacy/ctm_fbi_variantd.mdl",
    "models/player/custom_player/legacy/ctm_fbi_variante.mdl",
    "models/player/custom_player/legacy/ctm_fbi_variantf.mdl",
    "models/player/custom_player/legacy/ctm_fbi_variantg.mdl",
    "models/player/custom_player/legacy/ctm_fbi_varianth.mdl",
    "models/player/custom_player/legacy/ctm_gign_varianta.mdl",
    "models/player/custom_player/legacy/ctm_gign_variantb.mdl",
    "models/player/custom_player/legacy/ctm_gign_variantc.mdl",
    "models/player/custom_player/legacy/ctm_gign_variantd.mdl",
    "models/player/custom_player/legacy/ctm_gsg9_varianta.mdl",
    "models/player/custom_player/legacy/ctm_gsg9_variantb.mdl",
    "models/player/custom_player/legacy/ctm_gsg9_variantc.mdl",
    "models/player/custom_player/legacy/ctm_gsg9_variantd.mdl",
    "models/player/custom_player/legacy/ctm_st6_varianta.mdl",
    "models/player/custom_player/legacy/ctm_st6_variantb.mdl",
    "models/player/custom_player/legacy/ctm_st6_variantc.mdl",
    "models/player/custom_player/legacy/ctm_st6_variantd.mdl",
};

static_assert(IM_ARRAYSIZE(k_terrorist_models) == 21);
static_assert(IM_ARRAYSIZE(k_counter_terrorist_models) == 21);

int selected_agent(int side) {
    const int selected = side == 0
        ? g_cfg->skins.m_agents.m_selected
        : g_cfg->skins.m_agents.m_selected_ct;
    return std::clamp(selected, 0, static_cast<int>(IM_ARRAYSIZE(k_terrorist_models)) - 1);
}

const char* agent_model(int side, int agent) {
    return side == 0 ? k_terrorist_models[agent] : k_counter_terrorist_models[agent];
}

unsigned menu_background() {
    const auto channel = [](float value) {
        return static_cast<unsigned>(std::lround(std::clamp(value, 0.f, 1.f) * 255.f));
    };
    return (channel(gui.panel_body.r) << 16) |
           (channel(gui.panel_body.g) << 8) |
           channel(gui.panel_body.b);
}

bool readable_range(const void* address, std::size_t size) {
    if (!address || !size)
        return false;
    MEMORY_BASIC_INFORMATION information{};
    if (!VirtualQuery(address, &information, sizeof(information)) ||
        information.State != MEM_COMMIT ||
        (information.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
        return false;
    }
    const auto begin = reinterpret_cast<std::uintptr_t>(address);
    const auto end = begin + size;
    const auto region_begin = reinterpret_cast<std::uintptr_t>(information.BaseAddress);
    const auto region_end = region_begin + information.RegionSize;
    return end >= begin && end <= region_end;
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
        const std::size_t virtual_size = section->Misc.VirtualSize;
        const std::size_t raw_size = section->SizeOfRawData;
        size = virtual_size && raw_size ? std::min(virtual_size, raw_size)
                                        : std::max(virtual_size, raw_size);
        text = base + section->VirtualAddress;
        return text && size && readable_range(text, size);
    }
    return false;
}

template <std::size_t N>
bool function_matches(HMODULE module, const void* function, const int (&pattern)[N]) {
    const std::uint8_t* text = nullptr;
    std::size_t size = 0;
    if (!module_text(module, text, size) || !function)
        return false;
    const auto address = reinterpret_cast<std::uintptr_t>(function);
    const auto begin = reinterpret_cast<std::uintptr_t>(text);
    if (address < begin || address - begin > size || N > size - (address - begin))
        return false;
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(address);
    for (std::size_t i = 0; i < N; ++i) {
        if (pattern[i] >= 0 && bytes[i] != static_cast<std::uint8_t>(pattern[i]))
            return false;
    }
    return true;
}

// These short fingerprints come from the 32-bit CS:GO Panorama reference binaries,
// not from CS2 or a community offset list. The vtable slots are used only when the
// corresponding function and module still match. Otherwise no preview texture is drawn.
constexpr int k_access_engine_pattern[] = {
    0x8B, 0x81, 0x24, 0x01, 0x00, 0x00, 0xC3
};
constexpr int k_last_target_pattern[] = {
    0x8B, 0x81, 0x14, 0x05, 0x00, 0x00, 0xC3
};
constexpr int k_valid_panel_pattern[] = {
    0x55, 0x8B, 0xEC, 0x8B, 0x81, 0xFC, 0x00, 0x00, 0x00,
    0x83, 0xF8, 0xFF, 0x74, 0x32, 0x56, 0x8B, 0x75, 0x08, 0x57
};
constexpr int k_run_script_pattern[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x81, 0xEC,
    0x74, 0x01, 0x00, 0x00, 0x53, 0x8B, 0xD9,
    0x8B, 0x0D, -1, -1, -1, -1, 0x56, 0x57,
    0x89, 0x5C, 0x24, 0x0C, 0x8B, 0x81, 0x0C, 0x10, 0x00, 0x00
};

void forget_panel_locked(preview_state& s) {
    s.panel_created = false;
    s.panel_visible = false;
    s.panel_ready = false;
    s.panel_hide_sent = false;
    s.panel_side = -1;
    s.panel_agent = -1;
    s.next_panel_check = 0;
    s.next_hide_attempt = 0;
    s.capture_valid = false;
}

using access_ui_engine_t = void* (__thiscall*)(void*);
using is_valid_panel_t = bool(__thiscall*)(void*, void*);
using get_last_target_t = void* (__thiscall*)(void*);
using run_script_t = void(__thiscall*)(void*, void*, const char*, const char*,
    int, int, bool, bool);

void note_abi_failure_locked(preview_state& s, const char* reason) {
    if (s.abi_failure_logged)
        return;
    s.abi_failure_logged = true;
    (void)reason;
    AIRFLOW_DEBUG_LOGF("agent_preview Panorama unavailable: %s", reason);
}

bool validate_engine_locked(preview_state& s, void* engine) {
    if (!engine || !s.core_module)
        return false;
    const auto run = virtual_function<run_script_t>(engine, 113);
    const auto last = virtual_function<get_last_target_t>(engine, 56);
    const auto valid = virtual_function<is_valid_panel_t>(engine, 36);
    return function_matches(s.core_module, run, k_run_script_pattern) &&
           function_matches(s.core_module, last, k_last_target_pattern) &&
           function_matches(s.core_module, valid, k_valid_panel_pattern);
}

void* acquire_engine_locked(preview_state& s, ULONGLONG now) {
    if (s.abi_valid) {
        const auto access = virtual_function<access_ui_engine_t>(s.panorama_wrapper, 11);
        if (!access)
            return nullptr;
        void* current = access(s.panorama_wrapper);
        if (!current || !validate_engine_locked(s, current)) {
            note_abi_failure_locked(s, "engine ABI changed");
            s.abi_valid = false;
            s.abi_checked = true;
            s.engine = nullptr;
            s.context = nullptr;
            forget_panel_locked(s);
            return nullptr;
        }
        if (current != s.engine) {
            s.engine = current;
            s.context = nullptr;
            forget_panel_locked(s);
        }
        return s.engine;
    }

    if (s.abi_checked || now < s.next_engine_retry)
        return nullptr;
    s.next_engine_retry = now + 1000;

    // PanoramaUIEngine001 is exposed by panoramauiclient.dll. panorama.dll contains
    // the IUIEngine implementation and is deliberately not used as the factory here.
    const HMODULE client = GetModuleHandleW(L"panoramauiclient.dll");
    const HMODULE core = GetModuleHandleW(L"panorama.dll");
    if (!client || !core) {
        note_abi_failure_locked(s, "Panorama modules are not loaded");
        return nullptr;
    }

    using create_interface_t = void* (__cdecl*)(const char*, int*);
    const auto create_interface = reinterpret_cast<create_interface_t>(
        GetProcAddress(client, "CreateInterface"));
    if (!create_interface) {
        note_abi_failure_locked(s, "CreateInterface export is missing");
        s.abi_checked = true;
        return nullptr;
    }

    void* wrapper = create_interface("PanoramaUIEngine001", nullptr);
    if (!wrapper) {
        note_abi_failure_locked(s, "PanoramaUIEngine001 is unavailable");
        return nullptr;
    }

    const auto access = virtual_function<access_ui_engine_t>(wrapper, 11);
    if (!function_matches(client, access, k_access_engine_pattern)) {
        note_abi_failure_locked(s, "AccessUIEngine fingerprint mismatch");
        s.abi_checked = true;
        return nullptr;
    }

    void* engine = access(wrapper);
    s.client_module = client;
    s.core_module = core;
    if (!engine || !validate_engine_locked(s, engine)) {
        note_abi_failure_locked(s, "IUIEngine fingerprint mismatch");
        s.abi_checked = true;
        return nullptr;
    }

    s.panorama_wrapper = wrapper;
    s.engine = engine;
    s.abi_valid = true;
    s.abi_failure_logged = false;
    AIRFLOW_DEBUG_LOG("agent_preview Panorama ABI validated");
    return engine;
}

bool valid_panel_locked(preview_state& s, void* panel) {
    if (!s.abi_valid || !s.engine || !panel)
        return false;
    const auto is_valid = virtual_function<is_valid_panel_t>(s.engine, 36);
    return is_valid && is_valid(s.engine, panel);
}

void* context_for_script_locked(preview_state& s, ULONGLONG now) {
    void* engine = acquire_engine_locked(s, now);
    if (!engine)
        return nullptr;

    if (s.context && now < s.next_context_check)
        return s.context;
    s.next_context_check = now + 250;

    const auto get_last = virtual_function<get_last_target_t>(engine, 56);
    void* last = get_last ? get_last(engine) : nullptr;
    if (last && valid_panel_locked(s, last)) {
        s.context = last;
        return last;
    }
    if (s.context && valid_panel_locked(s, s.context))
        return s.context;
    s.context = nullptr;
    return nullptr;
}

bool run_script_locked(preview_state& s, void* context, const std::string& script) {
    void* engine = acquire_engine_locked(s, GetTickCount64());
    if (!engine || !context || !valid_panel_locked(s, context))
        return false;
    const auto run = virtual_function<run_script_t>(engine, 113);
    if (!run)
        return false;
    AIRFLOW_DEBUG_STAGE("agent_preview.Panorama.RunScript");
    run(engine, context, script.c_str(), "panorama/layout/base_mainmenu.xml",
        8, 10, false, false);
    return true;
}

constexpr char k_host_id[] = "DarksideEspAgentPreview";
constexpr char k_model_id[] = "DarksideEspAgentPreviewModel";
constexpr char k_manifest[] = "resource/ui/econ/ItemModelPanelCharWeaponInspect.res";
constexpr ULONGLONG k_panel_check_ms = 1000;
constexpr ULONGLONG k_capture_retry_ms = 1000;

std::string root_script_prefix() {
    // RunScript needs a valid panel context, but the parent walk belongs in JS:
    // it avoids depending on another private IUIPanel vtable layout in C++.
    return "(function(){var c=$.GetContextPanel();if(!c)return;var r=c;var root=null;"
           "for(var i=0;i<32&&r;i++){if(r.id==='CSGOHud'||r.id==='CSGOMainMenu')root=r;"
           "var parent=r.GetParent();if(!parent)break;r=parent;}if(!root)return;var d=root.Data();";
}

std::string panel_script(const RECT& rect, int side, int agent, unsigned background, bool reload) {
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    const char* team = side == 0 ? "t" : "ct";

    char color[16];
    std::snprintf(color, sizeof(color), "#%06X", background & 0xFFFFFFu);
    const std::string x = std::to_string(rect.left);
    const std::string y = std::to_string(rect.top);
    const std::string w = std::to_string(width);
    const std::string h = std::to_string(height);
    const std::string host_style =
        "flow-children:none;position:" + x + "px " + y + "px 0px;" +
        "horizontal-align:left;vertical-align:top;width:" + w + "px;height:" + h + "px;" +
        "overflow:clip;z-index:100000;background-color:" + color + ";";

    std::string script = root_script_prefix();
    script +=
        "var h=d.DarksideEspAgentPreview;var p=d.DarksideEspAgentPreviewModel;var made=false;"
        "if(h&&!h.IsValid()){h=null;p=null;}if(p&&!p.IsValid())p=null;"
        "if(!h)h=root.FindChildTraverse('" + std::string(k_host_id) + "');"
        "if(!h){h=$.CreatePanel('Panel',root,'" + k_host_id + "',{hittest:false,style:'" + host_style + "'});made=true;}"
        "if(!h)return;"
        "if(!p)p=h.FindChildTraverse('" + k_model_id + "');"
        "if(!p){p=$.CreatePanel('ItemPreviewPanel',h,'" + k_model_id + "',{hittest:false,mouse_rotate:false,"
        "'force-no-composition-layer':true,style:'width:100%;height:100%;horizontal-align:left;vertical-align:top;'});made=true;}"
        "if(!p)return;d.DarksideEspAgentPreview=h;d.DarksideEspAgentPreviewModel=p;"
        "h.style.position='" + x + "px " + y + "px 0px';"
        "h.style.width='" + w + "px';h.style.height='" + h + "px';"
        "h.style.backgroundColor='" + color + "';h.style.zIndex=100000;h.visible=true;";

    script += "if(made||";
    script += reload ? "true" : "false";
    script += "){p.ResetAnimation(true);p.SetSceneAngles(0,0,0,false);p.SetScene('";
    script += k_manifest;
    script += "','";
    script += agent_model(side, agent);
    script += "',false);p.SetPlayerModel('";
    script += agent_model(side, agent);
    script += "');p.ResetActivityModifiers();p.ApplyActivityModifier('";
    script += team;
    script += "');p.PlayActivity('ACT_CSGO_UIPLAYER_BUYMENU',true);p.SetCameraPreset(5,false);}";
    script += "p.EnableRendering(true);p.Pause(false);})();";
    return script;
}

void synchronize_panel_locked(preview_state& s, const RECT& rect, int side, ULONGLONG now) {
    void* context = context_for_script_locked(s, now);
    if (!context)
        return;

    const int agent = selected_agent(side);
    const unsigned background = menu_background();
    const bool reload = s.panel_side != side || s.panel_agent != agent;
    const bool changed = reload || !same_rect(s.panel_rect, rect) ||
                         s.panel_background != background;
    if (s.panel_created && s.panel_visible && s.panel_ready && !changed &&
        now < s.next_panel_check) {
        return;
    }

    if (!run_script_locked(s, context, panel_script(rect, side, agent, background, reload))) {
        s.next_panel_check = now + k_panel_check_ms;
        return;
    }

    s.panel_created = true;
    s.panel_visible = true;
    s.panel_ready = true;
    s.panel_hide_sent = false;
    s.panel_side = side;
    s.panel_agent = agent;
    s.panel_background = background;
    s.panel_rect = rect;
    s.next_panel_check = now + k_panel_check_ms;
}

void hide_panel_locked(preview_state& s) {
    if (!s.panel_created || !s.panel_visible || s.panel_hide_sent)
        return;
    const ULONGLONG now = GetTickCount64();
    if (now < s.next_hide_attempt)
        return;
    s.next_hide_attempt = now + k_panel_check_ms;

    void* context = context_for_script_locked(s, now);
    if (!context || !s.panel_created || !s.panel_visible)
        return;

    std::string script = root_script_prefix();
    script +=
        "var h=d.DarksideEspAgentPreview;if(h&&!h.IsValid())h=null;"
        "if(!h)h=root.FindChildTraverse('" + std::string(k_host_id) + "');if(!h)return;h.visible=false;"
        "var p=d.DarksideEspAgentPreviewModel;if(p&&!p.IsValid())p=null;"
        "if(!p)p=h.FindChildTraverse('" + k_model_id + "');"
        "if(p){p.EnableRendering(false);p.Pause(true);}})();";
    if (run_script_locked(s, context, script)) {
        s.panel_hide_sent = true;
        s.panel_visible = false;
        s.panel_ready = false;
    }
}

void release_capture_locked(preview_state& s) {
    if (s.capture_texture)
        s.capture_texture->Release();
    s.capture_texture = nullptr;
    s.capture_device = nullptr;
    s.capture_format = D3DFMT_UNKNOWN;
    s.capture_width = 0;
    s.capture_height = 0;
    s.capture_valid = false;
}

bool ensure_capture_texture_locked(preview_state& s, IDirect3DDevice9* device,
                                   int width, int height, D3DFORMAT format) {
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096)
        return false;
    if (s.capture_texture && s.capture_device == device && s.capture_width == width &&
        s.capture_height == height && s.capture_format == format) {
        return true;
    }

    release_capture_locked(s);
    IDirect3DTexture9* texture = nullptr;
    if (FAILED(device->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, format,
                                     D3DPOOL_DEFAULT, &texture, nullptr)) || !texture) {
        return false;
    }

    s.capture_device = device;
    s.capture_texture = texture;
    s.capture_format = format;
    s.capture_width = width;
    s.capture_height = height;
    return true;
}

bool capture_backbuffer_locked(preview_state& s, IDirect3DDevice9* device) {
    if (!device || !valid_rect(s.requested_rect))
        return false;

    IDirect3DSurface9* backbuffer = nullptr;
    if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer)) || !backbuffer)
        return false;

    D3DSURFACE_DESC description{};
    HRESULT result = backbuffer->GetDesc(&description);
    if (FAILED(result) || !description.Width || !description.Height) {
        backbuffer->Release();
        return false;
    }

    RECT source_rect = s.requested_rect;
    source_rect.left = std::clamp(source_rect.left, 0L, static_cast<LONG>(description.Width));
    source_rect.top = std::clamp(source_rect.top, 0L, static_cast<LONG>(description.Height));
    source_rect.right = std::clamp(source_rect.right, 0L, static_cast<LONG>(description.Width));
    source_rect.bottom = std::clamp(source_rect.bottom, 0L, static_cast<LONG>(description.Height));
    if (!valid_rect(source_rect) || !same_rect(source_rect, s.requested_rect)) {
        backbuffer->Release();
        return false;
    }

    bool full_frame = s.prefer_full_frame;
    const int copy_width = full_frame ? static_cast<int>(description.Width)
                                      : static_cast<int>(source_rect.right - source_rect.left);
    const int copy_height = full_frame ? static_cast<int>(description.Height)
                                       : static_cast<int>(source_rect.bottom - source_rect.top);
    if (!ensure_capture_texture_locked(s, device, copy_width, copy_height, description.Format)) {
        backbuffer->Release();
        return false;
    }

    IDirect3DSurface9* target = nullptr;
    result = s.capture_texture->GetSurfaceLevel(0, &target);
    if (SUCCEEDED(result) && target) {
        result = device->StretchRect(backbuffer, full_frame ? nullptr : &source_rect,
                                     target, nullptr, D3DTEXF_NONE);
        target->Release();
    }

    // Some multisampled D3D9 drivers only permit a whole-frame resolve.
    if (FAILED(result) && !full_frame &&
        ensure_capture_texture_locked(s, device, static_cast<int>(description.Width),
                                      static_cast<int>(description.Height), description.Format)) {
        target = nullptr;
        result = s.capture_texture->GetSurfaceLevel(0, &target);
        if (SUCCEEDED(result) && target) {
            result = device->StretchRect(backbuffer, nullptr, target, nullptr, D3DTEXF_NONE);
            target->Release();
        }
        full_frame = SUCCEEDED(result);
    }
    backbuffer->Release();

    if (FAILED(result))
        return false;

    s.prefer_full_frame = full_frame;
    s.capture_full_frame = full_frame;
    if (full_frame) {
        s.captured_uv0 = ImVec2(static_cast<float>(source_rect.left) / description.Width,
                                static_cast<float>(source_rect.top) / description.Height);
        s.captured_uv1 = ImVec2(static_cast<float>(source_rect.right) / description.Width,
                                static_cast<float>(source_rect.bottom) / description.Height);
    } else {
        s.captured_uv0 = ImVec2(0.f, 0.f);
        s.captured_uv1 = ImVec2(1.f, 1.f);
    }
    s.captured_rect = s.requested_rect;
    s.captured_side = s.requested_side;
    return true;
}
} // namespace

namespace agent_preview {
void begin_frame(IDirect3DDevice9* device) {
    auto& s = state();
    std::scoped_lock lock(s.mutex);
    s.drawn = false;
    s.capture_valid = false;
    if (!s.requested_active || !s.panel_ready || !s.panel_visible ||
        s.panel_side != s.requested_side || !same_rect(s.panel_rect, s.requested_rect)) {
        return;
    }

    const ULONGLONG now = GetTickCount64();
    if (now < s.capture_retry_after)
        return;
    if (capture_backbuffer_locked(s, device)) {
        s.capture_valid = true;
        s.capture_retry_after = 0;
    } else {
        s.capture_retry_after = now + k_capture_retry_ms;
    }
}

void draw(const ImVec2& min, const ImVec2& max, int side) {
    auto& s = state();
    std::scoped_lock lock(s.mutex);
    if (!ImGui::GetCurrentContext() || (side != 0 && side != 1) ||
        max.x - min.x < 1.f || max.y - min.y < 1.f) {
        s.drawn = false;
        s.requested_active = false;
        return;
    }

    const RECT rect = make_rect(min, max);
    if (!valid_rect(rect)) {
        s.drawn = false;
        s.requested_active = false;
        return;
    }

    s.drawn = true;
    s.requested_active = true;
    s.requested_side = side;
    s.requested_rect = rect;
    synchronize_panel_locked(s, rect, side, GetTickCount64());

    if (s.capture_valid && s.capture_texture && s.captured_side == side &&
        same_size(s.captured_rect, rect)) {
        const float alpha = ImGui::GetStyle().Alpha;
        ImGui::GetWindowDrawList()->AddImage(
            reinterpret_cast<ImTextureID>(s.capture_texture), min, max,
            s.captured_uv0, s.captured_uv1,
            ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, alpha)));
        return;
    }

    // Panorama-only: there is deliberately no static image fallback.
}

void end_frame() {
    auto& s = state();
    std::scoped_lock lock(s.mutex);
    if (s.drawn)
        return;
    s.requested_active = false;
    s.capture_valid = false;
    hide_panel_locked(s);
    release_capture_locked(s);
}

void on_device_reset() {
    auto& s = state();
    std::scoped_lock lock(s.mutex);
    release_capture_locked(s);
    s.prefer_full_frame = false;
    s.capture_retry_after = 0;
    s.panel_ready = false;
    s.next_panel_check = 0;
}
} // namespace agent_preview
