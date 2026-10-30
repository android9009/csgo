#include "agent_preview.hpp"

// Windows SDK headers must precede gui.hpp's `using namespace std`; otherwise rpcndr/objidl
// see both std::byte and the Win32 byte typedef under C++20.
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
#include <string>

namespace {
constexpr char k_host_id[] = "DarksideEspAgentPreview";
constexpr char k_model_id[] = "DarksideEspAgentPreviewModel";
constexpr char k_manifest[] = "resource/ui/econ/ItemModelPanelCharMainMenu.res";
constexpr ULONGLONG k_panel_check_ms = 1000;
constexpr ULONGLONG k_capture_retry_ms = 2000;

// These are the CS:GO legacy player models used by Panorama's own loadout/inspect UI.
// Index zero deliberately remains a regular model; the other entries follow the existing 0..20
// agent sliders instead of introducing another agent selector in the ESP page.
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

struct preview_state {
    void* panorama_engine = nullptr;
    void* root = nullptr;
    HMODULE root_slot_module = nullptr;
    void** main_menu_client_slot = nullptr;
    void** hud_client_slot = nullptr;
    ULONGLONG next_root_check = 0;
    ULONGLONG next_panel_check = 0;
    ULONGLONG next_hide_attempt = 0;

    bool panel_created = false;
    bool panel_visible = false;
    bool panel_hide_sent = false;
    bool panel_ready = false;
    int panel_side = -1;
    int panel_agent = -1;
    unsigned panel_background = 0;
    RECT panel_rect{};

    bool drawn = false;
    bool requested_active = false;
    int requested_side = -1;
    RECT requested_rect{};

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
    return RECT{
        static_cast<LONG>(std::floor(min.x)),
        static_cast<LONG>(std::floor(min.y)),
        static_cast<LONG>(std::ceil(max.x)),
        static_cast<LONG>(std::ceil(max.y))
    };
}

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

template <typename Fn>
Fn virtual_function(void* instance, size_t index) {
    if (!instance)
        return nullptr;
    void** table = *reinterpret_cast<void***>(instance);
    return table ? reinterpret_cast<Fn>(table[index]) : nullptr;
}

template <size_t N>
const std::uint8_t* find_pattern(const std::uint8_t* bytes, size_t size,
                                 const int (&pattern)[N]) {
    if (!bytes || size < N)
        return nullptr;
    for (size_t offset = 0; offset <= size - N; ++offset) {
        size_t index = 0;
        for (; index < N; ++index) {
            if (pattern[index] >= 0 &&
                bytes[offset + index] != static_cast<std::uint8_t>(pattern[index]))
                break;
        }
        if (index == N)
            return bytes + offset;
    }
    return nullptr;
}

bool module_text(HMODULE module, const std::uint8_t*& text, size_t& size) {
    if (!module)
        return false;
    const auto* base = reinterpret_cast<const std::uint8_t*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return false;

    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (std::memcmp(section->Name, ".text", 5) != 0)
            continue;
        const size_t virtual_size = section->Misc.VirtualSize;
        const size_t raw_size = section->SizeOfRawData;
        size = virtual_size && raw_size ? std::min(virtual_size, raw_size)
                                        : std::max(virtual_size, raw_size);
        text = base + section->VirtualAddress;
        return text && size;
    }
    return false;
}

void** absolute_pointer_operand(const std::uint8_t* store_instruction) {
    // Both signatures end at `mov [absolute], edi` (89 3D imm32) in the 32-bit client.
    std::uint32_t address = 0;
    std::memcpy(&address, store_instruction + 2, sizeof(address));
    return reinterpret_cast<void**>(static_cast<std::uintptr_t>(address));
}

