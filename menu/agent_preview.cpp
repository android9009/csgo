#include "agent_preview.hpp"

// Keep Win32/D3D declarations ahead of menu.hpp's standard-library namespace imports.
#include <Windows.h>
#include <d3d9.h>

#include "menu.hpp"
#include "../runtime/debug_console.hpp"
#include "../features/visuals/chams.hpp"
#include "../features/visuals/source.hpp"
#include "../sdk/config_system/config_system.hpp"

#include <algorithm>
#include <array>
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
    void* panel_context = nullptr;
    int context_game_state = -1;
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

    // Initialisation is independent of the Visuals page. The generation makes
    // deferred deletion of an older JS marker unable to acknowledge a new model.
    bool preload_ready = false;
    unsigned preload_generation = 0;
    ULONGLONG next_preload_check = 0;

    HMODULE material_module = nullptr;
    void* material_system = nullptr;
    bool material_abi_valid = false;
    ULONGLONG next_material_retry = 0;

    void* find_child_function = nullptr;
    void* composition_name_function = nullptr;
    bool panel_methods_valid = false;

    // This is Panorama's own composition RT, not a copy of the game backbuffer.
    // AddRef only maintains the D3D9 resource until ImGui has submitted its draw.
    IDirect3DDevice9* texture_device = nullptr;
    IDirect3DTexture9* panel_texture = nullptr;
    int panel_texture_width = 0;
    int panel_texture_height = 0;
    std::string panel_texture_name;
    ULONGLONG texture_retry_after = 0;
    bool texture_success_logged = false;
    bool texture_failure_logged = false;

    bool panel_texture_valid = false;
    int textured_side = -1;
    RECT textured_rect{};
    ImVec2 textured_uv1{ 1.f, 1.f };
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
    // Panorama layout and the D3D9 texture crop use the same integer rectangle.
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

static_assert(IM_ARRAYSIZE(k_terrorist_models) == 21);

int selected_agent(int) {
    // Agents T is the single canonical skin for Enemies, Teammates and Self.
    const int selected = g_cfg->skins.m_agents.m_selected;
    return std::clamp(selected, 0, static_cast<int>(IM_ARRAYSIZE(k_terrorist_models)) - 1);
}

