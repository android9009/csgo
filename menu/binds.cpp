#include "binds.hpp"
#include "gui.hpp"
#include "hashes.hpp" // ICON_FA_* glyphs for the bind menu rows
#include "../sdk/includes/json/json.h"

// gui.hpp ends with `using namespace std;`, so by the time the Windows headers are parsed here
// std::byte is in scope and the `byte` typedef in rpcndr.h becomes ambiguous (C2872). Renaming
// the Windows typedef for the duration of the include is what darkside.hpp and
// imgui_widgets.cpp already do; keeping the include order means the min/max macros still arrive
// after imgui and nlohmann/json have been parsed.
#ifdef _WIN32
#define byte win_byte_override
#endif
#include <Windows.h>
#ifdef _WIN32
#undef byte
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>
#include <unordered_map>
#include <vector>

namespace menu_binds {
namespace {
enum class value_type { boolean = 0, integer = 1, floating = 2, multi = 3 };
enum class bind_mode { always = 0, hold = 1, toggle = 2 };
constexpr int k_max_multi_items = 32;

struct bind_entry {
    std::uint64_t id = 0;
    int key = 0;
    bind_mode mode = bind_mode::toggle;
    bool bool_value = true;
    int int_value = 0;
    float float_value = 0.f;
    std::array<bool, k_max_multi_items> multi_value{};
    bool toggled = false;
    bool was_down = false;
    bool active = false;
    bool show_in_overlay = true; // the eye in the bind card: list this one in the KEYBINDS panel
};

struct bind_target {
    target_id id = 0;
    std::string key;
    std::string label;
    value_type type = value_type::boolean;
    void* pointer = nullptr;
    bool pointer_seen = false;
    bool base_bool = false;
    int base_int = 0;
    float base_float = 0.f;
    std::array<bool, k_max_multi_items> base_multi{};
    int multi_count = 0;
    std::vector<std::string> multi_items;
    int minimum_int = 0;
    int maximum_int = 100;
    float minimum_float = 0.f;
    float maximum_float = 1.f;
    bool was_overridden = false;
    std::vector<bind_entry> entries;
};

std::unordered_map<target_id, bind_target> g_targets;
std::unordered_map<void*, target_id> g_pointer_targets;
// A multibox owns an array, while existing explicit bool binds may own its individual elements.
// Keep its base-pointer registry separate so those two levels never overwrite one another.
std::unordered_map<void*, target_id> g_multi_pointer_targets;
std::uint64_t g_next_entry_id = 1;
bool g_loaded = false;
bool g_dirty = false;
std::uint64_t g_capture_entry = 0;
std::array<bool, 0xA6> g_capture_down{};
constexpr const char* k_bind_file = "c:\\airflow\\binds.json";

// --- Bind menu state ------------------------------------------------------------------------
// The menu is a root card (one row per bind / New Bind / Hotkeys / Reset) whose
// rows open a child card on *hover*, the way neverlose's does. Only one child is up at a time,
// so which one it is lives here rather than per row.
//
// g_submenu_opened gates the request we hand to gui.popup_animation(): that call toggles, so
// re-asserting "open" every frame would slam the card shut again, and an outside click (hitting
// another row of the root card, which ImGui treats as a click outside the child) would otherwise
// be undone on the very next frame. One request per latch change, and the latch only changes
// when a different row is hovered.
constexpr std::uint64_t k_draft_entry = ~0ull;
std::uint64_t g_submenu = 0;       // entry id of the open child card (k_draft_entry = "New Bind")
bool g_submenu_opened = false;     // the open request for g_submenu has already been issued
target_id g_draft_target = 0;      // which target the pending "New Bind" belongs to
bind_entry g_draft{};              // the pending bind, committed once it gets a key
bool g_overlay_enabled = true;     // draw the on-screen keybind list
bool g_all_hotkeys_open = false;   // floating All Hotkeys editor
bool g_hotkeys_capture = false;    // the active key capture belongs to that editor
std::unordered_map<std::uint64_t, float> g_entry_reveal;

std::uint64_t hash_key(const std::string& value) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const unsigned char c : value) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash;
}

bool belongs_to_main_menu(ImGuiWindow* window) {
    if (!window)
        return false;
    ImGuiWindow* main = ImGui::FindWindowByName("DARKSIDE##neverlose");
    if (!main)
        return false;
    if (window == main || window->RootWindow == main)
        return true;
    // Gear cards are popup roots, but their controls are still part of the main menu. Only the
    // windows explicitly marked by row_options_begin() qualify: bind editors and color-picker
    // internals must never register themselves recursively just because their ancestor is main.
    if (!window->StateStorage.GetBool(ImHashStr("##BindOptionsWindow"), false))
        return false;
    for (const ImGuiWindow* parent = window; parent; parent = parent->ParentWindow)
        if (parent == main)
            return true;
    return false;
}

const char* mode_name(bind_mode mode) {
    switch (mode) {
        case bind_mode::always: return "Always";
        case bind_mode::hold: return "Hold";
        case bind_mode::toggle: return "Toggle";
    }
    return "Hold";
}

std::string key_name(int key) {
    switch (key) {
        case 0: return "None";
        case VK_LBUTTON: return "Mouse 1";
        case VK_RBUTTON: return "Mouse 2";
        case VK_MBUTTON: return "Mouse 3";
        case VK_XBUTTON1: return "Mouse 4";
        case VK_XBUTTON2: return "Mouse 5";
        case VK_SHIFT: return "Shift";
        case VK_CONTROL: return "Ctrl";
        case VK_MENU: return "Alt";
        case VK_SPACE: return "Space";
        case VK_INSERT: return "Insert";
        case VK_DELETE: return "Delete";
        case VK_HOME: return "Home";
        case VK_END: return "End";
        case VK_PRIOR: return "Page Up";
        case VK_NEXT: return "Page Down";
        case VK_ESCAPE: return "Escape";
    }

    UINT scan = MapVirtualKeyA(static_cast<UINT>(key), MAPVK_VK_TO_VSC);
    if (key == VK_LEFT || key == VK_UP || key == VK_RIGHT || key == VK_DOWN ||
        key == VK_RCONTROL || key == VK_RMENU || key == VK_DIVIDE || key == VK_NUMLOCK)
        scan |= 0x100;

    char name[64]{};
    if (GetKeyNameTextA(static_cast<LONG>(scan << 16), name, static_cast<int>(sizeof(name))) > 0)
        return name;
    return "VK " + std::to_string(key);
}

// What a bind calls itself in the menu: `Toggle "N"`, `Hold "D"`, `Always`, or - while it has no
// key yet - the "New Bind" row it was created from.
std::string entry_label(const bind_entry& entry) {
    if (entry.mode == bind_mode::always)
        return gui.tr("Always");
    if (entry.key <= 0)
        return gui.tr("New Bind");
    return std::string(gui.tr(mode_name(entry.mode))) + " \"" + key_name(entry.key) + "\"";
}

void mark_dirty() {
    g_dirty = true;
}

void load_once() {
    if (g_loaded)
        return;
    g_loaded = true;

    std::ifstream input(k_bind_file);
    if (!input)
        return;

    nlohmann::json data;
    try {
        input >> data;
    } catch (const nlohmann::json::exception&) {
        return;
    }
    if (!data.is_object() || !data.contains("targets") || !data["targets"].is_array())
        return;

    g_overlay_enabled = data.value("hotkeys", true);

    try {
        for (const auto& saved_target : data["targets"]) {
        if (!saved_target.is_object())
            continue;
        const std::string key = saved_target.value("key", std::string{});
        if (key.empty())
            continue;

        bind_target target{};
        target.key = key;
        target.id = hash_key(key);
        target.label = saved_target.value("label", key);
        const int saved_type = saved_target.value("type", 0);
        target.type = saved_type == 1 ? value_type::integer
            : saved_type == 2 ? value_type::floating
            : saved_type == 3 ? value_type::multi : value_type::boolean;

        if (saved_target.contains("entries") && saved_target["entries"].is_array()) {
            for (const auto& saved_entry : saved_target["entries"]) {
                if (!saved_entry.is_object() || target.entries.size() >= 16)
                    continue;
                bind_entry entry{};
                entry.id = saved_entry.value("id", g_next_entry_id++);
                g_next_entry_id = (std::max)(g_next_entry_id, entry.id + 1);
                entry.key = std::clamp(saved_entry.value("key", 0), 0, 0xA5);
                entry.mode = static_cast<bind_mode>(std::clamp(saved_entry.value("mode", 1), 0, 2));
                entry.bool_value = saved_entry.value("bool_value", true);
                entry.int_value = saved_entry.value("int_value", 0);
                entry.float_value = saved_entry.value("float_value", 0.f);
                if (saved_entry.contains("multi_value") && saved_entry["multi_value"].is_array()) {
                    int multi_index = 0;
                    for (const auto& selected : saved_entry["multi_value"]) {
                        if (multi_index >= k_max_multi_items)
                            break;
                        entry.multi_value[multi_index++] = selected.get<bool>();
                    }
                }
                entry.show_in_overlay = saved_entry.value("show_in_overlay", true);
                if (std::isfinite(entry.float_value))
                    target.entries.push_back(entry);
            }
        }
            if (!target.entries.empty())
                g_targets[target.id] = std::move(target);
        }
    } catch (const nlohmann::json::exception&) {
        // Keep valid entries already decoded and ignore the malformed remainder.
    }
}

