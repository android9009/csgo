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
#include <vector>

namespace {
enum class panorama_profile {
    none,
    legacy_reference,
    csgo_2023,
};

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
    bool abi_valid = false;
    bool abi_failure_logged = false;
    panorama_profile profile = panorama_profile::none;
    std::string abi_failure_reason;
    ULONGLONG next_engine_retry = 0;
    ULONGLONG next_engine_check = 0;
    ULONGLONG next_context_check = 0;
    bool context_failure_logged = false;

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
    ULONGLONG last_capture = 0;
    bool capture_success_logged = false;
    bool capture_failure_logged = false;

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

    // VirtualQuery describes one protection region, not necessarily a whole PE
    // section. Walk all regions covered by the range instead of rejecting a
    // valid vtable/function merely because it crosses a region boundary.
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
        const auto region_begin = reinterpret_cast<std::uintptr_t>(information.BaseAddress);
        const auto region_end = region_begin + information.RegionSize;
        if (region_end <= region_begin || current < region_begin || current >= region_end)
            return false;
        current = std::min<std::uintptr_t>(end, static_cast<std::uintptr_t>(region_end));
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
        const std::size_t virtual_size = section->Misc.VirtualSize;
        const std::size_t raw_size = section->SizeOfRawData;
        size = virtual_size && raw_size ? std::min(virtual_size, raw_size)
                                        : std::max(virtual_size, raw_size);
        text = base + section->VirtualAddress;
        // The section can contain several VirtualQuery protection regions.
        // Validate the particular function bytes in function_matches instead of
        // requiring the entire .text section to be one readable region.
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
    if (!readable_range(bytes, N))
        return false;
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
    if (s.abi_failure_logged && s.abi_failure_reason == reason)
        return;
    s.abi_failure_logged = true;
    s.abi_failure_reason = reason;
    AIRFLOW_DEBUG_LOGF("agent_preview Panorama unavailable: %s", reason);
}

bool validate_engine_locked(preview_state& s, void* engine) {
    if (!engine || !s.core_module)
        return false;
    const auto run = virtual_function<run_script_t>(engine, 113);
    const auto last = virtual_function<get_last_target_t>(engine, 56);
    const auto valid = virtual_function<is_valid_panel_t>(engine, 36);
    if (!run || !valid)
        return false;

    // The final 2023 CS:GO build exposes the factory from panorama.dll and
    // places RunScript at this RVA. The older reversed reference uses the same
    // public slots but registers its factory in panoramauiclient.dll. Keep both
    // profiles explicit; never accept an arbitrary vtable as ABI validation
    // merely because a pointer is non-null.
    const auto run_address = reinterpret_cast<std::uintptr_t>(run);
    const auto core_address = reinterpret_cast<std::uintptr_t>(s.core_module);
    if (s.client_module == s.core_module && run_address == core_address + 0x4BC20) {
        if (!function_in_module(s.core_module, valid) &&
            !function_in_module(s.client_module, valid)) {
            return false;
        }
        s.profile = panorama_profile::csgo_2023;
        return true;
    }

    if (last && function_matches(s.core_module, run, k_run_script_pattern) &&
        function_matches(s.core_module, last, k_last_target_pattern) &&
        function_matches(s.core_module, valid, k_valid_panel_pattern)) {
        s.profile = panorama_profile::legacy_reference;
        return true;
    }
    return false;
}