const char* agent_model(int, int agent) {
    return k_terrorist_models[agent];
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
// Public IUIPanel methods in the same validated 2023 panorama.dll. Slot 39
// finds our model below the saved root; slot 323 returns the isolated layer's
// Source 1 texture name. The RVAs and prologues are both checked before use.
constexpr int k_find_child_traverse_pattern[] = {
    0x55, 0x8B, 0xEC, 0x53, 0x8B, 0x5D, 0x08, 0x57,
    0x8B, 0xF9, 0x53, 0x8B, 0x07, 0xFF, 0x90, 0x94,
    0x00, 0x00, 0x00, 0x85, 0xC0, 0x75, 0x25, 0x56,
    0x33, 0xF6, 0x39, 0x77, 0x2C
};
constexpr int k_composition_name_pattern[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x83, 0xEC,
    0x0C, 0x53, 0x56, 0x8B, 0xF1, 0x57, 0x85, 0xF6,
    0x74, 0x1B, 0x8B, 0x0D, -1, -1, -1, -1,
    0x8D, 0x54, 0x24, 0x10, 0x56, 0x52, 0x8B, 0x01,
    0xFF, 0x90, 0x94, 0x00, 0x00, 0x00
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
    s.panel_context = nullptr;
    s.preload_ready = false;
    s.next_preload_check = 0;
    s.panel_texture_valid = false;
    s.panel_texture_name.clear();
    s.find_child_function = nullptr;
    s.composition_name_function = nullptr;
    s.panel_methods_valid = false;
}

using access_ui_engine_t = void* (__thiscall*)(void*);
using is_valid_panel_t = bool(__thiscall*)(void*, void*);
using get_last_target_t = void* (__thiscall*)(void*);
using run_script_t = void(__thiscall*)(void*, void*, const char*, const char*,
    int, int, bool, bool);
using find_child_traverse_t = void* (__thiscall*)(void*, const char*);
using composition_layer_name_t = const char* (__thiscall*)(void*);
using find_texture_t = void* (__thiscall*)(void*, const char*, const char*, bool, int);
using panorama_texture_ptr_t = void* (__thiscall*)(void*, void*);
using texture_int_t = int(__thiscall*)(void*);
using texture_bool_t = bool(__thiscall*)(void*);

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

std::string preload_marker_id(const char* stage, unsigned generation) {
    return "DarksideEspAgentPreview" + std::string(stage) + std::to_string(generation);
}

struct panel_id_matches {
    bool root = false;
    bool main_menu = false;
    bool hud = false;
    bool ready = false;
};

panel_id_matches scan_panel_ids(void* panel, const std::string& ready_id) {
    panel_id_matches matches;
    if (!panel)
        return matches;
    const auto base = reinterpret_cast<std::uintptr_t>(panel);
    // The 2023 panel map does not expose the ID through a stable public
    // structure. This bounded scan is the same layout used by the CS:GO 2023
    // Panorama bridge; every pointer and string read is guarded. Snapshot the
    // panel in one read instead of issuing 252 tiny ReadProcessMemory calls.
    // If the object crosses an unreadable page, retain the field-wise fallback.
    std::array<std::uint8_t, 0x400> snapshot{};
    const bool has_snapshot = read_process_value(base, snapshot);
    for (std::uintptr_t offset = 0x10; offset < 0x400; offset += sizeof(void*)) {
        std::uintptr_t string_address = 0;
        if (has_snapshot) {
            std::memcpy(&string_address, snapshot.data() + offset, sizeof(string_address));
        } else if (!read_process_value(base + offset, string_address)) {
            continue;
        }
        if (!string_address)
            continue;
        const std::string id = read_process_string(string_address, 48);
        if (id == "CSGOMainMenu" || id == "MainMenu") {
            matches.root = true;
            matches.main_menu = true;
        }
        if (id == "CSGOHud" || id == "Hud") {
            matches.root = true;
            matches.hud = true;
        }
        if (!ready_id.empty() && id == ready_id)
            matches.ready = true;
        if (matches.ready || (matches.root && ready_id.empty()))
            break;
    }
    return matches;
}

void* find_2023_context_locked(preview_state& s, const std::string* ready_id = nullptr,
                               bool* preload_ready = nullptr) {
    if (preload_ready)
        *preload_ready = false;
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

    void* main_menu_context = nullptr;
    void* hud_context = nullptr;
    bool ready_found = false;
    const bool in_game = visuals_source::is_in_game();
    const std::string expected_ready_id = ready_id ? *ready_id : std::string();
    std::vector<int> pending;
    pending.push_back(root);
    for (std::size_t visited = 0; !pending.empty() && visited < 8192; ++visited) {
        const int index = pending.back();
        pending.pop_back();
        if (index < 0 || index > 0x100000)
            continue;
        const auto node = nodes + static_cast<std::uintptr_t>(index) * 24;
        std::array<std::uint8_t, 24> node_snapshot{};
        if (!read_process_value(node, node_snapshot))
            continue;
        int left = -1;
        int right = -1;
        void* panel = nullptr;
        std::memcpy(&left, node_snapshot.data(), sizeof(left));
        std::memcpy(&right, node_snapshot.data() + 4, sizeof(right));
        std::memcpy(&panel, node_snapshot.data() + 0x10, sizeof(panel));
        if (panel && valid_panel_locked(s, panel)) {
            const panel_id_matches ids = scan_panel_ids(panel, expected_ready_id);
            if (ids.main_menu)
                main_menu_context = panel;
            if (ids.hud)
                hud_context = panel;
            if (ids.ready)
                ready_found = true;
            void* preferred = in_game ? hud_context : main_menu_context;
            // A readiness query only needs the marker. Do not continue through
            // thousands of unrelated panels merely to rediscover the root that
            // synchronize_panel_locked() already used to create the scene.
            if (ready_id && ready_found) {
                if (preload_ready)
                    *preload_ready = true;
                return preferred ? preferred : s.context;
            }
            if (preferred && !ready_id) {
                if (preload_ready)
                    *preload_ready = ready_found;
                return preferred;
            }
        }
        if (left >= 0)
            pending.push_back(left);
        if (right >= 0)
            pending.push_back(right);
    }

    // A MainMenu panel can remain valid after the transition into a match. It
    // is not a valid texture/script target while playing; wait for the HUD root
    // instead of drawing a stale MainMenu scene over the game.
    void* context = in_game ? hud_context
        : (main_menu_context ? main_menu_context : hud_context);
    if (preload_ready)
        *preload_ready = ready_found && context != nullptr;
    return context;
}

bool hide_panel_in_context_locked(preview_state& s, void* context);

void* context_for_script_locked(preview_state& s, ULONGLONG now) {
    const bool in_game = visuals_source::is_in_game();
    // synchronize_panel_locked() is reached from the draw path every frame.
    // Do not even query the engine wrapper while both the context and game
    // state are inside their existing validity window.
    if (s.abi_valid && s.context && now < s.next_context_check &&
        s.context_game_state == static_cast<int>(in_game)) {
        return s.context;
    }

    void* engine = acquire_engine_locked(s, now);
    if (!engine)
        return nullptr;

    if (s.context_game_state != static_cast<int>(in_game)) {
        // The host belongs to the old MainMenu/Hud root. Hide it through that
        // exact context before selecting the new root; looking it up through
        // the new context leaves a stale panel over the game/team UI.
        if (s.panel_created && s.panel_visible && s.panel_context)
            hide_panel_in_context_locked(s, s.panel_context);
        s.panel_visible = false;
        s.panel_hide_sent = false;
        s.panel_context = nullptr;
        s.context_game_state = static_cast<int>(in_game);
        s.context = nullptr;
        s.next_context_check = 0;
        s.panel_texture_valid = false;
        s.panel_texture_name.clear();
        s.panel_methods_valid = false;
        s.panel_ready = false;
        s.preload_ready = false;
        s.next_preload_check = 0;
    }
    if (s.context && now < s.next_context_check)
        return s.context;
    // A healthy context must not trigger another full panel-tree/string scan.
    // MainMenu/Hud transitions are already handled by context_game_state above;
    // rescan only when the selected panel actually disappears.
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
constexpr ULONGLONG k_panel_preload_check_ms = 250;
constexpr ULONGLONG k_preload_marker_check_ms = 500;
constexpr ULONGLONG k_panel_heartbeat_ms = 5000;
constexpr ULONGLONG k_texture_retry_ms = 250;

std::string root_script_prefix() {
    // RunScript needs a valid panel context, but the parent walk belongs in JS:
    // it avoids depending on another private IUIPanel vtable layout in C++.
    return "(function(){var c=$.GetContextPanel();if(!c)return;var r=c;var root=null;"
           "for(var i=0;i<32&&r;i++){if(r.id==='Hud'||r.id==='MainMenu'||r.id==='CSGOHud'||r.id==='CSGOMainMenu')root=r;"
           "var parent=r.GetParent();if(!parent)break;r=parent;}if(!root)return;var d=root.Data();";
}

bool hide_panel_in_context_locked(preview_state& s, void* context) {
    if (!context)
        return false;
    std::string script = root_script_prefix();
    script +=
        "var h=d.DarksideEspAgentPreview;if(h&&!h.IsValid())h=null;"
        "if(!h)h=root.FindChildTraverse('" + std::string(k_host_id) + "');"
        "if(!h)return;h.visible=false;h.style.zIndex=0;h.style.backgroundColor='transparent';"
        "var p=d.DarksideEspAgentPreviewModel;if(p&&!p.IsValid())p=null;"
        "if(!p)p=h.FindChildTraverse('" + k_model_id + "');"
        "if(p){p.hittest=false;p.EnableRendering(false);p.Pause(true);}})();";
    return run_script_locked(s, context, script);
}

std::string panel_script(const RECT& rect, int side, int agent, unsigned background,
                         bool reload, unsigned generation) {
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    const char* team = side == 0 ? "t" : "ct";
    const std::string accepted_id = preload_marker_id("Scene", generation);
    const std::string ready_id = preload_marker_id("Ready", generation);
    const std::string previous_accepted_id = preload_marker_id("Scene", generation - 1);
    const std::string previous_ready_id = preload_marker_id("Ready", generation - 1);

    char color[16];
    std::snprintf(color, sizeof(color), "#%06X", background & 0xFFFFFFu);
    const std::string x = std::to_string(rect.left);
    const std::string y = std::to_string(rect.top);
    const std::string w = std::to_string(width);
    const std::string h = std::to_string(height);
    const std::string host_style =
        "flow-children:none;overflow:clip;horizontal-align:left;vertical-align:top;";
    const std::string marker_style = "width:0px;height:0px;horizontal-align:left;vertical-align:top;";

    std::string script = root_script_prefix();
    script +=
        "var h=d.DarksideEspAgentPreview;var p=d.DarksideEspAgentPreviewModel;var made=false;"
        "if(h&&!h.IsValid()){h=null;p=null;}if(p&&!p.IsValid())p=null;"
        "if(!h)h=root.FindChildTraverse('" + std::string(k_host_id) + "');"
        "if(!h){h=$.CreatePanel('Panel',root,'" + k_host_id + "',{hittest:false,style:'" + host_style + "'});made=true;}"
        "if(!h)return;"
        "if(!p)p=h.FindChildTraverse('" + k_model_id + "');"
        // The isolated panel is render-only. ImGui owns the modal menu mouse;
        // native hittest/rotation must not capture it behind the overlay.
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
        // The native host retains its own composition layer above native popups;
        // ImGui samples that isolated layer afterwards in the same foreground menu.
        "h.style.backgroundColor='" + color + "';h.style.zIndex=100000;h.visible=true;p.hittest=false;"
        "var acceptedMarker=h.FindChildTraverse('" + accepted_id + "');"
        "var readyMarker=h.FindChildTraverse('" + ready_id + "');";

    script += "var reloading=made||";
    script += reload ? "true" : "false";
    script += ";var needsScene=reloading||!acceptedMarker;if(needsScene){";
    script += "var oldMarker=h.FindChildTraverse('" + previous_accepted_id + "');if(oldMarker)oldMarker.DeleteAsync(0);";
    script += "oldMarker=h.FindChildTraverse('" + previous_ready_id + "');if(oldMarker)oldMarker.DeleteAsync(0);";
    script += "if(acceptedMarker){acceptedMarker.DeleteAsync(0);acceptedMarker=null;}";
    script += "if(readyMarker){readyMarker.DeleteAsync(0);readyMarker=null;}";
    script += "p.ResetAnimation(true);p.SetSceneAngles(0,0,0,false);var sceneAccepted=p.SetScene('";
    script += k_manifest;
    script += "','";
    script += agent_model(side, agent);
    script += "',false);p.SetPlayerModel('";
    script += agent_model(side, agent);
    script += "');p.ResetActivityModifiers();p.ApplyActivityModifier('";
    script += team;
    script += "');p.PlayActivity('ACT_CSGO_UIPLAYER_IDLE',true);";
    // Preset 18 is the original full-body buymenu framing. Use a slightly
    // closer custom preset 0 so the agent fills the larger ESP target while
    // the head and feet remain inside the preview.
    script += "p.SetCameraPreset(18,false);";
    script += "p.SetCameraPosition(178.93,65.08,63.44);";
    script += "p.SetCameraAngles(5.25,-162.13,0.00);";
    script += "p.SetCameraPreset(0,false);";
    script += "p.SetSceneIntroRotation(-2.0,10.0,true);";
    script += "if(sceneAccepted){acceptedMarker=$.CreatePanel('Panel',h,'" + accepted_id +
              "',{hittest:false,style:'" + marker_style + "'});acceptedMarker.visible=false;}}";
    script += "p.EnableRendering(true);p.Pause(false);";
    // SetScene initializes the scene/model synchronously. BReadyForDisplay is
    // checked on a later script pass, after Panorama has laid out the new panel.
    script += "if(!needsScene&&acceptedMarker&&p.BReadyForDisplay()&&!readyMarker){";
    script += "readyMarker=$.CreatePanel('Panel',h,'" + ready_id +
              "',{hittest:false,style:'" + marker_style + "'});readyMarker.visible=false;}";
    script += "})();";
    return script;
}

void synchronize_panel_locked(preview_state& s, const RECT& rect, int side, ULONGLONG now) {
    void* context = context_for_script_locked(s, now);
    if (!context)
        return;

    const int agent = selected_agent(side);
    const unsigned background = menu_background();
    const bool reload = s.panel_context != context || s.panel_side != side || s.panel_agent != agent;
    const bool changed = reload || !same_rect(s.panel_rect, rect) ||
                         s.panel_background != background;
    if (s.panel_created && s.panel_visible && s.panel_ready && !changed &&
        now < s.next_panel_check) {
        return;
    }

    if (reload) {
        if (s.panel_context != context && s.panel_created && s.panel_visible &&
            s.panel_context && hide_panel_in_context_locked(s, s.panel_context)) {
            s.panel_visible = false;
            s.panel_ready = false;
        }
        ++s.preload_generation;
        if (!s.preload_generation)
            ++s.preload_generation;
        s.preload_ready = false;
        s.next_preload_check = 0;
        s.panel_texture_valid = false;
        s.panel_texture_name.clear();
    }
    if (!run_script_locked(s, context,
                           panel_script(rect, side, agent, background, reload,
                                        s.preload_generation))) {
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
    s.panel_context = context;
    s.panel_side = side;
    s.panel_agent = agent;
    s.panel_background = background;
    s.panel_rect = rect;
    // While loading, poll the JS readiness marker without rerunning SetScene.
    // Once ready, keep only the slow heartbeat for game UI panel transitions.
    s.next_panel_check = now + (s.preload_ready ? k_panel_heartbeat_ms
                                                : k_panel_preload_check_ms);
}

void hide_panel_locked(preview_state& s) {
    if (!s.panel_created || !s.panel_visible || s.panel_hide_sent)
        return;
    const ULONGLONG now = GetTickCount64();
    if (now < s.next_hide_attempt)
        return;
    s.next_hide_attempt = now + k_panel_check_ms;

    // Hide in the root that actually owns the host. Only if that panel is
    // already gone may we fall back to the currently selected Panorama root.
    void* context = s.panel_context;
    if (!context || !valid_panel_locked(s, context))
        context = context_for_script_locked(s, now);
    if (!context || !s.panel_created || !s.panel_visible)
        return;

    if (hide_panel_in_context_locked(s, context)) {
        s.panel_hide_sent = true;
        s.panel_visible = false;
        s.panel_ready = false;
    }
}

RECT warmup_rect() {
    // Keep the real ItemPreviewPanel laid out and able to load its scene while
    // placing it outside the desktop/backbuffer. No replacement renderer is used.
    return RECT{ -4096, -4096, -3840, -3840 };
}

void refresh_preload_ready_locked(preview_state& s, ULONGLONG now) {
    if (s.preload_ready || !s.panel_created || !s.panel_ready ||
        now < s.next_preload_check) {
        return;
    }
    if (!acquire_engine_locked(s, now) || !s.panel_created || !s.panel_ready)
        return;

    // The legacy compatibility profile has no validated panel-map layout for
    // observing the JS marker. Its RunScript acknowledgement is retained only
    // as a fallback; the target 2023 profile below waits for the actual marker.
    if (s.profile == panorama_profile::legacy_reference) {
        s.preload_ready = true;
        s.next_panel_check = now + k_panel_heartbeat_ms;
        AIRFLOW_DEBUG_LOG("agent_preview legacy Panorama initialization script acknowledged");
        return;
    }
    if (s.profile != panorama_profile::csgo_2023 || !s.engine || !s.preload_generation)
        return;

    s.next_preload_check = now + k_preload_marker_check_ms;
    const std::string ready_id = preload_marker_id("Ready", s.preload_generation);
    bool ready = false;
    find_2023_context_locked(s, &ready_id, &ready);
    if (!ready)
        return;

    s.preload_ready = true;
    s.next_panel_check = now + k_panel_heartbeat_ms;
    AIRFLOW_DEBUG_LOG("agent_preview Panorama scene/model reported ready for display");
}

void prewarm_locked(preview_state& s, ULONGLONG now) {
    refresh_preload_ready_locked(s, now);
    if (s.preload_ready) {
        if (!s.requested_active && s.panel_visible && same_rect(s.panel_rect, warmup_rect()))
            hide_panel_locked(s);
        return;
    }
    // An on-screen draw owns the rectangle. Otherwise initialize at injection
    // time off-screen instead of waiting for the first Visuals page visit.
    if (s.requested_active)
        return;

    const int side = s.panel_side == 0 || s.panel_side == 1 ? s.panel_side : 0;
    synchronize_panel_locked(s, warmup_rect(), side, now);
    refresh_preload_ready_locked(s, now);
    if (s.preload_ready && s.panel_visible)
        hide_panel_locked(s);
}

void release_panel_texture_locked(preview_state& s) {
    if (s.panel_texture)
        s.panel_texture->Release();
    s.panel_texture = nullptr;
    s.texture_device = nullptr;
    s.panel_texture_width = 0;
    s.panel_texture_height = 0;
    s.panel_texture_name.clear();
    s.panel_texture_valid = false;
    s.textured_side = -1;
    s.textured_rect = RECT{};
    s.textured_uv1 = ImVec2(1.f, 1.f);
}

bool acquire_material_locked(preview_state& s, ULONGLONG now) {
    if (s.material_abi_valid)
        return s.material_system != nullptr;
    if (now < s.next_material_retry)
        return false;
    s.next_material_retry = now + 1000;

    const HMODULE module = GetModuleHandleW(L"materialsystem.dll");
    if (!module)
        return false;
    using create_interface_t = void* (__cdecl*)(const char*, int*);
    const auto create_interface = reinterpret_cast<create_interface_t>(
        GetProcAddress(module, "CreateInterface"));
    if (!create_interface)
        return false;
    void* material_system = create_interface("VMaterialSystem080", nullptr);
    if (!material_system)
        return false;

    // VMaterialSystem080's public Source 1 ABI: FindTexture is slot 91 and
    // GetPanormaTexturePtr is slot 156. The validated 2023 Panorama binary's
    // engine-RT wrapper calls through byte offsets 0x16c (91 * 4) and its D3D9
    // backend through 0x270 (156 * 4), respectively; these are not CS2 slots.
    const auto find = virtual_function<find_texture_t>(material_system, 91);
    const auto native = virtual_function<panorama_texture_ptr_t>(material_system, 156);
    if (!find || !native || !function_in_module(module, find) ||
        !function_in_module(module, native)) {
        return false;
    }

    s.material_module = module;
    s.material_system = material_system;
    s.material_abi_valid = true;
    return true;
}

bool validate_panel_methods_locked(preview_state& s, void* context) {
    if (s.panel_methods_valid)
        return true;
    if (s.profile != panorama_profile::csgo_2023 || !s.core_module || !context)
        return false;

    const auto find = virtual_function<find_child_traverse_t>(context, 39);
    const auto composition = virtual_function<composition_layer_name_t>(context, 323);
    const auto base = reinterpret_cast<std::uintptr_t>(s.core_module);
    if (!find || !composition ||
        reinterpret_cast<std::uintptr_t>(find) != base + 0x87070 ||
        reinterpret_cast<std::uintptr_t>(composition) != base + 0x90FE0 ||
        !function_matches(s.core_module, find, k_find_child_traverse_pattern) ||
        !function_matches(s.core_module, composition, k_composition_name_pattern)) {
        return false;
    }

    s.find_child_function = reinterpret_cast<void*>(find);
    s.composition_name_function = reinterpret_cast<void*>(composition);
    s.panel_methods_valid = true;
    return true;
}

bool acquire_panel_texture_locked(preview_state& s, IDirect3DDevice9* device,
                                  ULONGLONG now) {
    if (!device || !valid_rect(s.requested_rect) || !s.preload_ready ||
        !validate_panel_methods_locked(s, s.panel_context) ||
        !acquire_material_locked(s, now)) {
        return false;
    }

    const auto find_child = reinterpret_cast<find_child_traverse_t>(s.find_child_function);
    const auto composition_name = reinterpret_cast<composition_layer_name_t>(
        s.composition_name_function);
    void* model = find_child(s.panel_context, k_model_id);
    if (!model || !valid_panel_locked(s, model))
        return false;

    // Unlike the backbuffer, this layer is allocated by Panorama specifically
    // for the ItemPreviewPanel. Console/VGUI pixels are composed elsewhere and
    // can never become part of this texture.
    const char* raw_name = composition_name(model);
    const std::string name = read_process_string(
        reinterpret_cast<std::uintptr_t>(raw_name), 128);
    if (name.empty() || name.size() >= 128)
        return false;

    const auto find_texture = virtual_function<find_texture_t>(s.material_system, 91);
    const auto panorama_texture = virtual_function<panorama_texture_ptr_t>(
        s.material_system, 156);
    if (!find_texture || !panorama_texture)
        return false;
    void* source_texture = find_texture(s.material_system, name.c_str(),
        "RenderTargets", false, 0);
    if (!source_texture)
        return false;

    const auto is_error = virtual_function<texture_bool_t>(source_texture, 15);
    const auto is_render_target = virtual_function<texture_bool_t>(source_texture, 20);
    const auto actual_width = virtual_function<texture_int_t>(source_texture, 3);
    const auto actual_height = virtual_function<texture_int_t>(source_texture, 4);
    if (!is_error || !is_render_target || !actual_width || !actual_height ||
        is_error(source_texture) || !is_render_target(source_texture) ||
        actual_width(source_texture) <= 0 || actual_height(source_texture) <= 0) {
        return false;
    }

    auto* native = reinterpret_cast<IDirect3DTexture9*>(
        panorama_texture(s.material_system, source_texture));
    if (!native)
        return false;
    D3DSURFACE_DESC description{};
    if (FAILED(native->GetLevelDesc(0, &description)) ||
        !description.Width || !description.Height ||
        description.Width > 16384 || description.Height > 16384) {
        return false;
    }

    const int width = s.requested_rect.right - s.requested_rect.left;
    const int height = s.requested_rect.bottom - s.requested_rect.top;
    if (description.Width < static_cast<UINT>(width) ||
        description.Height < static_cast<UINT>(height)) {
        return false;
    }

    if (s.panel_texture != native) {
        native->AddRef();
        if (s.panel_texture)
            s.panel_texture->Release();
        s.panel_texture = native;
    }
    s.texture_device = device;
    s.panel_texture_width = static_cast<int>(description.Width);
    s.panel_texture_height = static_cast<int>(description.Height);
    s.panel_texture_name = name;
    s.textured_uv1 = ImVec2(
        std::min(1.f, static_cast<float>(width) / description.Width),
        std::min(1.f, static_cast<float>(height) / description.Height));
    s.textured_rect = s.requested_rect;
    s.textured_side = s.requested_side;
    s.panel_texture_valid = true;
    return true;
}

void premultiplied_image_callback(const ImDrawList*, const ImDrawCmd* command) {
    auto* device = command && command->UserCallbackData
        ? static_cast<IDirect3DDevice9*>(command->UserCallbackData) : nullptr;
    if (device) {
        // Panorama composition layers are premultiplied. ONE/INVSRCALPHA avoids
        // multiplying their edge alpha a second time in ImGui's D3D9 renderer.
        device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
    }
}
} // namespace

namespace agent_preview {
void begin_frame(IDirect3DDevice9* device) {
    auto& s = state();
    std::scoped_lock lock(s.mutex);
    s.drawn = false;
    const ULONGLONG now = GetTickCount64();
    prewarm_locked(s, now);
    if (!s.requested_active || !s.panel_ready || !s.panel_visible || !s.preload_ready ||
        s.panel_side != s.requested_side || !same_rect(s.panel_rect, s.requested_rect)) {
        s.panel_texture_valid = false;
        return;
    }
    // A retry delay must not blank an already acquired isolated texture. The
    // native RT continues to update even when no resource lookup is needed.
    if (now < s.texture_retry_after)
        return;

    // No console gate and no backbuffer read: the native menu stays open while
    // ImGui samples only the isolated RT belonging to our ItemPreviewPanel.
    if (acquire_panel_texture_locked(s, device, now)) {
        s.texture_retry_after = 0;
        if (!s.texture_success_logged) {
            s.texture_success_logged = true;
            AIRFLOW_DEBUG_LOGF("agent_preview isolated Panorama RT acquired (%dx%d)",
                s.panel_texture_width, s.panel_texture_height);
        }
        s.texture_failure_logged = false;
    } else {
        s.texture_retry_after = now + k_texture_retry_ms;
        if (!s.texture_failure_logged) {
            s.texture_failure_logged = true;
            AIRFLOW_DEBUG_LOG("agent_preview isolated Panorama RT is not ready");
        }
    }
}

void draw(const ImVec2& min, const ImVec2& max, int side, bool chams_active) {
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
    visuals_chams::set_preview_model(agent_model(side, selected_agent(side)), chams_active);
    const ULONGLONG now = GetTickCount64();
    synchronize_panel_locked(s, rect, side, now);
    refresh_preload_ready_locked(s, now);

    if (s.panel_texture_valid && s.panel_texture && s.texture_device &&
        s.textured_side == side && same_size(s.textured_rect, rect)) {
        const float alpha = ImGui::GetStyle().Alpha;
        auto* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddCallback(premultiplied_image_callback, s.texture_device);
        draw_list->AddImage(
            reinterpret_cast<ImTextureID>(s.panel_texture), min, max,
            ImVec2(0.f, 0.f), s.textured_uv1,
            // Modulating RGB by the global alpha preserves premultiplication
            // during menu fades as well as during the normal opaque display.
            ImGui::GetColorU32(ImVec4(alpha, alpha, alpha, alpha)));
        draw_list->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
        return;
    }

    // Panorama-only: there is deliberately no static image fallback.
}

void end_frame() {
    auto& s = state();
    std::scoped_lock lock(s.mutex);
    if (s.drawn)
        return;
    visuals_chams::set_preview_model(nullptr, false);
    s.requested_active = false;
    s.panel_texture_valid = false;
    const bool keep_offscreen_warmup = !s.preload_ready && s.panel_visible &&
                                       same_rect(s.panel_rect, warmup_rect());
    if (!keep_offscreen_warmup)
        hide_panel_locked(s);
    release_panel_texture_locked(s);
}

bool is_preloaded() {
    auto& s = state();
    std::scoped_lock lock(s.mutex);
    return s.preload_ready;
}

void on_menu_closed() {
    visuals_chams::set_preview_model(nullptr, false);
    auto& s = state();
    std::scoped_lock lock(s.mutex);
    // Closing the menu is a lifecycle edge. Submit the hide before the next
    // Panorama composition so the independent panel does not linger while
    // ImGui is already gone. Keep the current draw/texture lifetime for
    // end_frame(): an input callback can race a queued ImGui image.
    s.requested_active = false;
    s.panel_texture_valid = false;
    s.next_hide_attempt = 0;
    hide_panel_locked(s);
}

bool contains_point(const ImVec2& point) {
    auto& s = state();
    std::scoped_lock lock(s.mutex);
    return s.requested_active && point.x >= static_cast<float>(s.requested_rect.left) &&
        point.x <= static_cast<float>(s.requested_rect.right) &&
        point.y >= static_cast<float>(s.requested_rect.top) &&
        point.y <= static_cast<float>(s.requested_rect.bottom);
}

void on_device_reset() {
    auto& s = state();
    std::scoped_lock lock(s.mutex);
    release_panel_texture_locked(s);
    s.texture_retry_after = 0;
    s.panel_ready = false;
    s.next_panel_check = 0;
}
} // namespace agent_preview