void save_if_dirty() {
    if (!g_dirty)
        return;

    std::error_code error;
    std::filesystem::create_directories("c:\\airflow\\", error);
    if (error)
        return;

    nlohmann::json data;
    data["version"] = 1;
    data["hotkeys"] = g_overlay_enabled;
    data["targets"] = nlohmann::json::array();
    for (const auto& [id, target] : g_targets) {
        if (target.entries.empty() || !target.pointer_seen)
            continue;
        nlohmann::json saved_target;
        saved_target["key"] = target.key;
        saved_target["label"] = target.label;
        saved_target["type"] = static_cast<int>(target.type);
        saved_target["entries"] = nlohmann::json::array();
        for (const auto& entry : target.entries) {
            nlohmann::json saved_entry = {
                {"id", entry.id}, {"key", entry.key}, {"mode", static_cast<int>(entry.mode)},
                {"bool_value", entry.bool_value}, {"int_value", entry.int_value},
                {"float_value", entry.float_value}, {"show_in_overlay", entry.show_in_overlay}
            };
            if (target.type == value_type::multi) {
                saved_entry["multi_value"] = nlohmann::json::array();
                for (int i = 0; i < target.multi_count; ++i)
                    saved_entry["multi_value"].push_back(entry.multi_value[i]);
            }
            saved_target["entries"].push_back(std::move(saved_entry));
        }
        data["targets"].push_back(std::move(saved_target));
    }

    std::ofstream output(k_bind_file, std::ios::trunc);
    if (!output)
        return;
    output << std::setw(2) << data;
    output.flush();
    if (output)
        g_dirty = false;
}

void begin_capture(bind_entry& entry, bool is_draft = false) {
    // A draft's id is regenerated every frame the "New Bind" row re-latches, so it can't be the
    // capture handle - key it off the stable k_draft_entry sentinel instead. Real entries keep
    // their own id (stable inside target.entries).
    g_capture_entry = is_draft ? k_draft_entry : entry.id;
    for (int key = 1; key <= 0xA5; ++key)
        g_capture_down[key] = (GetAsyncKeyState(key) & 0x8000) != 0;
}

bool update_capture(bind_entry& entry, bool is_draft = false) {
    const std::uint64_t handle = is_draft ? k_draft_entry : entry.id;
    if (g_capture_entry != handle)
        return false;

    ImGui::SetNextFrameWantCaptureKeyboard(true);
    for (int key = 1; key <= 0xA5; ++key) {
        const bool down = (GetAsyncKeyState(key) & 0x8000) != 0;
        if (down && !g_capture_down[key]) {
            if (key == VK_INSERT || key == VK_END) {
                g_capture_down[key] = down;
                continue;
            }
            if (key == VK_ESCAPE) {
                g_capture_entry = 0;
                return false;
            }
            if (key == VK_BACK || key == VK_DELETE)
                entry.key = 0;
            else
                entry.key = key;
            entry.was_down = entry.key > 0 && (GetAsyncKeyState(entry.key) & 0x8000) != 0;
            entry.toggled = false;
            g_capture_entry = 0;
            mark_dirty();
            return true;
        }
        g_capture_down[key] = down;
    }
    return false;
}

// write_base() is defined below, next to the rest of the pointer plumbing.
void write_base(bind_target& target);

// --- Bind menu -------------------------------------------------------------------------------
//
// Layout mirrors neverlose's: a gear in the gap left of the row's control opens a context menu
// (right-clicking the row does the same), and every bind in it opens its own card on hover.
//
//   ##bind_menu (root card)
//   Toggle "N"                >           -> ##bind_sub_<id>    Key / Mode / Value + actions
//   Hold "D"                  >
//   New Bind                  >           -> the same popup id when its draft is committed
//   Hotkeys
//   ----------
//   Reset

// The gui.cpp row helpers (cfg_anim / cfg_mix) are file-static over there, so the bind cards keep
// their own rather than widening that header for three functions.
float row_anim(ImGuiID id, bool active, float speed = 15.f) {
    static std::unordered_map<ImGuiID, float> anims;
    float& a = anims[id];
    a = ImLerp(a, active ? 1.f : 0.f, 1.f - std::exp(-speed * ImMin(ImGui::GetIO().DeltaTime, 0.05f)));
    return a;
}

// Existing/loaded rows start at full height. A newly committed row is explicitly seeded with 0,
// then both its opacity and layout height ease in, so the auto-height parent never jumps.
float entry_reveal(std::uint64_t entry_id) {
    auto [it, inserted] = g_entry_reveal.try_emplace(entry_id, 1.f);
    IM_UNUSED(inserted);
    float& amount = it->second;
    const float dt = ImMin(ImGui::GetIO().DeltaTime, 0.05f);
    amount = ImLerp(amount, 1.f, 1.f - std::exp(-18.f * dt));
    if (amount > 0.998f)
        amount = 1.f;
    return amount;
}

ImVec4 lerp_color(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                  a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

ImU32 mix_color(const ImVec4& a, const ImVec4& b, float t) {
    return ImGui::GetColorU32(lerp_color(a, b, t));
}

void request_submenu(std::uint64_t entry_id) {
    if (g_submenu == entry_id)
        return;
    g_submenu = entry_id;
    g_submenu_opened = false; // one open request per latch change - see g_submenu_opened
}

struct menu_row {
    ImVec2 pos;
    float width = 0.f;
    float height = 0.f;
    bool hovered = false;
    bool clicked = false;
};

// One line of a bind card. `lit` keeps a row highlighted while whatever it opened is still up.
menu_row bind_menu_row(const char* id, const char* icon, const char* label, bool chevron,
                       bool lit, const ImVec4& tint, bool dim = false, float reveal = 1.f) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImFont* font = ImGui::GetFont();
    const float s = gui.m_scale;
    const float w = gui.row_content_width();
    const float reveal_t = ImSaturate(reveal);
    const float h = 30.f * s * reveal_t;
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    if (h > 0.01f)
        ImGui::InvisibleButton(id, ImVec2(w, h));
    else
        ImGui::Dummy(ImVec2(w, 0.f));
    // A child card is a focused popup sitting above this one, and ImGui refuses every hover test
    // on items of a popup something is open on top of - which is exactly the gesture that has to
    // keep working here. Parent and child never overlap, so test the rect directly and take the
    // click the same way instead of relying on this item's own hover state.
    menu_row row{ pos, w, h, reveal_t > 0.95f &&
        ImGui::IsMouseHoveringRect(pos, pos + ImVec2(w, h), true), false };
    row.clicked = row.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * reveal_t);
    const float t = row_anim(ImGui::GetID(id), row.hovered || lit);
    if (t > 0.01f)
        draw->AddRectFilled(pos, pos + ImVec2(w, h), gui.frame_inactive.to_im_color(t * 0.7f), 6.f * s);

    const ImVec4 base = dim ? gui.text_disabled.to_vec4(1.f, false)
                            : ImVec4(tint.x, tint.y, tint.z, tint.w * 0.82f);
    const ImU32 fg = mix_color(base, tint, t);
    const float fs = gui.scaled_font_size(font), icon_fs = 13.f * s, cy = pos.y + h * 0.5f;

    const float lx = pos.x + 8.f * s;
    if (icon) {
        const ImVec2 isz = font->CalcTextSizeA(icon_fs, FLT_MAX, 0.f, icon);
        draw->AddText(font, icon_fs, ImVec2(IM_ROUND(lx + (16.f * s - isz.x) * 0.5f),
                                            IM_ROUND(cy - isz.y * 0.5f)), fg, icon);
    }

    const char* shown = gui.tr(label);
    const char* end = ImGui::FindRenderedTextEnd(shown);
    draw->AddText(font, fs, ImVec2(lx + 25.f * s, IM_ROUND(cy - TextLineCenterY(font, fs))),
                  fg, shown, end);

    if (chevron) {
        const ImVec2 csz = font->CalcTextSizeA(icon_fs, FLT_MAX, 0.f, ICON_FA_CHEVRON_RIGHT);
        draw->AddText(font, icon_fs, ImVec2(IM_ROUND(pos.x + w - 6.f * s - csz.x),
                                            IM_ROUND(cy - csz.y * 0.5f)), fg, ICON_FA_CHEVRON_RIGHT);
    }
    ImGui::PopStyleVar();
    return row;
}

void bind_menu_separator() {
    const float s = gui.m_scale, w = gui.row_content_width();
    const ImVec2 p = ImGui::GetCursorScreenPos() + ImVec2(0.f, 4.f * s);
    ImGui::GetWindowDrawList()->AddLine(p, p + ImVec2(w, 0.f), gui.popup_border.to_im_color(0.55f), 1.f * s);
    ImGui::Dummy(ImVec2(w, 9.f * s));
}