void* acquire_engine_locked(preview_state& s, ULONGLONG now) {
    if (s.abi_valid) {
        // Fingerprinting PE sections and querying vtable pages is validation,
        // not per-frame work. Still fetch the current engine pointer so a
        // Panorama reload cannot leave us calling a stale cached object.
        const bool cached_validation = s.engine && now < s.next_engine_check;
        const auto access = virtual_function<access_ui_engine_t>(s.panorama_wrapper, 11);
        if (!access)
            return nullptr;
        void* current = access(s.panorama_wrapper);
        if (cached_validation && current == s.engine)
            return s.engine;
        s.next_engine_check = now + 1000;
        if (!current || !validate_engine_locked(s, current)) {
            note_abi_failure_locked(s, "engine ABI changed");
            s.abi_valid = false;
            s.profile = panorama_profile::none;
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

    // Do not latch an early startup failure: Panorama can finish registering
    // its interfaces after the first ImGui frame.
    if (now < s.next_engine_retry)
        return nullptr;
    s.next_engine_retry = now + 1000;

    const HMODULE core = GetModuleHandleW(L"panorama.dll");
    const HMODULE client = GetModuleHandleW(L"panoramauiclient.dll");
    if (!core && !client) {
        note_abi_failure_locked(s, "Panorama modules are not loaded");
        return nullptr;
    }

    using create_interface_t = void* (__cdecl*)(const char*, int*);
    // 2023 CS:GO registers PanoramaUIEngine001 in panorama.dll. Older CS:GO
    // builds registered it in panoramauiclient.dll, so retain that as a
    // compatibility candidate rather than assuming one module layout.
    const HMODULE candidates[] = { core, client };
    bool saw_factory = false;
    bool saw_wrapper = false;
    for (const HMODULE factory : candidates) {
        if (!factory)
            continue;
        const auto create_interface = reinterpret_cast<create_interface_t>(
            GetProcAddress(factory, "CreateInterface"));
        if (!create_interface)
            continue;
        saw_factory = true;
        void* wrapper = create_interface("PanoramaUIEngine001", nullptr);
        if (!wrapper)
            continue;
        saw_wrapper = true;

        const auto access = virtual_function<access_ui_engine_t>(wrapper, 11);
        const bool access_matches = factory == core
            ? (function_in_module(core, access) || function_in_module(client, access))
            : function_matches(factory, access, k_access_engine_pattern);
        if (!access_matches)
            continue;

        s.client_module = factory;
        s.core_module = core;
        s.profile = panorama_profile::none;
        void* engine = access(wrapper);
        if (!engine || !validate_engine_locked(s, engine))
            continue;

        s.panorama_wrapper = wrapper;
        s.engine = engine;
        s.abi_valid = true;
        s.next_engine_check = now + 1000;
        s.abi_failure_logged = false;
        s.abi_failure_reason.clear();
        AIRFLOW_DEBUG_LOGF("agent_preview Panorama ABI validated (%s)",
            s.profile == panorama_profile::csgo_2023 ? "CS:GO 2023 panorama.dll" :
            "legacy Panorama reference");
        return engine;
    }

    if (!saw_factory)
        note_abi_failure_locked(s, "CreateInterface export is missing");
    else if (!saw_wrapper)
        note_abi_failure_locked(s, "PanoramaUIEngine001 is unavailable in panorama.dll/panoramauiclient.dll");
    else
        note_abi_failure_locked(s, "IUIEngine ABI validation failed for all factory candidates");
    return nullptr;
}

bool valid_panel_locked(preview_state& s, void* panel) {
    if (!s.abi_valid || !s.engine || !panel)
        return false;
    const auto is_valid = virtual_function<is_valid_panel_t>(s.engine, 36);
    return is_valid && is_valid(s.engine, panel);
}

template <typename T>
bool read_process_value(std::uintptr_t address, T& value) {
    if (address < 0x10000)
        return false;
    SIZE_T bytes_read = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
                             &value, sizeof(value), &bytes_read) &&
           bytes_read == sizeof(value);
}

std::string read_process_string(std::uintptr_t address, std::size_t maximum = 32) {
    if (address < 0x10000 || !maximum)
        return {};
    maximum = std::min<std::size_t>(maximum, 128);
    std::string result(maximum, '\0');
    SIZE_T bytes_read = 0;
    if (ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
                          result.data(), maximum, &bytes_read) && bytes_read == maximum) {
        const auto terminator = result.find('\0');
        result.resize(terminator == std::string::npos ? maximum : terminator);
        return result;
    }

    // A short string can straddle an unreadable page. Read it bytewise in that
    // case instead of treating the failed bulk read as an empty panel ID.
    result.clear();
    for (std::size_t i = 0; i < maximum; ++i) {
        char character = '\0';
        if (!read_process_value(address + i, character) || !character)
            break;
        result.push_back(character);
    }
    return result;
}

bool panel_has_root_id(void* panel) {
    if (!panel)
        return false;
    const auto base = reinterpret_cast<std::uintptr_t>(panel);
    // The 2023 panel map does not expose the ID through a stable public
    // structure. This bounded scan is the same layout used by the CS:GO 2023
    // Panorama bridge; every pointer and string read is guarded.
    for (std::uintptr_t offset = 0x10; offset < 0x400; offset += sizeof(void*)) {
        std::uintptr_t string_address = 0;
        if (!read_process_value(base + offset, string_address) || !string_address)
            continue;
        const std::string id = read_process_string(string_address);
        if (id == "CSGOMainMenu" || id == "MainMenu" || id == "CSGOHud" || id == "Hud")
            return true;
    }
    return false;
}