void resolve_root_slots() {
    auto& s = state();
    const HMODULE client = GetModuleHandleW(L"client.dll");
    if (!client || s.root_slot_module == client)
        return;

    // A loaded module cannot change its code in place. Scan at most once per client.dll instance.
    s.root_slot_module = client;
    s.main_menu_client_slot = nullptr;
    s.hud_client_slot = nullptr;

    const std::uint8_t* text = nullptr;
    size_t text_size = 0;
    if (!module_text(client, text, text_size))
        return;

    // March-2023 CS:GO Windows x86 constructors. Each stores its CPanel2D-derived singleton,
    // whose +4 member is IUIPanel*. The corresponding destructors clear these globals.
    // Wildcards cover only vtable/string addresses and the resolved singleton address itself.
    static constexpr int main_menu_pattern[] = {
        0x8B, 0x4F, 0x04, 0x6A, 0x00,
        0xC7, 0x47, 0x58, 0x00, 0x00, 0x00, 0x00,
        0xC7, 0x47, 0x5C, 0x00, 0x00, 0x00, 0x00,
        0x8B, 0x01, 0x6A, 0x00, 0x68, -1, -1, -1, -1,
        0x89, 0x3D, -1, -1, -1, -1, 0xFF, 0x50, 0x34
    };
    static constexpr int hud_pattern[] = {
        0x8B, 0x4F, 0x04, 0x6A, 0x00,
        0xC7, 0x07, -1, -1, -1, -1,
        0xC7, 0x47, 0x14, -1, -1, -1, -1,
        0xC7, 0x06, -1, -1, -1, -1,
        0x8B, 0x01, 0x6A, 0x00, 0x68, -1, -1, -1, -1,
        0x89, 0x3D, -1, -1, -1, -1, 0xFF, 0x50, 0x30
    };
    constexpr size_t main_menu_store = 28;
    constexpr size_t hud_store = 33;
    static_assert(main_menu_pattern[main_menu_store] == 0x89);
    static_assert(hud_pattern[hud_store] == 0x89);

    if (const auto* match = find_pattern(text, text_size, main_menu_pattern))
        s.main_menu_client_slot = absolute_pointer_operand(match + main_menu_store);
    if (const auto* match = find_pattern(text, text_size, hud_pattern))
        s.hud_client_slot = absolute_pointer_operand(match + hud_store);
}

void* root_from_slot(void** slot) {
    if (!slot)
        return nullptr;
    void* client_panel = *slot;
    if (!client_panel)
        return nullptr;
    return *reinterpret_cast<void**>(static_cast<std::uint8_t*>(client_panel) + sizeof(void*));
}

struct root_candidates {
    void* main_menu = nullptr;
    void* hud = nullptr;
};

root_candidates direct_roots() {
    resolve_root_slots();
    auto& s = state();
    return { root_from_slot(s.main_menu_client_slot), root_from_slot(s.hud_client_slot) };
}

void* ui_engine() {
    auto& s = state();
    if (!s.panorama_engine) {
        const HMODULE module = GetModuleHandleW(L"panorama.dll");
        if (!module)
            return nullptr;
        using create_interface_t = void* (__cdecl*)(const char*, int*);
        const auto create_interface = reinterpret_cast<create_interface_t>(
            GetProcAddress(module, "CreateInterface"));
        if (!create_interface)
            return nullptr;
        AIRFLOW_DEBUG_STAGE("panorama.CreateInterface");
        s.panorama_engine = create_interface("PanoramaUIEngine001", nullptr);
        AIRFLOW_DEBUG_STAGE("panorama.CreateInterface returned");
        AIRFLOW_DEBUG_LOGF("panorama CreateInterface: %p", s.panorama_engine);
        if (!s.panorama_engine)
            return nullptr;
    }

    using access_ui_engine_t = void* (__thiscall*)(void*);
    const auto access = virtual_function<access_ui_engine_t>(s.panorama_engine, 11);
    AIRFLOW_DEBUG_STAGE("panorama.AccessUIEngine");
    void* engine = access ? access(s.panorama_engine) : nullptr;
    AIRFLOW_DEBUG_STAGE("panorama.AccessUIEngine returned");
    return engine;
}

bool valid_panel(void* engine, void* panel) {
    using is_valid_panel_t = bool(__thiscall*)(void*, void*);
    const auto is_valid = virtual_function<is_valid_panel_t>(engine, 36);
    if (!panel || !is_valid)
        return false;
    AIRFLOW_DEBUG_STAGE("panorama.IsValidPanelPointer");
    const bool valid = is_valid(engine, panel);
    AIRFLOW_DEBUG_STAGE("panorama.IsValidPanelPointer returned");
    return valid;
}

bool panel_laid_out(void* panel) {
    // CS:GO Windows x86 IUIPanel ordinal. The corresponding Linux declaration is 316 because
    // Itanium has an additional destructor slot; do not use the Linux index here.
    using has_been_laid_out_t = bool(__thiscall*)(const void*);
    const auto has_been_laid_out = virtual_function<has_been_laid_out_t>(panel, 315);
    if (!panel || !has_been_laid_out)
        return false;
    AIRFLOW_DEBUG_STAGE("panorama.IUIPanel.BHasBeenLayedOut");
    const bool laid_out = has_been_laid_out(panel);
    AIRFLOW_DEBUG_STAGE("panorama.IUIPanel.BHasBeenLayedOut returned");
    return laid_out;
}