// "Key  [ N ]" - clicking the box arms begin_capture(); the next key pressed lands in `entry`.
bool bind_key_row(bind_entry& entry, bool is_draft = false) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImFont* font = ImGui::GetFont();
    const float s = gui.m_scale;
    const float w = gui.row_content_width(), h = 30.f * s;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##key", ImVec2(w, h));

    const float fs = gui.scaled_font_size(font), cy = pos.y + h * 0.5f;
    draw->AddText(font, fs, ImVec2(pos.x + 8.f * s, IM_ROUND(cy - TextLineCenterY(font, fs))),
                  gui.text.to_im_color(1.f, false), gui.tr("Key"));

    const bool capturing = g_capture_entry == (is_draft ? k_draft_entry : entry.id);
    const std::string shown = capturing ? std::string("...")
        : (entry.key > 0 ? key_name(entry.key) : std::string("-"));
    const float box_h = 22.f * s;
    const float box_w = ImMax(40.f * s, font->CalcTextSizeA(fs, FLT_MAX, 0.f, shown.c_str()).x + 16.f * s);
    const ImVec2 box_min(pos.x + w - 6.f * s - box_w, IM_ROUND(cy - box_h * 0.5f));
    const ImRect box(box_min, box_min + ImVec2(box_w, box_h));

    const bool hovered = ImGui::IsMouseHoveringRect(box.Min, box.Max, true);
    bool started = false;
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !capturing) {
        begin_capture(entry, is_draft);
        started = true;
    }
    const float t = row_anim(ImGui::GetID("##keybox"), hovered || capturing);
    draw->AddRectFilled(box.Min, box.Max, gui.field_bg.to_im_color(0.9f, false), 5.f * s);
    if (t > 0.01f)
        draw->AddRect(box.Min, box.Max, gui.accent_color.to_im_color(t, false), 5.f * s, 0, 1.f * s);
    const ImVec2 tsz = font->CalcTextSizeA(fs, FLT_MAX, 0.f, shown.c_str());
    draw->AddText(font, fs, ImVec2(IM_ROUND(box.GetCenter().x - tsz.x * 0.5f),
                                   IM_ROUND(box.GetCenter().y - TextLineCenterY(font, fs))),
                  capturing ? gui.accent_color.to_im_color(1.f, false) : gui.text.to_im_color(1.f, false),
                  shown.c_str());
    return started;
}

// "Mode  [ Toggle | Hold ]". An `always` bind (only reachable from an older binds.json) lights
// neither segment; picking either one converts it.
bool bind_mode_row(bind_entry& entry) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImFont* font = ImGui::GetFont();
    const float s = gui.m_scale;
    const float w = gui.row_content_width(), h = 30.f * s;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##mode", ImVec2(w, h));

    const float fs = gui.scaled_font_size(font), cy = pos.y + h * 0.5f;
    draw->AddText(font, fs, ImVec2(pos.x + 8.f * s, IM_ROUND(cy - TextLineCenterY(font, fs))),
                  gui.text.to_im_color(1.f, false), gui.tr("Mode"));

    static const char* names[] = { "Toggle", "Hold" };
    static const bind_mode modes[] = { bind_mode::toggle, bind_mode::hold };
    const float seg_h = 22.f * s, seg_w = 46.f * s, gap = 3.f * s;
    const float right = pos.x + w - 6.f * s, top = IM_ROUND(cy - seg_h * 0.5f);
    bool changed = false;

    for (int i = 0; i < 2; ++i) {
        // Not `min`: Windows.h defines that as a macro and the project does not set NOMINMAX.
        const ImVec2 seg_pos(right - (seg_w + gap) * (2 - i) + gap, top);
        const ImRect bb(seg_pos, seg_pos + ImVec2(seg_w, seg_h));
        const bool hovered = ImGui::IsMouseHoveringRect(bb.Min, bb.Max, true);
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && entry.mode != modes[i]) {
            entry.mode = modes[i];
            entry.toggled = false;
            entry.was_down = entry.key > 0 && (GetAsyncKeyState(entry.key) & 0x8000) != 0;
            changed = true;
        }
        const float t = row_anim(ImGui::GetID(names[i]), entry.mode == modes[i]);
        if (hovered)
            draw->AddRectFilled(bb.Min, bb.Max, gui.frame_inactive.to_im_color(0.7f), 5.f * s);
        const char* shown = gui.tr(names[i]);
        const ImVec2 tsz = font->CalcTextSizeA(fs, FLT_MAX, 0.f, shown);
        ImVec4 ink = ImLerp(gui.text_disabled.to_vec4(1.f, false), gui.text.to_vec4(1.f, false),
                            ImMax(t, hovered ? 0.6f : 0.f));
        ink = ImLerp(ink, gui.accent_color.to_vec4(1.f, false), t);
        draw->AddText(font, fs, ImVec2(IM_ROUND(bb.GetCenter().x - tsz.x * 0.5f),
                                       IM_ROUND(bb.GetCenter().y - TextLineCenterY(font, fs))),
                      ImGui::GetColorU32(ink), shown);
    }
    return changed;
}

// Value keeps the control's own type: switch for booleans, slider for numbers and a real
// multibox for an option set. No boolean switch is added to numeric or multi-value binds.
bool bind_value_row(const bind_target& target, bind_entry& entry) {
    if (target.type == value_type::multi) {
        const char* items[k_max_multi_items]{};
        for (int i = 0; i < target.multi_count; ++i)
            items[i] = target.multi_items[i].c_str();
        return gui.row_multibox("Value", items, entry.multi_value.data(), target.multi_count,
                                "Select the values applied while this bind is active.");
    }
    if (target.type == value_type::integer)
        return ImGui::SliderInt("Value", &entry.int_value, target.minimum_int, target.maximum_int);
    if (target.type == value_type::floating)
        return ImGui::SliderFloat("Value", &entry.float_value, target.minimum_float,
                                  target.maximum_float, "%.2f");

    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImFont* font = ImGui::GetFont();
    const float s = gui.m_scale;
    const float w = gui.row_content_width(), h = 30.f * s;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##value", ImVec2(w, h));

    const float fs = gui.scaled_font_size(font), cy = pos.y + h * 0.5f;
    draw->AddText(font, fs, ImVec2(pos.x + 8.f * s, IM_ROUND(cy - TextLineCenterY(font, fs))),
                  gui.text.to_im_color(1.f, false), gui.tr("Value"));

    const ImVec2 sz(gui.switch_w(), gui.switch_h());
    // Not `min`: Windows.h defines that as a macro and the project does not set NOMINMAX.
    const ImVec2 sw_pos(pos.x + w - 6.f * s - sz.x, IM_ROUND(cy - sz.y * 0.5f));
    const ImRect bb(sw_pos, sw_pos + sz);
    bool changed = false;
    if (ImGui::IsMouseHoveringRect(bb.Min, bb.Max, true) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        entry.bool_value = !entry.bool_value;
        changed = true;
    }
    gui.draw_switch_body(draw, bb, row_anim(ImGui::GetID("##valueswitch"), entry.bool_value, 12.f));
    return changed;
}

// Trash / eye / clone along the bottom of a bind card.
enum class card_action { none, remove, duplicate };

card_action bind_card_actions(bind_entry& entry, bool is_draft) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImFont* font = ImGui::GetFont();
    const float s = gui.m_scale;
    const float w = gui.row_content_width(), h = 28.f * s;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##actions", ImVec2(w, h));

    const char* icons[] = { ICON_FA_TRASH_ALT,
                            entry.show_in_overlay ? ICON_FA_EYE : ICON_FA_EYE_SLASH,
                            ICON_FA_CLONE };
    const char* ids[] = { "##act_del", "##act_eye", "##act_dup" };
    const float icon_fs = 14.f * s, slot = w / 3.f;
    card_action action = card_action::none;

    for (int i = 0; i < 3; ++i) {
        const ImRect bb(ImVec2(pos.x + slot * i, pos.y), ImVec2(pos.x + slot * (i + 1), pos.y + h));
        // A pending bind has nothing to delete or clone yet - only the eye is live on it.
        const bool enabled = !is_draft || i == 1;
        const bool hovered = enabled && ImGui::IsMouseHoveringRect(bb.Min, bb.Max, true);
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (i == 0)
                action = card_action::remove;
            else if (i == 1) {
                entry.show_in_overlay = !entry.show_in_overlay;
                mark_dirty();
            }
            else
                action = card_action::duplicate;
        }
        const float t = row_anim(ImGui::GetID(ids[i]), hovered);
        const ImVec4 idle = gui.text_disabled.to_vec4(enabled ? 1.f : 0.4f, false);
        const ImVec4 hot = i == 0 ? ImVec4(0.93f, 0.33f, 0.36f, 1.f) : gui.text.to_vec4(1.f, false);
        const ImVec2 isz = font->CalcTextSizeA(icon_fs, FLT_MAX, 0.f, icons[i]);
        draw->AddText(font, icon_fs, ImVec2(IM_ROUND(bb.GetCenter().x - isz.x * 0.5f),
                                            IM_ROUND(bb.GetCenter().y - isz.y * 0.5f)),
                      mix_color(idle, hot, t), icons[i]);
    }
    return action;
}