void* find_2023_context_locked(preview_state& s) {
    // CUIEngine's 2023 panel map: CUtlRBTree nodes are 24 bytes, with left and
    // right child indices at 0/4 and the IUIPanel key at 0x10. The map itself
    // is at engine+0xF0 and its root index at engine+0xFC.
    const auto engine = reinterpret_cast<std::uintptr_t>(s.engine);
    std::uintptr_t nodes = 0;
    int root = -1;
    if (!read_process_value(engine + 0xF0, nodes) ||
        !read_process_value(engine + 0xFC, root) || !nodes || root < 0) {
        return nullptr;
    }

    std::vector<int> pending;
    pending.push_back(root);
    for (std::size_t visited = 0; !pending.empty() && visited < 8192; ++visited) {
        const int index = pending.back();
        pending.pop_back();
        if (index < 0 || index > 0x100000)
            continue;
        const auto node = nodes + static_cast<std::uintptr_t>(index) * 24;
        int left = -1;
        int right = -1;
        void* panel = nullptr;
        if (!read_process_value(node, left) ||
            !read_process_value(node + 4, right) ||
            !read_process_value(node + 0x10, panel)) {
            continue;
        }
        if (panel && valid_panel_locked(s, panel) && panel_has_root_id(panel)) {
            return panel;
        }
        if (left >= 0)
            pending.push_back(left);
        if (right >= 0)
            pending.push_back(right);
    }
    return nullptr;
}