void* find_root(void* engine) {
    if (!engine)
        return nullptr;

    const root_candidates roots = direct_roots();
    const auto usable = [&](void* panel) {
        return panel && valid_panel(engine, panel) && panel_laid_out(panel);
    };
    const bool main_menu_usable = usable(roots.main_menu);
    const bool hud_usable = usable(roots.hud);

    // A single direct root is unambiguous. When both windows exist, use the event target to
    // select the active one rather than adding another visibility ABI or guessing a window.
    if (main_menu_usable != hud_usable)
        return hud_usable ? roots.hud : roots.main_menu;

    using get_last_target_t = void* (__thiscall*)(void*);
    using get_panel_id_t = const char* (__thiscall*)(void*);
    using get_parent_t = void* (__thiscall*)(void*);
    const auto get_last_target = virtual_function<get_last_target_t>(engine, 56);
    void* panel = nullptr;
    if (get_last_target) {
        AIRFLOW_DEBUG_STAGE("panorama.GetLastDispatchedEventTargetPanel");
        panel = get_last_target(engine);
        AIRFLOW_DEBUG_STAGE("panorama.GetLastDispatchedEventTargetPanel returned");
    }

    // GetID/GetParent are IUIPanel methods: both the function table and `this` must come from
    // the current panel, never from UIEngine. The depth cap bounds a malformed parent chain.
    for (int depth = 0; panel && depth < 32 && valid_panel(engine, panel); ++depth) {
        const auto get_panel_id = virtual_function<get_panel_id_t>(panel, 9);
        const auto get_parent = virtual_function<get_parent_t>(panel, 25);
        if (!get_panel_id || !get_parent)
            break;

        AIRFLOW_DEBUG_LOGF("panorama root walk: depth=%d panel=%p", depth, panel);
        AIRFLOW_DEBUG_STAGE("panorama.IUIPanel.GetID");
        const char* id = get_panel_id(panel);
        AIRFLOW_DEBUG_STAGE("panorama.IUIPanel.GetID returned");
        if (id && (!std::strcmp(id, "CSGOHud") || !std::strcmp(id, "CSGOMainMenu")))
            return panel;
        AIRFLOW_DEBUG_STAGE("panorama.IUIPanel.GetParent");
        panel = get_parent(panel);
        AIRFLOW_DEBUG_STAGE("panorama.IUIPanel.GetParent returned");
    }

    return nullptr;
}

bool run_script(void* root, const std::string& script) {
    void* engine = ui_engine();
    if (!engine || !valid_panel(engine, root) || !panel_laid_out(root))
        return false;

    // CS:GO x86 IUIPanel/UIEngine layout. These are not the Source 2 / CS2 vtable indices.
    using run_script_t = void(__thiscall*)(void*, void*, const char*, const char*, int, int, bool, bool);
    const auto run = virtual_function<run_script_t>(engine, 113);
    if (!run)
        return false;
    AIRFLOW_DEBUG_LOGF("panorama RunScript enter: engine=%p root=%p bytes=%llu", engine, root,
        static_cast<unsigned long long>(script.size()));
    AIRFLOW_DEBUG_STAGE("panorama.UIEngine.RunScript");
    run(engine, root, script.c_str(), "panorama/layout/base_mainmenu.xml", 8, 10, false, false);
    AIRFLOW_DEBUG_STAGE("panorama.UIEngine.RunScript returned");
    AIRFLOW_DEBUG_LOG("panorama RunScript leave");
    return true;
}

void forget_panel() {
    auto& s = state();
    s.panel_created = false;
    s.panel_visible = false;
    s.panel_hide_sent = false;
    s.panel_ready = false;
    s.next_hide_attempt = 0;
    s.panel_side = -1;
    s.panel_agent = -1;
    s.capture_valid = false;
}

void remove_panel(void* root) {
    if (!root)
        return;
    run_script(root,
        "(function(){var r=$.GetContextPanel();if(!r)return;"
        "var h=r.FindChildTraverse('" + std::string(k_host_id) + "');"
        "if(h)h.DeleteAsync(0);})();");
}