// The card a bind row opens, anchored to the right edge of the card `parent` and level with the
// row it belongs to, so it reads as growing out of that row.
// Returns false once `entry` must not be touched again this frame - it was deleted, or the
// vector behind it was reallocated by a duplicate.
bool draw_entry_card(bind_target& target, bind_entry& entry, bool is_draft,
                     ImGuiWindow* parent, float row_y, bool& removed, bool& committed) {
    const float s = gui.m_scale;
    char name[48];
    ImFormatString(name, IM_ARRAYSIZE(name), "##bind_sub_%llu", (unsigned long long)entry.id);

    const std::uint64_t latch = is_draft ? k_draft_entry : entry.id;
    const bool open_request = g_submenu == latch && !g_submenu_opened;
    if (open_request)
        g_submenu_opened = true;

    bool wants_open = false;
    const float anim = gui.popup_animation(name, open_request, wants_open);
    const ImVec2 at(parent->Pos.x + parent->Size.x + 8.f * s, row_y - 10.f * s);
    if (!gui.begin_popup_card_at(name, wants_open, anim, at, ImVec2(0.f, 0.f), 196.f))
        return true;

    if (bind_key_row(entry, is_draft))
        mark_dirty();
    // Has to run every frame the card is up, not only when the box was clicked: this is what
    // actually reads the keyboard while the bind is armed.
    const bool got_key = update_capture(entry, is_draft);
    if (bind_mode_row(entry))
        mark_dirty();
    if (bind_value_row(target, entry))
        mark_dirty();
    bind_menu_separator();
    const card_action action = bind_card_actions(entry, is_draft);
    gui.end_popup_card();

    if (action == card_action::duplicate && !is_draft && target.entries.size() < 16) {
        if (g_capture_entry == entry.id) {
            g_capture_entry = 0;
            g_hotkeys_capture = false;
        }
        bind_entry copy = entry;
        copy.id = g_next_entry_id++;
        copy.toggled = copy.was_down = copy.active = false;
        g_entry_reveal[copy.id] = 0.f;
        target.entries.push_back(copy);
        request_submenu(copy.id);
        mark_dirty();
        return false; // `entry` points into the vector that just reallocated
    }
    if (action == card_action::remove && !is_draft) {
        removed = true;
        return false;
    }
    // A draft becomes a real bind the moment it is given a key. The card stays up and now belongs
    // to that bind, which is what turns the "New Bind" row into `Toggle "N"`.
    if (is_draft && got_key && entry.key > 0)
        committed = true;
    return true;
}

void draw_bind_menu(bind_target& target, const ImRect& rect, const ImRect& control) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImDrawList* draw = window->DrawList;
    ImFont* font = ImGui::GetFont();
    const float s = gui.m_scale;
    IM_UNUSED(font);

    ImGui::PushID(target.key.c_str());
    const bool row_hovered = ImGui::IsMouseHoveringRect(rect.Min, rect.Max, true);
    const bool has_binds = !target.entries.empty();

    // Draw a subtle accent-colored dot on the right side of the control to indicate binds exist
    if (has_binds) {
        const float dot_r = 3.f * s;
        const ImVec2 dot_center(control.Max.x + 6.f * s, control.GetCenter().y);
        draw->AddCircleFilled(dot_center, dot_r, gui.accent_color.to_im_color(0.85f));
    }

    // Right-click on the row opens the bind menu (no gear icon)
    const bool open_menu = row_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);

    bool wants_open = false;
    const float anim = gui.popup_animation("##bind_menu", open_menu, wants_open);
    // Keep the outer card's top edge fixed. Its rows are appended above New Bind, so a new
    // hotkey extends the card downward instead of making the centered card jump upward.
    const ImVec2 anchor(rect.Max.x + 8.f * s, rect.Min.y - 10.f * s);
    if (gui.begin_popup_card_at("##bind_menu", wants_open, anim, anchor, ImVec2(0.f, 0.f), 184.f)) {
        ImGuiWindow* card = ImGui::GetCurrentWindow();
        const ImVec4 c_text = gui.text.to_vec4(1.f, false);
        const ImVec4 c_red(0.93f, 0.33f, 0.36f, 1.f);

        // A click on one of this card's own rows lands outside the child card, which is ImGui's
        // cue to dismiss it. Drop the latch along with it, or the row it belonged to would look
        // inert until the cursor had visited some other row first. Tested against the card's rect
        // rather than its hover state: the child never overlaps the parent, and hover tests on a
        // popup with something open above it are refused. Done before the rows, so whichever one
        // the cursor is on re-latches in the same frame.
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            ImGui::IsMouseHoveringRect(card->Pos, card->Pos + card->Size, false))
            request_submenu(0);

        int remove_index = -1;
        bool commit_draft = false, bail = false;
        for (int i = 0; i < static_cast<int>(target.entries.size()); ++i) {
            bind_entry& entry = target.entries[i];
            ImGui::PushID(static_cast<int>(entry.id));
            const std::string label = entry_label(entry);
            const menu_row row = bind_menu_row("##bind", ICON_FA_BOLT, label.c_str(), true,
                                               g_submenu == entry.id, c_text, false,
                                               entry_reveal(entry.id));
            if (row.hovered)
                request_submenu(entry.id);
            ImGui::PopID();
            bool removed = false, ignored = false;
            const bool alive = draw_entry_card(target, entry, false, card, row.pos.y, removed, ignored);
            if (removed)
                remove_index = i;
            if (!alive) {
                bail = true; // duplicated (vector reallocated) or deleted - leave the rest for next frame
                break;
            }
        }

        if (!bail) {
            if (target.entries.size() < 16) {
                const menu_row row = bind_menu_row("##new", ICON_FA_PLUS_CIRCLE, "New Bind", true,
                                                   g_submenu == k_draft_entry, c_text, true);
                // Don't regenerate the draft while its key is being captured: a stray re-latch
                // (e.g. the key-box click also landing in the parent card's rect and resetting
                // g_submenu) would otherwise wipe the pending bind out from under the capture.
                const bool capturing_draft = g_capture_entry == k_draft_entry && g_draft_target == target.id;
                if (row.hovered && g_submenu != k_draft_entry && !capturing_draft) {
                    // A fresh pending bind, pre-filled against what the row is set to right now.
                    g_draft = bind_entry{};
                    g_draft.id = g_next_entry_id++;
                    g_draft.mode = bind_mode::toggle;
                    g_draft.bool_value = target.type == value_type::boolean ? !target.base_bool : true;
                    g_draft.int_value = std::clamp(target.base_int, target.minimum_int, target.maximum_int);
                    g_draft.float_value = std::clamp(target.base_float, target.minimum_float, target.maximum_float);
                    g_draft.multi_value = target.base_multi;
                    g_draft_target = target.id;
                    request_submenu(k_draft_entry);
                }
                // If a re-latch pulled the submenu off the draft while it was still capturing,
                // put it back so the card stays open on the pending bind.
                if (capturing_draft && g_submenu != k_draft_entry)
                    request_submenu(k_draft_entry);
                // Submitted even when the latch has moved on, so the card gets to fade out.
                if (g_draft_target == target.id) {
                    bool ignored = false;
                    draw_entry_card(target, g_draft, true, card, row.pos.y, ignored, commit_draft);
                }
            }

            if (bind_menu_row("##hotkeys", ICON_FA_LIST, "Hotkeys", false,
                              g_all_hotkeys_open, c_text).clicked) {
                g_all_hotkeys_open = true;
                request_submenu(0);
                ImGui::CloseCurrentPopup();
            }
            bind_menu_separator();
            if (bind_menu_row("##reset", ICON_FA_TRASH_ALT, "Reset", false, false, c_red).clicked &&
                has_binds) {
                for (const bind_entry& entry : target.entries) {
                    if (g_capture_entry == entry.id)
                        g_capture_entry = 0;
                    g_entry_reveal.erase(entry.id);
                }
                target.entries.clear();
                write_base(target);
                target.was_overridden = false;
                request_submenu(0);
                mark_dirty();
                remove_index = -1;
                commit_draft = false;
            }
        }

        gui.end_popup_card();

        if (commit_draft) {
            g_entry_reveal[g_draft.id] = 0.f;
            target.entries.push_back(g_draft);
            request_submenu(g_draft.id);
            // The draft and committed entry deliberately share one popup ID. Keep it open instead
            // of closing the draft card and opening a second one on the next frame.
            g_submenu_opened = true;
            g_draft = bind_entry{};
            g_draft_target = 0;
            mark_dirty();
        }
        else if (remove_index >= 0) {
            if (g_capture_entry == target.entries[remove_index].id)
                g_capture_entry = 0;
            g_entry_reveal.erase(target.entries[remove_index].id);
            target.entries.erase(target.entries.begin() + remove_index);
            request_submenu(0);
            mark_dirty();
        }
    }
    else if (!wants_open && g_capture_entry != 0) {
        // The menu went away mid-capture (clicked elsewhere, tab switched): disarm, or the next
        // key press would be eaten on behalf of a card nobody can see.
        const bool mine = (g_capture_entry == k_draft_entry && g_draft_target == target.id) ||
            std::any_of(target.entries.begin(), target.entries.end(),
                        [](const bind_entry& entry) { return entry.id == g_capture_entry; });
        if (mine)
            g_capture_entry = 0;
    }
    ImGui::PopID();
}