void* context_for_script_locked(preview_state& s, ULONGLONG now) {
    void* engine = acquire_engine_locked(s, now);
    if (!engine)
        return nullptr;

    if (s.context && now < s.next_context_check)
        return s.context;
    // A healthy context is cheap to revalidate and must not trigger another
    // full panel-tree/string scan. Scan only after it actually disappears.
    if (s.context && valid_panel_locked(s, s.context)) {
        s.next_context_check = now + 1000;
        s.context_failure_logged = false;
        return s.context;
    }
    s.context = nullptr;
    s.next_context_check = now + 250;

    if (s.profile == panorama_profile::csgo_2023) {
        // The 2023 map walk is preferable to the last-event target: opening an
        // ImGui menu does not dispatch a Panorama event, so that target can be
        // null or belong to an unrelated popup.
        if (void* panel = find_2023_context_locked(s)) {
            if (s.context != panel)
                AIRFLOW_DEBUG_LOG("agent_preview Panorama context acquired from panel map");
            s.context = panel;
            s.next_context_check = now + 1000;
            s.context_failure_logged = false;
            return panel;
        }
    } else {
        const auto get_last = virtual_function<get_last_target_t>(engine, 56);
        void* last = get_last ? get_last(engine) : nullptr;
        if (last && valid_panel_locked(s, last)) {
            s.context = last;
            s.next_context_check = now + 1000;
            s.context_failure_logged = false;
            return last;
        }
    }
    s.context = nullptr;
    if (!s.context_failure_logged) {
        s.context_failure_logged = true;
        AIRFLOW_DEBUG_LOG("agent_preview Panorama has no valid MainMenu/Hud context yet");
    }
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
constexpr ULONGLONG k_panel_heartbeat_ms = 5000;
constexpr ULONGLONG k_capture_retry_ms = 1000;
constexpr ULONGLONG k_capture_interval_ms = 33;

std::string root_script_prefix() {
    // RunScript needs a valid panel context, but the parent walk belongs in JS:
    // it avoids depending on another private IUIPanel vtable layout in C++.
    return "(function(){var c=$.GetContextPanel();if(!c)return;var r=c;var root=null;"
           "for(var i=0;i<32&&r;i++){if(r.id==='Hud'||r.id==='MainMenu'||r.id==='CSGOHud'||r.id==='CSGOMainMenu')root=r;"
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
        "flow-children:none;overflow:clip;horizontal-align:left;vertical-align:top;";

    std::string script = root_script_prefix();
    script +=
        "var h=d.DarksideEspAgentPreview;var p=d.DarksideEspAgentPreviewModel;var made=false;"
        "if(h&&!h.IsValid()){h=null;p=null;}if(p&&!p.IsValid())p=null;"
        "if(!h)h=root.FindChildTraverse('" + std::string(k_host_id) + "');"
        "if(!h){h=$.CreatePanel('Panel',root,'" + k_host_id + "',{hittest:false,style:'" + host_style + "'});made=true;}"
        "if(!h)return;"
        "if(!p)p=h.FindChildTraverse('" + k_model_id + "');"
        "if(!p){p=$.CreatePanel('ItemPreviewPanel',h,'" + k_model_id + "',{hittest:false,mouse_rotate:false,"
        "'require-composition-layer':true,style:'width:100%;height:100%;horizontal-align:left;vertical-align:top;'});made=true;}"
        "if(!p)return;d.DarksideEspAgentPreview=h;d.DarksideEspAgentPreviewModel=p;"
        // ImGui rects are in backbuffer pixels; Panorama style lengths are in
        // the root's scaled UI coordinates. Convert through the actual root
        // transform instead of assuming a 1:1 CSS/screen mapping.
        "var wp=root.GetPositionWithinWindow();var sx=root.actualuiscale_x;var sy=root.actualuiscale_y;"
        "if(!(sx>0))sx=1;if(!(sy>0))sy=1;"
        "h.style.x=((" + x + "-wp.x)/sx)+'px';"
        "h.style.y=((" + y + "-wp.y)/sy)+'px';"
        "h.style.width=((" + w + ")/sx)+'px';"
        "h.style.height=((" + h + ")/sy)+'px';"
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
    script += "');p.PlayActivity('ACT_CSGO_UIPLAYER_IDLE',true);";
    // Preset 18 is the original full-body buymenu framing. Use a slightly
    // closer custom preset 0 and a little more downward pitch so the head sits
    // nearer the header while the feet remain inside the preview.
    script += "p.SetCameraPreset(18,false);";
    script += "p.SetCameraPosition(198.81,65.08,63.44);";
    script += "p.SetCameraAngles(5.25,-162.13,0.00);";
    script += "p.SetCameraPreset(0,false);";
    script += "p.SetSceneIntroRotation(-2.0,10.0,true);}";
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

    if (!s.panel_created)
        AIRFLOW_DEBUG_LOG("agent_preview Panorama ItemPreviewPanel script submitted");
    s.panel_created = true;
    s.panel_visible = true;
    s.panel_ready = true;
    s.panel_hide_sent = false;
    // A new visible cycle must always be allowed to hide immediately. Do not
    // carry a previous close attempt's one-second retry budget into it.
    s.next_hide_attempt = 0;
    s.panel_side = side;
    s.panel_agent = agent;
    s.panel_background = background;
    s.panel_rect = rect;
    // Keep a slow heartbeat for panels removed by game UI transitions, but do
    // not rebuild/render-script an unchanged preview every frame or second.
    s.next_panel_check = now + k_panel_heartbeat_ms;
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
    s.last_capture = 0;
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
    if (!s.requested_active || !s.panel_ready || !s.panel_visible ||
        s.panel_side != s.requested_side || !same_rect(s.panel_rect, s.requested_rect)) {
        s.capture_valid = false;
        return;
    }

    const ULONGLONG now = GetTickCount64();
    const bool reusable = s.capture_valid && s.capture_texture &&
                          s.captured_side == s.requested_side &&
                          same_rect(s.captured_rect, s.requested_rect);
    // A live preview does not need a second full GPU copy at the game's full
    // frame rate. Reuse the last texture for ~30 Hz capture and keep it valid
    // on skipped frames instead of blanking the image.
    if (reusable && now - s.last_capture < k_capture_interval_ms)
        return;
    s.capture_valid = false;
    if (now < s.capture_retry_after)
        return;
    if (capture_backbuffer_locked(s, device)) {
        s.capture_valid = true;
        s.last_capture = now;
        s.capture_retry_after = 0;
        if (!s.capture_success_logged) {
            s.capture_success_logged = true;
            AIRFLOW_DEBUG_LOGF("agent_preview D3D9 capture succeeded (%dx%d%s)",
                s.capture_width, s.capture_height, s.capture_full_frame ? ", full frame" : "");
        }
        s.capture_failure_logged = false;
    } else {
        s.capture_retry_after = now + k_capture_retry_ms;
        if (!s.capture_failure_logged) {
            s.capture_failure_logged = true;
            AIRFLOW_DEBUG_LOG("agent_preview D3D9 backbuffer capture failed");
        }
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

void on_menu_closed() {
    auto& s = state();
    std::scoped_lock lock(s.mutex);
    // Closing the menu is a lifecycle edge, not a capture retry. Submit the
    // hide before the next Panorama composition so the independent panel does
    // not linger while ImGui is already gone. Keep the current draw/texture
    // lifetime for end_frame(): an input callback can race a queued ImGui image.
    s.requested_active = false;
    s.capture_valid = false;
    s.next_hide_attempt = 0;
    hide_panel_locked(s);
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