void refresh_root(ULONGLONG now) {
    auto& s = state();
    if (now < s.next_root_check)
        return;
    s.next_root_check = now + k_panel_check_ms;

    void* engine = ui_engine();
    if (!engine)
        return;
    void* current = find_root(engine);
    AIRFLOW_DEBUG_LOGF("panorama roots: cached=%p current=%p", s.root, current);
    const bool old_valid = s.root && valid_panel(engine, s.root);

    if (s.root && !old_valid) {
        s.root = nullptr;
        forget_panel();
    }
    if (current && current != s.root) {
        if (old_valid && s.panel_created)
            remove_panel(s.root);
        s.root = current;
        forget_panel();
        s.next_panel_check = 0;
    } else if (!s.root) {
        s.root = current;
    }
}

std::string panel_script(const RECT& rect, int side, int agent, unsigned background, bool reload) {
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    const char* team = side == 0 ? "t" : "ct";
    const char* sequence = side == 0 ? "t_loadout_pistol_idle" : "ct_loadout_pistol_idle_alt_loop01";
    const int camera = side == 0 ? 2 : 0;

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

    std::string script =
        "(function(){var r=$.GetContextPanel();if(!r)return;var d=r.Data();"
        "var h=d.DarksideEspAgentPreview;var p=d.DarksideEspAgentPreviewModel;var made=false;"
        "if(h&&!h.IsValid()){h=null;p=null;}if(p&&!p.IsValid())p=null;"
        "if(!h)h=r.FindChildTraverse('" + std::string(k_host_id) + "');"
        "if(!h){h=$.CreatePanel('Panel',r,'" + k_host_id + "',{hittest:false,style:'" + host_style + "'});made=true;}"
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
    script += "){p.ResetAnimation(false);p.SetScene('";
    script += k_manifest;
    script += "','";
    script += agent_model(side, agent);
    script += "',false);p.EquipPlayerFromLoadout('";
    script += team;
    script += "','secondary0');p.EquipPlayerFromLoadout('";
    script += team;
    script += "','clothing_hands');p.LayerSequence('";
    script += sequence;
    script += "',true,false);p.SetCameraPreset(";
    script += std::to_string(camera);
    script += ",false);}";
    script += "p.EnableRendering(true);p.Pause(false);})();";
    return script;
}

void synchronize_panel(const RECT& rect, int side, ULONGLONG now) {
    auto& s = state();
    refresh_root(now);
    if (!s.root || (!s.panel_created && now < s.next_panel_check))
        return;

    const int agent = selected_agent(side);
    const unsigned background = menu_background();
    const bool reload = s.panel_side != side || s.panel_agent != agent;
    const bool changed = reload || !same_rect(s.panel_rect, rect) ||
                         s.panel_background != background;
    if (s.panel_created && s.panel_visible && s.panel_ready && !changed &&
        now < s.next_panel_check)
        return;

    AIRFLOW_DEBUG_LOGF("agent preview sync: side=%d agent=%d rect=%ld,%ld,%ld,%ld reload=%d",
        side, agent, rect.left, rect.top, rect.right, rect.bottom, reload ? 1 : 0);
    AIRFLOW_DEBUG_STAGE("agent_preview.panel_script");
    const std::string script = panel_script(rect, side, agent, background, reload);
    if (!run_script(s.root, script)) {
        s.next_panel_check = now + k_panel_check_ms;
        return;
    }

    s.panel_created = true;
    s.panel_visible = true;
    s.panel_hide_sent = false;
    s.panel_ready = true;
    s.next_hide_attempt = 0;
    s.panel_side = side;
    s.panel_agent = agent;
    s.panel_background = background;
    s.panel_rect = rect;
    s.next_panel_check = now + k_panel_check_ms;
}

void hide_panel() {
    auto& s = state();
    if (!s.panel_created || !s.panel_visible || s.panel_hide_sent)
        return;
    const ULONGLONG now = GetTickCount64();
    if (now < s.next_hide_attempt)
        return;
    s.next_hide_attempt = now + k_panel_check_ms;
    refresh_root(now);
    if (!s.panel_created || !s.panel_visible)
        return;

    // A hidden panel is allowed to stop rendering here: it is not needed while the menu is closed.
    // While visible in Players it is never hidden with opacity zero or moved off-screen.
    const bool sent = run_script(s.root,
        "(function(){var r=$.GetContextPanel();if(!r)return;var d=r.Data();"
        "var h=d.DarksideEspAgentPreview;if(h&&!h.IsValid())h=null;"
        "if(!h)h=r.FindChildTraverse('" + std::string(k_host_id) + "');if(!h)return;h.visible=false;"
        "var p=d.DarksideEspAgentPreviewModel;if(p&&!p.IsValid())p=null;"
        "if(!p)p=h.FindChildTraverse('" + std::string(k_model_id) + "');"
        "if(p){p.EnableRendering(false);p.Pause(true);}})();");
    s.panel_hide_sent = sent;
    if (sent)
        s.panel_visible = false;
    else
        s.next_root_check = 0; // retry against a refreshed root instead of assuming it was hidden
    s.panel_ready = false;
}