// --- All Hotkeys ------------------------------------------------------------------------------
// A searchable, spaced-out index of every registered bind. Feature/edit opens the same full card
// as the context menu; Key / Mode / Visibility / New Value edit that same entry inline, while
// duplication, deletion, persistence and runtime synchronization continue to use the shared data.
std::string lower_copy(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

bool hotkey_matches(const bind_target& target, const bind_entry& entry, const std::string& needle) {
    if (needle.empty())
        return true;
    const std::string haystack = lower_copy(target.key + " " + target.label + " " +
        entry_label(entry) + " " + key_name(entry.key) + " " + mode_name(entry.mode));
    return haystack.find(needle) != std::string::npos;
}

std::string hotkey_value(const bind_target& target, const bind_entry& entry) {
    if (target.type == value_type::multi) {
        std::string preview;
        for (int i = 0; i < target.multi_count; ++i) {
            if (!entry.multi_value[i])
                continue;
            if (!preview.empty())
                preview += ", ";
            preview += gui.tr(target.multi_items[i].c_str());
        }
        return preview.empty() ? std::string(gui.tr("Select")) : preview;
    }
    if (target.type == value_type::boolean)
        return gui.tr(entry.bool_value ? "On" : "Off");
    if (target.type == value_type::integer)
        return std::to_string(entry.int_value);
    char text[32];
    ImFormatString(text, IM_ARRAYSIZE(text), "%.2f", entry.float_value);
    return text;
}

struct hotkey_columns {
    float feature = 0.f;
    float key = 0.f;
    float mode = 0.f;
    float visibility = 0.f;
    float value = 0.f;
    float edit = 0.f;
    float remove = 0.f;
};

hotkey_columns hotkey_columns_at(ImVec2 pos, float width) {
    const float s = gui.m_scale;
    hotkey_columns columns;
    columns.feature = pos.x + 10.f * s;
    columns.key = pos.x + width * 0.34f;
    columns.mode = pos.x + width * 0.46f;
    columns.visibility = pos.x + width * 0.58f;
    columns.value = pos.x + width * 0.73f;
    columns.edit = pos.x + width - 48.f * s;
    columns.remove = pos.x + width - 20.f * s;
    return columns;
}

void draw_hotkey_columns_header() {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImFont* font = ImGui::GetFont();
    const float s = gui.m_scale, fs = gui.scaled_font_size(font);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const hotkey_columns c = hotkey_columns_at(pos, width);
    const ImU32 color = ImGui::GetColorU32(gui.text_disabled.to_vec4(0.9f, false));
    const float y = IM_ROUND(pos.y + 3.f * s);
    draw->AddText(font, fs, ImVec2(c.feature, y), color, gui.tr("Feature"));
    draw->AddText(font, fs, ImVec2(c.key, y), color, gui.tr("Key"));
    draw->AddText(font, fs, ImVec2(c.mode, y), color, gui.tr("Mode"));
    draw->AddText(font, fs, ImVec2(c.visibility, y), color, gui.tr("Visibility"));
    draw->AddText(font, fs, ImVec2(c.value, y), color, gui.tr("New Value"));
    ImGui::Dummy(ImVec2(width, 22.f * s));
}

enum class hotkey_cell { none, key, mode, visibility, value };

struct hotkey_row {
    ImVec2 pos{};
    bool clicked = false;
    bool remove = false;
    hotkey_cell cell = hotkey_cell::none;
};

hotkey_row draw_hotkey_row(const bind_target& target, bind_entry& entry) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImFont* font = ImGui::GetFont();
    const float s = gui.m_scale;
    const float reveal = entry_reveal(entry.id);
    const float full_h = 42.f * s;
    const float h = full_h * reveal;
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    if (h > 0.01f)
        ImGui::InvisibleButton("##hotkey_row", ImVec2(width, h));
    else
        ImGui::Dummy(ImVec2(width, 0.f));
    ImGui::Dummy(ImVec2(0.f, 6.f * s * reveal)); // deliberate breathing room between rows

    hotkey_row row;
    row.pos = pos;
    const bool hovered = reveal > 0.95f &&
        ImGui::IsMouseHoveringRect(pos, pos + ImVec2(width, h), true);
    const hotkey_columns c = hotkey_columns_at(pos, width);
    const float cell_top = pos.y + 4.f * s;
    const float cell_bottom = pos.y + full_h - 4.f * s;
    const ImRect key_bb(ImVec2(c.key - 6.f * s, cell_top),
                        ImVec2(c.mode - 8.f * s, cell_bottom));
    const ImRect mode_bb(ImVec2(c.mode - 6.f * s, cell_top),
                         ImVec2(c.visibility - 8.f * s, cell_bottom));
    const ImRect visibility_bb(ImVec2(c.visibility - 6.f * s, cell_top),
                               ImVec2(c.value - 8.f * s, cell_bottom));
    const ImRect value_bb(ImVec2(c.value - 6.f * s, cell_top),
                          ImVec2(c.edit - 19.f * s, cell_bottom));
    const ImRect remove_bb(ImVec2(c.remove - 13.f * s, pos.y + full_h * 0.5f - 13.f * s),
                           ImVec2(c.remove + 13.f * s, pos.y + full_h * 0.5f + 13.f * s));
    const auto cell_hovered = [&](const ImRect& bb) {
        return hovered && ImGui::IsMouseHoveringRect(bb.Min, bb.Max, true);
    };
    const bool key_hovered = cell_hovered(key_bb);
    const bool mode_hovered = cell_hovered(mode_bb);
    const bool visibility_hovered = cell_hovered(visibility_bb);
    const bool value_hovered = cell_hovered(value_bb);
    const bool remove_hovered = cell_hovered(remove_bb);
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (remove_hovered)
            row.remove = true;
        else if (key_hovered)
            row.cell = hotkey_cell::key;
        else if (mode_hovered)
            row.cell = hotkey_cell::mode;
        else if (visibility_hovered)
            row.cell = hotkey_cell::visibility;
        else if (value_hovered)
            row.cell = hotkey_cell::value;
        else
            row.clicked = true;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * reveal);
    const float selected = g_submenu == entry.id ? 1.f : 0.f;
    const float hot = ImMax(row_anim(ImGui::GetID("##hotkey_bg"), hovered), selected);
    draw->AddRectFilled(pos, pos + ImVec2(width, h),
        ImGui::GetColorU32(gui.frame_inactive.to_vec4(0.38f + hot * 0.34f, false)), 7.f * s);
    if (selected > 0.f)
        draw->AddRect(pos, pos + ImVec2(width, h),
            ImGui::GetColorU32(gui.accent_color.to_vec4(0.65f, false)), 7.f * s, 0, 1.f * s);

    const float fs = gui.scaled_font_size(font);
    const float small_fs = ImMax(10.f * s, fs * 0.78f);
    const float cy = pos.y + h * 0.5f;
    const ImU32 text = ImGui::GetColorU32(gui.text.to_vec4(1.f, false));
    const ImU32 dim = ImGui::GetColorU32(gui.text_disabled.to_vec4(1.f, false));

    // Feature name plus its stable registration path. Clip only this column so a long localized
    // label can never run through the key/mode columns.
    draw->PushClipRect(pos, ImVec2(c.key - 8.f * s, pos.y + h), true);
    draw->AddText(font, fs, ImVec2(c.feature, IM_ROUND(cy - 14.f * s * reveal)), text,
                  gui.tr(target.label.c_str()));
    draw->AddText(font, small_fs, ImVec2(c.feature, IM_ROUND(cy + 3.f * s * reveal)), dim,
                  target.key.c_str());
    draw->PopClipRect();

    const auto draw_cell = [&](const ImRect& bb, const char* id, const char* value,
                               bool cell_hot, bool open, const ImVec4& base, const char* icon) {
        const float t = row_anim(ImGui::GetID(id), cell_hot || open);
        const float animated_h = bb.GetHeight() * reveal;
        const ImRect animated_bb(ImVec2(bb.Min.x, cy - animated_h * 0.5f),
                                 ImVec2(bb.Max.x, cy + animated_h * 0.5f));
        if (t > 0.01f)
            draw->AddRectFilled(animated_bb.Min, animated_bb.Max,
                                gui.frame_inactive.to_im_color(t * 0.55f), 5.f * s);

        const ImVec4 hot_color = open ? gui.accent_color.to_vec4(1.f, false)
                                     : gui.text.to_vec4(1.f, false);
        const ImU32 color = mix_color(base, hot_color, t);
        const float chevron_fs = 9.f * s;
        const ImVec2 chevron_size = font->CalcTextSizeA(chevron_fs, FLT_MAX, 0.f,
                                                        ICON_FA_CHEVRON_DOWN);
        const float chevron_x = bb.Max.x - 5.f * s - chevron_size.x;
        float text_x = bb.Min.x + 6.f * s;
        draw->PushClipRect(ImVec2(bb.Min.x, pos.y),
                           ImVec2(chevron_x - 4.f * s, pos.y + h), true);
        if (icon) {
            draw->AddText(font, 13.f * s,
                          ImVec2(text_x, IM_ROUND(cy - 6.5f * s)), color, icon);
            text_x += 19.f * s;
        }
        draw->AddText(font, fs, ImVec2(text_x, IM_ROUND(cy - TextLineCenterY(font, fs))),
                      color, value);
        draw->PopClipRect();
        draw->AddText(font, chevron_fs,
                      ImVec2(IM_ROUND(chevron_x), IM_ROUND(cy - chevron_size.y * 0.5f)),
                      color, ICON_FA_CHEVRON_DOWN);
    };

    const bool capturing = g_capture_entry == entry.id && g_hotkeys_capture;
    const bool mode_open = ImGui::IsPopupOpen(ImGui::GetID("##hotkey_mode"), ImGuiPopupFlags_None);
    const bool visibility_open = ImGui::IsPopupOpen(ImGui::GetID("##hotkey_visibility"),
                                                     ImGuiPopupFlags_None);
    const bool value_open = ImGui::IsPopupOpen(ImGui::GetID("##hotkey_value"), ImGuiPopupFlags_None);
    const std::string key = capturing ? std::string("...")
        : (entry.key > 0 ? key_name(entry.key) : std::string("-"));
    const std::string value = hotkey_value(target, entry);
    const ImVec4 text_vec = gui.text.to_vec4(1.f, false);
    const ImVec4 dim_vec = gui.text_disabled.to_vec4(1.f, false);
    const char* eye = entry.show_in_overlay ? ICON_FA_EYE : ICON_FA_EYE_SLASH;
    draw_cell(key_bb, "##key_cell", key.c_str(), key_hovered, capturing, text_vec, nullptr);
    draw_cell(mode_bb, "##mode_cell", gui.tr(mode_name(entry.mode)), mode_hovered, mode_open,
              text_vec, nullptr);
    draw_cell(visibility_bb, "##visibility_cell",
              gui.tr(entry.show_in_overlay ? "Visible" : "Hidden"), visibility_hovered,
              visibility_open, text_vec, eye);
    draw_cell(value_bb, "##value_cell", value.c_str(), value_hovered, value_open,
              entry.bool_value || target.type != value_type::boolean ? text_vec : dim_vec, nullptr);

    const ImVec2 edit_size = font->CalcTextSizeA(13.f * s, FLT_MAX, 0.f, ICON_FA_EDIT);
    draw->AddText(font, 13.f * s, ImVec2(IM_ROUND(c.edit - edit_size.x * 0.5f),
        IM_ROUND(cy - edit_size.y * 0.5f)), hovered ? text : dim, ICON_FA_EDIT);
    const ImVec2 remove_size = font->CalcTextSizeA(13.f * s, FLT_MAX, 0.f, ICON_FA_TRASH_ALT);
    const ImU32 remove_color = remove_hovered ? ImGui::GetColorU32(ImVec4(0.93f, 0.33f, 0.36f, 1.f)) : dim;
    draw->AddText(font, 13.f * s, ImVec2(IM_ROUND(c.remove - remove_size.x * 0.5f),
        IM_ROUND(cy - remove_size.y * 0.5f)), remove_color, ICON_FA_TRASH_ALT);

    ImGui::PopStyleVar();
    return row;
}

bool draw_hotkey_mode_editor(bind_entry& entry, bool clicked, float row_y) {
    static const char* options[] = { "Hold", "Toggle", "Always" };
    static const bind_mode modes[] = { bind_mode::hold, bind_mode::toggle, bind_mode::always };
    int index = entry.mode == bind_mode::hold ? 0 : entry.mode == bind_mode::toggle ? 1 : 2;
    if (!gui.popup_option_card("##hotkey_mode", clicked, row_y, options, IM_ARRAYSIZE(options), &index))
        return false;
    entry.mode = modes[ImClamp(index, 0, 2)];
    entry.toggled = false;
    entry.was_down = entry.key > 0 && (GetAsyncKeyState(entry.key) & 0x8000) != 0;
    return true;
}

bool draw_hotkey_visibility_editor(bind_entry& entry, bool clicked, float row_y) {
    static const char* options[] = { "Hidden", "Visible" };
    int index = entry.show_in_overlay ? 1 : 0;
    if (!gui.popup_option_card("##hotkey_visibility", clicked, row_y,
                               options, IM_ARRAYSIZE(options), &index))
        return false;
    entry.show_in_overlay = index != 0;
    return true;
}

bool draw_hotkey_number_editor(bind_target& target, bind_entry& entry, bool clicked, float row_y) {
    const float s = gui.m_scale;
    bool wants_open = false;
    const float anim = gui.popup_animation("##hotkey_value", clicked, wants_open);
    ImGuiWindow* parent = ImGui::GetCurrentWindow();
    const ImVec2 at(parent->Pos.x + parent->Size.x + 8.f * s, row_y - 10.f * s);
    if (!gui.begin_popup_card_at("##hotkey_value", wants_open, anim, at,
                                 ImVec2(0.f, 0.f), 196.f))
        return false;

    bool changed = false;
    ImGui::TextDisabled("%s", gui.tr("New Value"));
    ImGui::Dummy(ImVec2(0.f, 6.f * s));
    ImGui::SetNextItemWidth(gui.row_content_width());
    if (target.type == value_type::integer) {
        const int previous = entry.int_value;
        changed = ImGui::InputInt("##value", &entry.int_value, 1, 10);
        entry.int_value = std::clamp(entry.int_value, target.minimum_int, target.maximum_int);
        changed |= previous != entry.int_value;
    }
    else {
        const float previous = entry.float_value;
        changed = ImGui::InputFloat("##value", &entry.float_value, 0.1f, 1.f, "%.2f");
        if (!std::isfinite(entry.float_value))
            entry.float_value = previous;
        entry.float_value = std::clamp(entry.float_value, target.minimum_float, target.maximum_float);
        changed |= previous != entry.float_value;
    }
    gui.end_popup_card();
    return changed;
}

bool draw_hotkey_multi_editor(bind_target& target, bind_entry& entry, bool clicked, float row_y) {
    const float s = gui.m_scale;
    bool wants_open = false;
    const float anim = gui.popup_animation("##hotkey_value", clicked, wants_open);
    ImGuiWindow* parent = ImGui::GetCurrentWindow();
    const ImVec2 at(parent->Pos.x + parent->Size.x + 8.f * s, row_y - 10.f * s);
    if (!gui.begin_popup_card_at("##hotkey_value", wants_open, anim, at,
                                 ImVec2(0.f, 0.f), 240.f))
        return false;

    const char* items[k_max_multi_items]{};
    for (int i = 0; i < target.multi_count; ++i)
        items[i] = target.multi_items[i].c_str();
    const bool changed = gui.row_multibox("Value", items, entry.multi_value.data(), target.multi_count,
                                          "Select the values applied while this bind is active.");
    gui.end_popup_card();
    return changed;
}

bool draw_hotkey_value_editor(bind_target& target, bind_entry& entry, bool clicked, float row_y) {
    if (target.type == value_type::multi)
        return draw_hotkey_multi_editor(target, entry, clicked, row_y);
    if (target.type != value_type::boolean)
        return draw_hotkey_number_editor(target, entry, clicked, row_y);
    static const char* options[] = { "Off", "On" };
    int index = entry.bool_value ? 1 : 0;
    if (!gui.popup_option_card("##hotkey_value", clicked, row_y,
                               options, IM_ARRAYSIZE(options), &index))
        return false;
    entry.bool_value = index != 0;
    return true;
}