void release_capture() {
    auto& s = state();
    if (s.capture_texture) {
        AIRFLOW_DEBUG_STAGE("d3d9.Texture.Release(agent preview)");
        s.capture_texture->Release();
        AIRFLOW_DEBUG_STAGE("d3d9.Texture.Release(agent preview) returned");
    }
    s.capture_texture = nullptr;
    s.capture_device = nullptr;
    s.capture_format = D3DFMT_UNKNOWN;
    s.capture_width = 0;
    s.capture_height = 0;
    s.capture_valid = false;
}

bool ensure_capture_texture(IDirect3DDevice9* device, int width, int height, D3DFORMAT format) {
    auto& s = state();
    if (width <= 0 || height <= 0)
        return false;
    if (s.capture_texture && s.capture_device == device && s.capture_width == width &&
        s.capture_height == height && s.capture_format == format)
        return true;

    release_capture();
    IDirect3DTexture9* texture = nullptr;
    AIRFLOW_DEBUG_STAGE("d3d9.CreateTexture(agent preview)");
    const HRESULT result = device->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, format,
        D3DPOOL_DEFAULT, &texture, nullptr);
    AIRFLOW_DEBUG_STAGE("d3d9.CreateTexture(agent preview) returned");
    if (FAILED(result) || !texture) {
        AIRFLOW_DEBUG_LOGF("agent capture CreateTexture failed: hr=0x%08lX size=%dx%d format=%d",
            static_cast<unsigned long>(result), width, height, format);
        return false;
    }

    s.capture_device = device;
    s.capture_texture = texture;
    s.capture_format = format;
    s.capture_width = width;
    s.capture_height = height;
    return true;
}