void draw_hotkeys_editor() {
    if (!g_all_hotkeys_open && g_hotkeys_capture) {
        g_capture_entry = 0;
        g_hotkeys_capture = false;
    }
    if (!gui.window_begin("AllHotkeysEditor", &g_all_hotkeys_open, ImVec2(780.f, 520.f)))
        return;
    ImGuiWindow* manager = ImGui::GetCurrentWindow();
    const float s = gui.m_scale;
    const float content_x = ImGui::GetCursorPosX();
    const float content_width = ImGui::GetContentRegionAvail().x;

    ImFontAtlas* fonts = ImGui::GetIO().Fonts;
    ImFont* bold = fonts->Fonts.Size > 4 ? fonts->Fonts[4] : ImGui::GetFont();
    ImGui::PushFont(bold);
    const char* title = gui.tr("All Hotkeys");
    const float title_width = ImGui::CalcTextSize(title).x;
    ImGui::SetCursorPosX(ImMax(16.f * s, (ImGui::GetWindowWidth() - title_width) * 0.5f));
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.f, 4.f * s));

    static char search[96]{};
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.f * s, 7.f * s));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, gui.field_bg.to_vec4(0.9f, false));
    // Centering the title changes CursorPos.x. Restore the original content line explicitly so
    // the search has equal side insets and its right rounded corner is not clipped by the window.
    ImGui::SetCursorPosX(content_x);
    ImGui::SetNextItemWidth(content_width);
    ImGui::BeginDisabled(g_capture_entry != 0);
    ImGui::InputTextWithHint("##all_hotkeys_search", gui.tr("Search functions..."),
                             search, IM_ARRAYSIZE(search));
    ImGui::EndDisabled();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    if (gui.popup_toggle(ICON_FA_LIST, "Show Overlay", &g_overlay_enabled))
        mark_dirty();
    draw_hotkey_columns_header();

    std::vector<bind_target*> targets;
    for (auto& [id, target] : g_targets) {
        IM_UNUSED(id);
        if (!target.entries.empty())
            targets.push_back(&target);
    }
    std::sort(targets.begin(), targets.end(), [](const bind_target* left, const bind_target* right) {
        if (left->label != right->label)
            return left->label < right->label;
        return left->key < right->key;
    });

    const std::string needle = lower_copy(search);
    target_id remove_target = 0;
    std::uint64_t remove_entry = 0;
    bool any = false, bail = false;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.f * s, 0.f));
    if (ImGui::BeginChild("##all_hotkeys_rows", ImVec2(0.f, 0.f), false)) {
        for (bind_target* target : targets) {
            if (bail)
                break;
            for (int i = 0; i < static_cast<int>(target->entries.size()); ++i) {
                bind_entry& entry = target->entries[i];
                if (!hotkey_matches(*target, entry, needle))
                    continue;
                any = true;
                ImGui::PushID(static_cast<int>(entry.id));
                const hotkey_row row = draw_hotkey_row(*target, entry);
                if (row.cell == hotkey_cell::key) {
                    if (g_capture_entry != entry.id || !g_hotkeys_capture)
                        begin_capture(entry);
                    g_hotkeys_capture = true;
                    request_submenu(0);
                }
                else if (row.cell != hotkey_cell::none) {
                    request_submenu(0); // an inline cell replaces, rather than stacks with, the full card
                }
                if (row.clicked)
                    request_submenu(entry.id);

                if (draw_hotkey_mode_editor(entry, row.cell == hotkey_cell::mode, row.pos.y))
                    mark_dirty();
                if (draw_hotkey_visibility_editor(entry, row.cell == hotkey_cell::visibility, row.pos.y))
                    mark_dirty();
                if (draw_hotkey_value_editor(*target, entry, row.cell == hotkey_cell::value, row.pos.y))
                    mark_dirty();
                if (g_hotkeys_capture && g_capture_entry == entry.id)
                    update_capture(entry); // key capture lives in the row, even with no card open

                bool removed = false, ignored = false;
                const bool alive = draw_entry_card(*target, entry, false, manager,
                                                   row.pos.y, removed, ignored);
                if (alive && g_capture_entry == entry.id)
                    g_hotkeys_capture = true;
                else if (g_hotkeys_capture && g_capture_entry == 0)
                    g_hotkeys_capture = false;
                ImGui::PopID();
                if (row.remove || removed) {
                    remove_target = target->id;
                    remove_entry = entry.id;
                }
                if (!alive || row.remove || removed) {
                    bail = true; // deletion or duplicate: finish with fresh references next frame
                    break;
                }
            }
        }
        if (!any) {
            ImGui::Dummy(ImVec2(0.f, 12.f * s));
            ImGui::TextDisabled("%s", gui.tr(search[0] ? "No matching functions." : "No hotkeys yet."));
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    gui.window_end();

    if (remove_entry != 0) {
        auto target_it = g_targets.find(remove_target);
        if (target_it != g_targets.end()) {
            bind_target& target = target_it->second;
            auto entry_it = std::find_if(target.entries.begin(), target.entries.end(),
                [remove_entry](const bind_entry& entry) { return entry.id == remove_entry; });
            if (entry_it != target.entries.end()) {
                if (g_capture_entry == remove_entry) {
                    g_capture_entry = 0;
                    g_hotkeys_capture = false;
                }
                g_entry_reveal.erase(remove_entry);
                target.entries.erase(entry_it);
                request_submenu(0);
                mark_dirty();
            }
        }
    }
    if (!g_all_hotkeys_open)
        request_submenu(0);
}

bind_target& register_stable_target(const char* stable_key, const char* label,
                                  value_type type, void* pointer) {
    load_once();
    const std::string key = stable_key ? stable_key : "item";
    const target_id id = hash_key(key);
    auto [it, inserted] = g_targets.try_emplace(id);
    bind_target& target = it->second;
    if (inserted) {
        target.id = id;
        target.key = key;
        target.type = type;
    }
    target.label = label ? label : key;
    target.type = type;
    target.pointer = pointer;
    g_pointer_targets[pointer] = id;
    if (!target.pointer_seen) {
        target.pointer_seen = true;
        if (type == value_type::boolean) target.base_bool = *static_cast<bool*>(pointer);
        else if (type == value_type::integer) target.base_int = *static_cast<int*>(pointer);
        else target.base_float = *static_cast<float*>(pointer);
    }
    return target;
}

bind_target* find_registered_target(void* pointer) {
    const auto pointer_it = g_pointer_targets.find(pointer);
    if (pointer_it == g_pointer_targets.end())
        return nullptr;
    const auto target_it = g_targets.find(pointer_it->second);
    return target_it == g_targets.end() ? nullptr : &target_it->second;
}

bind_target& register_automatic_target(ImGuiWindow* window, ImGuiID item_id, const char* label,
                                       value_type type, void* pointer) {
    // Explicit registrations keep their readable Lua keys. Every other main-menu checkbox/slider
    // receives a stable ImGui-window/item key, so the bind menu is no longer limited to a hand-maintained list.
    const std::string key = "auto." + std::to_string(window->ID) + "." + std::to_string(item_id);
    const char* begin = label ? label : "Control";
    const char* end = ImGui::FindRenderedTextEnd(begin);
    const std::string visible_label(begin, end);
    return register_stable_target(key.c_str(), visible_label.c_str(), type, pointer);
}

bind_target* find_registered_multibox(void* pointer) {
    const auto pointer_it = g_multi_pointer_targets.find(pointer);
    if (pointer_it == g_multi_pointer_targets.end())
        return nullptr;
    const auto target_it = g_targets.find(pointer_it->second);
    return target_it == g_targets.end() ? nullptr : &target_it->second;
}

bind_target& register_multibox_target(const char* stable_key, const char* label, bool* values,
                                      const char* const items[], int count) {
    load_once();
    const std::string key = stable_key ? stable_key : "multibox";
    const target_id id = hash_key(key);
    auto [it, inserted] = g_targets.try_emplace(id);
    bind_target& target = it->second;
    if (inserted) {
        target.id = id;
        target.key = key;
        target.type = value_type::multi;
    }
    const char* begin = label ? label : "Multibox";
    const char* end = ImGui::FindRenderedTextEnd(begin);
    target.label.assign(begin, end);
    target.type = value_type::multi;
    target.pointer = values;
    target.multi_count = count;
    target.multi_items.clear();
    for (int i = 0; i < count; ++i)
        target.multi_items.emplace_back(items[i] ? items[i] : "*Unknown item*");
    g_multi_pointer_targets[values] = id;
    if (!target.pointer_seen) {
        for (int i = 0; i < count; ++i)
            target.base_multi[i] = values[i];
        target.pointer_seen = true;
    }
    return target;
}

bind_target& register_automatic_multibox(ImGuiWindow* window, ImGuiID item_id, const char* label,
                                         bool* values, const char* const items[], int count) {
    const std::string key = "auto." + std::to_string(window->ID) + "." + std::to_string(item_id);
    return register_multibox_target(key.c_str(), label, values, items, count);
}

void write_base(bind_target& target) {
    if (!target.pointer)
        return;
    if (target.type == value_type::boolean)
        *static_cast<bool*>(target.pointer) = target.base_bool;
    else if (target.type == value_type::integer)
        *static_cast<int*>(target.pointer) = target.base_int;
    else if (target.type == value_type::floating)
        *static_cast<float*>(target.pointer) = target.base_float;
    else {
        bool* values = static_cast<bool*>(target.pointer);
        for (int i = 0; i < target.multi_count; ++i)
            values[i] = target.base_multi[i];
    }
}

void write_entry(bind_target& target, const bind_entry& entry) {
    if (!target.pointer)
        return;
    if (target.type == value_type::boolean)
        *static_cast<bool*>(target.pointer) = entry.bool_value;
    else if (target.type == value_type::integer)
        *static_cast<int*>(target.pointer) = std::clamp(entry.int_value, target.minimum_int, target.maximum_int);
    else if (target.type == value_type::floating)
        *static_cast<float*>(target.pointer) = std::clamp(entry.float_value, target.minimum_float, target.maximum_float);
    else {
        bool* values = static_cast<bool*>(target.pointer);
        for (int i = 0; i < target.multi_count; ++i)
            values[i] = entry.multi_value[i];
    }
}
}

void draw_hotkeys_window() {
    draw_hotkeys_editor();
}

void register_bool(const char* key, const char* label, bool* value) {
    if (key && value)
        register_stable_target(key, label, value_type::boolean, value);
}

void register_int(const char* key, const char* label, int* value, int minimum, int maximum) {
    if (!key || !value)
        return;
    bind_target& target = register_stable_target(key, label, value_type::integer, value);
    target.minimum_int = minimum;
    target.maximum_int = maximum;
    for (auto& entry : target.entries)
        entry.int_value = std::clamp(entry.int_value, minimum, maximum);
}