bool capture_backbuffer(IDirect3DDevice9* device) {
    auto& s = state();
    if (!device || !valid_rect(s.requested_rect))
        return false;

    IDirect3DSurface9* backbuffer = nullptr;
    AIRFLOW_DEBUG_STAGE("d3d9.GetBackBuffer(agent preview)");
    const HRESULT backbuffer_result = device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO,
        &backbuffer);
    AIRFLOW_DEBUG_STAGE("d3d9.GetBackBuffer(agent preview) returned");
    if (FAILED(backbuffer_result) || !backbuffer) {
        AIRFLOW_DEBUG_LOGF("agent capture GetBackBuffer failed: hr=0x%08lX",
            static_cast<unsigned long>(backbuffer_result));
        return false;
    }

    D3DSURFACE_DESC description{};
    AIRFLOW_DEBUG_STAGE("d3d9.BackBuffer.GetDesc(agent preview)");
    HRESULT result = backbuffer->GetDesc(&description);
    AIRFLOW_DEBUG_STAGE("d3d9.BackBuffer.GetDesc(agent preview) returned");
    RECT source_rect = s.requested_rect;
    source_rect.left = std::clamp(source_rect.left, 0L, static_cast<LONG>(description.Width));
    source_rect.top = std::clamp(source_rect.top, 0L, static_cast<LONG>(description.Height));
    source_rect.right = std::clamp(source_rect.right, 0L, static_cast<LONG>(description.Width));
    source_rect.bottom = std::clamp(source_rect.bottom, 0L, static_cast<LONG>(description.Height));
    if (FAILED(result) || !valid_rect(source_rect) || !same_rect(source_rect, s.requested_rect)) {
        backbuffer->Release();
        return false;
    }

    bool full_frame = s.prefer_full_frame;
    const int copy_width = full_frame ? static_cast<int>(description.Width)
                                      : static_cast<int>(source_rect.right - source_rect.left);
    const int copy_height = full_frame ? static_cast<int>(description.Height)
                                       : static_cast<int>(source_rect.bottom - source_rect.top);
    if (!ensure_capture_texture(device, copy_width, copy_height, description.Format)) {
        backbuffer->Release();
        return false;
    }

    IDirect3DSurface9* target = nullptr;
    AIRFLOW_DEBUG_STAGE("d3d9.Texture.GetSurfaceLevel(agent preview region)");
    result = s.capture_texture->GetSurfaceLevel(0, &target);
    AIRFLOW_DEBUG_STAGE("d3d9.Texture.GetSurfaceLevel(agent preview region) returned");
    if (SUCCEEDED(result) && target) {
        AIRFLOW_DEBUG_STAGE("d3d9.StretchRect(agent preview region)");
        result = device->StretchRect(backbuffer, full_frame ? nullptr : &source_rect,
            target, nullptr, D3DTEXF_NONE);
        AIRFLOW_DEBUG_STAGE("d3d9.StretchRect(agent preview region) returned");
        target->Release();
        if (FAILED(result))
            AIRFLOW_DEBUG_LOGF("agent capture region StretchRect failed: hr=0x%08lX",
                static_cast<unsigned long>(result));
    }

    // StretchRect cannot sub-rect a multisampled backbuffer on every D3D9 driver. Resolve the
    // complete frame once in that case and sample only the preview UVs; no CPU readback is used.
    if (FAILED(result) && !full_frame &&
        ensure_capture_texture(device, static_cast<int>(description.Width),
                               static_cast<int>(description.Height), description.Format)) {
        target = nullptr;
        AIRFLOW_DEBUG_STAGE("d3d9.Texture.GetSurfaceLevel(agent preview full frame)");
        result = s.capture_texture->GetSurfaceLevel(0, &target);
        AIRFLOW_DEBUG_STAGE("d3d9.Texture.GetSurfaceLevel(agent preview full frame) returned");
        if (SUCCEEDED(result) && target) {
            AIRFLOW_DEBUG_STAGE("d3d9.StretchRect(agent preview full frame)");
            result = device->StretchRect(backbuffer, nullptr, target, nullptr, D3DTEXF_NONE);
            AIRFLOW_DEBUG_STAGE("d3d9.StretchRect(agent preview full frame) returned");
            target->Release();
        }
        full_frame = SUCCEEDED(result);
        if (FAILED(result))
            AIRFLOW_DEBUG_LOGF("agent capture full-frame StretchRect failed: hr=0x%08lX",
                static_cast<unsigned long>(result));
    }
    backbuffer->Release();

    if (FAILED(result))
        return false;

    s.prefer_full_frame = full_frame;
    s.capture_full_frame = full_frame;
    if (full_frame && description.Width && description.Height) {
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
    s.drawn = false;
    s.capture_valid = false;
    if (!s.requested_active || !s.panel_ready || !s.panel_visible ||
        s.panel_side != s.requested_side || !same_rect(s.panel_rect, s.requested_rect))
        return;

    const ULONGLONG now = GetTickCount64();
    if (now < s.capture_retry_after)
        return;
    if (capture_backbuffer(device)) {
        s.capture_valid = true;
        s.capture_retry_after = 0;
    } else {
        s.capture_retry_after = now + k_capture_retry_ms;
    }
}

void draw(const ImVec2& min, const ImVec2& max, int side) {
    auto& s = state();
    const RECT rect = make_rect(min, max);
    if (!valid_rect(rect) || (side != 0 && side != 1)) {
        s.drawn = false;
        s.requested_active = false;
        return;
    }

    s.drawn = true;
    s.requested_active = true;
    s.requested_side = side;
    s.requested_rect = rect;
    AIRFLOW_DEBUG_STAGE("agent_preview.synchronize_panel");
    synchronize_panel(rect, side, GetTickCount64());
    AIRFLOW_DEBUG_STAGE("agent_preview.synchronize_panel returned");

    if (!s.capture_valid || !s.capture_texture || s.captured_side != side ||
        !same_size(s.captured_rect, rect))
        return;

    const float alpha = ImGui::GetStyle().Alpha;
    AIRFLOW_DEBUG_STAGE("ImGui.AddImage(agent preview)");
    ImGui::GetWindowDrawList()->AddImage(
        reinterpret_cast<ImTextureID>(s.capture_texture), min, max,
        s.captured_uv0, s.captured_uv1, ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, alpha)));
    AIRFLOW_DEBUG_STAGE("ImGui.AddImage(agent preview) returned");
}

void end_frame() {
    auto& s = state();
    if (s.drawn)
        return;
    s.requested_active = false;
    s.capture_valid = false;
    hide_panel();
    release_capture();
}

void on_device_reset() {
    auto& s = state();
    release_capture();
    s.prefer_full_frame = false;
    s.capture_retry_after = 0;
    s.panel_ready = false;
    s.next_panel_check = 0;
}
} // namespace agent_preview