void register_float(const char* key, const char* label, float* value, float minimum, float maximum) {
    if (!key || !value)
        return;
    bind_target& target = register_stable_target(key, label, value_type::floating, value);
    target.minimum_float = minimum;
    target.maximum_float = maximum;
    for (auto& entry : target.entries)
        entry.float_value = std::clamp(entry.float_value, minimum, maximum);
}

void register_multibox(const char* key, const char* label, bool* values,
                       const char* const items[], int count) {
    if (!key || !values || !items || count <= 0 || count > k_max_multi_items)
        return;
    register_multibox_target(key, label, values, items, count);
}

target_id checkbox_item(ImGuiWindow* window, ImGuiID item_id, const char* label,
                        const ImRect& rect, const ImRect& control, bool* value) {
    if (!value || !belongs_to_main_menu(window) ||
        (GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled))
        return 0;
    bind_target* target = find_registered_target(value);
    if (!target)
        target = &register_automatic_target(window, item_id, label, value_type::boolean, value);
    if (target->type != value_type::boolean)
        return 0;
    draw_bind_menu(*target, rect, control);
    return target->id;
}

target_id slider_item(ImGuiWindow* window, ImGuiID item_id, const char* label,
                      const ImRect& rect, const ImRect& control, ImGuiDataType type, void* value,
                      const void* minimum, const void* maximum) {
    if (!value || !minimum || !maximum || !belongs_to_main_menu(window) ||
        (GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled))
        return 0;
    if (type != ImGuiDataType_S32 && type != ImGuiDataType_Float)
        return 0;

    const value_type target_type = type == ImGuiDataType_S32 ? value_type::integer : value_type::floating;
    bind_target* target = find_registered_target(value);
    if (!target)
        target = &register_automatic_target(window, item_id, label, target_type, value);
    if (target->type != target_type)
        return 0;
    if (target_type == value_type::integer) {
        target->minimum_int = *static_cast<const int*>(minimum);
        target->maximum_int = *static_cast<const int*>(maximum);
        for (auto& entry : target->entries)
            entry.int_value = std::clamp(entry.int_value, target->minimum_int, target->maximum_int);
    } else {
        target->minimum_float = *static_cast<const float*>(minimum);
        target->maximum_float = *static_cast<const float*>(maximum);
        for (auto& entry : target->entries)
            entry.float_value = std::clamp(entry.float_value, target->minimum_float, target->maximum_float);
    }
    draw_bind_menu(*target, rect, control);
    return target->id;
}

target_id multibox_item(ImGuiWindow* window, ImGuiID item_id, const char* label,
                        const ImRect& rect, const ImRect& control, bool* values,
                        const char* const items[], int count) {
    if (!values || !items || count <= 0 || count > k_max_multi_items ||
        !belongs_to_main_menu(window) || (GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled))
        return 0;
    bind_target* target = find_registered_multibox(values);
    if (!target)
        target = &register_automatic_multibox(window, item_id, label, values, items, count);
    if (target->type != value_type::multi)
        return 0;
    draw_bind_menu(*target, rect, control);
    return target->id;
}

void sync_bool(target_id id, bool value) {
    if (!id) return;
    auto it = g_targets.find(id);
    if (it != g_targets.end()) it->second.base_bool = value;
}

void sync_int(target_id id, int value) {
    if (!id) return;
    auto it = g_targets.find(id);
    if (it != g_targets.end()) it->second.base_int = value;
}

void sync_float(target_id id, float value) {
    if (!id || !std::isfinite(value)) return;
    auto it = g_targets.find(id);
    if (it != g_targets.end()) it->second.base_float = value;
}

void sync_multibox(target_id id, const bool* values, int count) {
    if (!id || !values)
        return;
    auto it = g_targets.find(id);
    if (it == g_targets.end() || it->second.type != value_type::multi)
        return;
    bind_target& target = it->second;
    const int shared = (std::min)(count, target.multi_count);
    for (int i = 0; i < shared; ++i)
        target.base_multi[i] = values[i];
}

void update() {
    load_once();
    for (auto& [id, target] : g_targets) {
        if (!target.pointer_seen || !target.pointer)
            continue;

        if (!target.was_overridden) {
            if (target.type == value_type::boolean)
                target.base_bool = *static_cast<bool*>(target.pointer);
            else if (target.type == value_type::integer)
                target.base_int = *static_cast<int*>(target.pointer);
            else if (target.type == value_type::floating) {
                if (std::isfinite(*static_cast<float*>(target.pointer)))
                    target.base_float = *static_cast<float*>(target.pointer);
            }
            else {
                const bool* values = static_cast<const bool*>(target.pointer);
                for (int i = 0; i < target.multi_count; ++i)
                    target.base_multi[i] = values[i];
            }
        }

        bind_entry* winner = nullptr;
        for (auto& entry : target.entries) {
            const bool down = entry.key > 0 && (GetAsyncKeyState(entry.key) & 0x8000) != 0;
            if (g_capture_entry != 0) {
                entry.was_down = down;
                if (entry.active)
                    winner = &entry;
                continue;
            }
            if (entry.mode == bind_mode::always) {
                entry.active = true;
            } else if (entry.mode == bind_mode::hold) {
                entry.active = entry.key > 0 && down;
            } else {
                if (entry.key > 0 && down && !entry.was_down)
                    entry.toggled = !entry.toggled;
                entry.active = entry.key > 0 && entry.toggled;
            }
            entry.was_down = down;
            if (entry.active)
                winner = &entry;
        }

        if (winner)
            write_entry(target, *winner);
        else if (target.was_overridden)
            write_base(target);
        target.was_overridden = winner != nullptr;
    }
    save_if_dirty();
}

void flush() {
    save_if_dirty();
}

void before_config_save() {
    for (auto& [id, target] : g_targets)
        if (target.was_overridden)
            write_base(target);
}

void after_config_save() {
    for (auto& [id, target] : g_targets) {
        const bind_entry* winner = nullptr;
        for (const auto& entry : target.entries)
            if (entry.active)
                winner = &entry;
        if (winner)
            write_entry(target, *winner);
    }
}

void on_config_loaded() {
    for (auto& [id, target] : g_targets) {
        if (!target.pointer_seen || !target.pointer)
            continue;
        target.was_overridden = false;
        if (target.type == value_type::boolean)
            target.base_bool = *static_cast<bool*>(target.pointer);
        else if (target.type == value_type::integer)
            target.base_int = *static_cast<int*>(target.pointer);
        else if (target.type == value_type::floating) {
            if (std::isfinite(*static_cast<float*>(target.pointer)))
                target.base_float = *static_cast<float*>(target.pointer);
        }
        else {
            const bool* values = static_cast<const bool*>(target.pointer);
            for (int i = 0; i < target.multi_count; ++i)
                target.base_multi[i] = values[i];
        }

        const bind_entry* winner = nullptr;
        for (const auto& entry : target.entries)
            if (entry.active)
                winner = &entry;
        if (winner) {
            write_entry(target, *winner);
            target.was_overridden = true;
        }
    }
}

bool is_capturing() {
    return g_capture_entry != 0;
}

bool overlay_enabled() {
    return g_overlay_enabled;
}

void set_overlay_enabled(bool enabled) {
    if (g_overlay_enabled == enabled)
        return;
    g_overlay_enabled = enabled;
    mark_dirty();
}

void draw_overlay(bool menu_open) {
    if (!menu_open)
        g_all_hotkeys_open = false;
    if (!g_overlay_enabled)
        return;

    struct active_row { std::string label; std::string binding; };
    std::vector<active_row> rows;
    for (const auto& [id, target] : g_targets) {
        const bind_entry* winner = nullptr;
        for (const auto& entry : target.entries)
            if (entry.active && entry.show_in_overlay)
                winner = &entry;
        if (!winner)
            continue;
        std::string binding = gui.tr(mode_name(winner->mode));
        if (winner->mode != bind_mode::always)
            binding += " · " + key_name(winner->key);
        rows.push_back({ gui.tr(target.label.c_str()), std::move(binding) });
    }
    std::sort(rows.begin(), rows.end(), [](const active_row& left, const active_row& right) {
        return left.label < right.label;
    });
    if (rows.empty())
        return;

    const float s = gui.m_scale;
    ImGui::SetNextWindowPos(ImVec2(20.f * s, 180.f * s), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(230.f * s, 0.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.f * s, 10.f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f * s);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, gui.panel_body.to_vec4(0.94f, false));
    ImGui::PushStyleColor(ImGuiCol_Border, gui.popup_border.to_vec4(0.9f, false));

    ImGuiWindowFlags overlay_flags = ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (!menu_open)
        overlay_flags |= ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav;

    if (ImGui::Begin("##pastaware_keybinds", nullptr, overlay_flags)) {
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts.Size > 4 ? ImGui::GetIO().Fonts->Fonts[4] : ImGui::GetFont());
        ImGui::TextUnformatted(gui.tr("KEYBINDS"));
        ImGui::PopFont();
        ImGui::Separator();
        for (const auto& row : rows) {
            ImGui::TextUnformatted(row.label.c_str());
            const float width = ImGui::CalcTextSize(row.binding.c_str()).x;
            ImGui::SameLine((std::max)(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - width - 12.f * s));
            ImGui::TextColored(gui.accent_color.to_vec4(), "%s", row.binding.c_str());
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}
}
