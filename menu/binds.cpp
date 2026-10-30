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
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>
#include <unordered_map>
#include <vector>

namespace menu_binds {
namespace {
enum class value_type { boolean, integer, floating };
enum class bind_mode { always = 0, hold = 1, toggle = 2 };

struct bind_entry {
    std::uint64_t id = 0;
    int key = 0;
    bind_mode mode = bind_mode::toggle;
    bool bool_value = true;
    int int_value = 0;
    float float_value = 0.f;
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
    int minimum_int = 0;
    int maximum_int = 100;
    float minimum_float = 0.f;
    float maximum_float = 1.f;
    bool was_overridden = false;
    std::vector<bind_entry> entries;
};

std::unordered_map<target_id, bind_target> g_targets;
std::unordered_map<void*, target_id> g_pointer_targets;
std::uint64_t g_next_entry_id = 1;
bool g_loaded = false;
bool g_dirty = false;
std::uint64_t g_capture_entry = 0;
std::array<bool, 0xA6> g_capture_down{};
constexpr const char* k_bind_file = "c:\\airflow\\binds.json";

// --- Bind menu state ------------------------------------------------------------------------
// The menu is a root card (Copy Lua Path / one row per bind / New Bind / Hotkeys / Reset) whose
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
bool g_overlay_enabled = true;     // "Hotkeys": draw the on-screen keybind list at all

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
    return main && (window == main || window->RootWindow == main);
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
        target.type = saved_type == 1 ? value_type::integer : saved_type == 2 ? value_type::floating : value_type::boolean;

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
        saved_target["type"] = target.type == value_type::boolean ? 0 : target.type == value_type::integer ? 1 : 2;
        saved_target["entries"] = nlohmann::json::array();
        for (const auto& entry : target.entries) {
            saved_target["entries"].push_back({
                {"id", entry.id}, {"key", entry.key}, {"mode", static_cast<int>(entry.mode)},
                {"bool_value", entry.bool_value}, {"int_value", entry.int_value},
                {"float_value", entry.float_value}, {"show_in_overlay", entry.show_in_overlay}
            });
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
//   Copy Lua Path                         ##bind_menu  (root card)
//   ----------
//   Toggle "N"                >           -> ##bind_sub_<id>    Key / Mode / Value + actions
//   Hold "D"                  >
//   New Bind                  >           -> ##bind_sub_draft   (the same card, pending bind)
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
                       bool lit, const ImVec4& tint, bool dim = false) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImFont* font = ImGui::GetFont();
    const float s = gui.m_scale;
    const float w = ImGui::GetContentRegionAvail().x, h = 30.f * s;
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton(id, ImVec2(w, h));
    // A child card is a focused popup sitting above this one, and ImGui refuses every hover test
    // on items of a popup something is open on top of - which is exactly the gesture that has to
    // keep working here. Parent and child never overlap, so test the rect directly and take the
    // click the same way instead of relying on this item's own hover state.
    menu_row row{ pos, w, h, ImGui::IsMouseHoveringRect(pos, pos + ImVec2(w, h), true), false };
    row.clicked = row.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);

    const float t = row_anim(ImGui::GetID(id), row.hovered || lit);
    if (t > 0.01f)
        draw->AddRectFilled(pos, pos + ImVec2(w, h), gui.frame_inactive.to_im_color(t * 0.7f), 6.f * s);

    const ImVec4 base = dim ? gui.text_disabled.to_vec4(1.f, false)
                            : ImVec4(tint.x, tint.y, tint.z, tint.w * 0.82f);
    const ImU32 fg = mix_color(base, tint, t);
    const float fs = font->FontSize, icon_fs = 13.f * s, cy = pos.y + h * 0.5f;

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
    return row;
}

void bind_menu_separator() {
    const float s = gui.m_scale, w = ImGui::GetContentRegionAvail().x;
    const ImVec2 p = ImGui::GetCursorScreenPos() + ImVec2(0.f, 4.f * s);
    ImGui::GetWindowDrawList()->AddLine(p, p + ImVec2(w, 0.f), gui.popup_border.to_im_color(0.55f), 1.f * s);
    ImGui::Dummy(ImVec2(w, 9.f * s));
}

// "Key  [ N ]" - clicking the box arms begin_capture(); the next key pressed lands in `entry`.
bool bind_key_row(bind_entry& entry, bool is_draft = false) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImFont* font = ImGui::GetFont();
    const float s = gui.m_scale;
    const float w = ImGui::GetContentRegionAvail().x, h = 30.f * s;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##key", ImVec2(w, h));

    const float fs = font->FontSize, cy = pos.y + h * 0.5f;
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
    const float w = ImGui::GetContentRegionAvail().x, h = 30.f * s;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##mode", ImVec2(w, h));

    const float fs = font->FontSize, cy = pos.y + h * 0.5f;
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
        if (t > 0.01f)
            draw->AddRectFilled(bb.Min, bb.Max, gui.accent_color.to_im_color(t, false), 5.f * s);
        else if (hovered)
            draw->AddRectFilled(bb.Min, bb.Max, gui.frame_inactive.to_im_color(0.7f), 5.f * s);
        const char* shown = gui.tr(names[i]);
        const ImVec2 tsz = font->CalcTextSizeA(fs, FLT_MAX, 0.f, shown);
        draw->AddText(font, fs, ImVec2(IM_ROUND(bb.GetCenter().x - tsz.x * 0.5f),
                                       IM_ROUND(bb.GetCenter().y - TextLineCenterY(font, fs))),
                      mix_color(gui.text_disabled.to_vec4(1.f, false), gui.text.to_vec4(1.f, false),
                                ImMax(t, hovered ? 0.6f : 0.f)), shown);
    }
    return changed;
}

// "Value [switch]" for a bool target. Ints and floats fall through to the real slider widgets,
// which already render in the compact in-card style.
bool bind_value_row(const bind_target& target, bind_entry& entry) {
    if (target.type == value_type::integer)
        return ImGui::SliderInt("Value", &entry.int_value, target.minimum_int, target.maximum_int);
    if (target.type == value_type::floating)
        return ImGui::SliderFloat("Value", &entry.float_value, target.minimum_float,
                                  target.maximum_float, "%.2f");

    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImFont* font = ImGui::GetFont();
    const float s = gui.m_scale;
    const float w = ImGui::GetContentRegionAvail().x, h = 30.f * s;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##value", ImVec2(w, h));

    const float fs = font->FontSize, cy = pos.y + h * 0.5f;
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
    const float w = ImGui::GetContentRegionAvail().x, h = 28.f * s;
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
    if (is_draft)
        ImFormatString(name, IM_ARRAYSIZE(name), "##bind_sub_draft");
    else
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
        bind_entry copy = entry;
        copy.id = g_next_entry_id++;
        copy.toggled = copy.was_down = copy.active = false;
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

        if (bind_menu_row("##lua", ICON_FA_LINK, "Copy Lua Path", false, false, c_text).clicked)
            ImGui::SetClipboardText(target.key.c_str());
        bind_menu_separator();

        int remove_index = -1;
        bool commit_draft = false, bail = false;
        for (int i = 0; i < static_cast<int>(target.entries.size()); ++i) {
            bind_entry& entry = target.entries[i];
            ImGui::PushID(static_cast<int>(entry.id));
            const std::string label = entry_label(entry);
            const menu_row row = bind_menu_row("##bind", ICON_FA_BOLT, label.c_str(), true,
                                               g_submenu == entry.id, c_text);
            if (row.hovered)
                request_submenu(entry.id);
            bool removed = false, ignored = false;
            const bool alive = draw_entry_card(target, entry, false, card, row.pos.y, removed, ignored);
            ImGui::PopID();
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

            if (bind_menu_row("##hotkeys", ICON_FA_LIST, "Hotkeys", false, g_overlay_enabled, c_text).clicked) {
                g_overlay_enabled = !g_overlay_enabled;
                mark_dirty();
            }
            bind_menu_separator();
            if (bind_menu_row("##reset", ICON_FA_TRASH_ALT, "Reset", false, false, c_red).clicked &&
                has_binds) {
                for (const bind_entry& entry : target.entries)
                    if (g_capture_entry == entry.id)
                        g_capture_entry = 0;
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
            target.entries.push_back(g_draft);
            request_submenu(g_draft.id);
            g_draft = bind_entry{};
            g_draft_target = 0;
            mark_dirty();
        }
        else if (remove_index >= 0) {
            if (g_capture_entry == target.entries[remove_index].id)
                g_capture_entry = 0;
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

void write_base(bind_target& target) {
    if (!target.pointer)
        return;
    if (target.type == value_type::boolean) *static_cast<bool*>(target.pointer) = target.base_bool;
    else if (target.type == value_type::integer) *static_cast<int*>(target.pointer) = target.base_int;
    else *static_cast<float*>(target.pointer) = target.base_float;
}

void write_entry(bind_target& target, const bind_entry& entry) {
    if (!target.pointer)
        return;
    if (target.type == value_type::boolean) *static_cast<bool*>(target.pointer) = entry.bool_value;
    else if (target.type == value_type::integer) *static_cast<int*>(target.pointer) = std::clamp(entry.int_value, target.minimum_int, target.maximum_int);
    else *static_cast<float*>(target.pointer) = std::clamp(entry.float_value, target.minimum_float, target.maximum_float);
}
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

target_id checkbox_item(ImGuiWindow* window, ImGuiID item_id, const char* label,
                        const ImRect& rect, const ImRect& control, bool* value) {
    IM_UNUSED(label);
    IM_UNUSED(item_id);
    if (!value || !belongs_to_main_menu(window) ||
        (GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled))
        return 0;
    bind_target* target = find_registered_target(value);
    if (!target || target->type != value_type::boolean)
        return 0;
    draw_bind_menu(*target, rect, control);
    return target->id;
}

target_id slider_item(ImGuiWindow* window, ImGuiID item_id, const char* label,
                      const ImRect& rect, const ImRect& control, ImGuiDataType type, void* value,
                      const void* minimum, const void* maximum) {
    IM_UNUSED(label);
    IM_UNUSED(item_id);
    if (!value || !minimum || !maximum || !belongs_to_main_menu(window) ||
        (GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled))
        return 0;
    if (type != ImGuiDataType_S32 && type != ImGuiDataType_Float)
        return 0;

    const value_type target_type = type == ImGuiDataType_S32 ? value_type::integer : value_type::floating;
    bind_target* target = find_registered_target(value);
    if (!target || target->type != target_type)
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

void update() {
    load_once();
    for (auto& [id, target] : g_targets) {
        if (!target.pointer_seen || !target.pointer)
            continue;

        if (!target.was_overridden) {
            if (target.type == value_type::boolean) target.base_bool = *static_cast<bool*>(target.pointer);
            else if (target.type == value_type::integer) target.base_int = *static_cast<int*>(target.pointer);
            else if (std::isfinite(*static_cast<float*>(target.pointer))) target.base_float = *static_cast<float*>(target.pointer);
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
        if (target.type == value_type::boolean) target.base_bool = *static_cast<bool*>(target.pointer);
        else if (target.type == value_type::integer) target.base_int = *static_cast<int*>(target.pointer);
        else if (std::isfinite(*static_cast<float*>(target.pointer))) target.base_float = *static_cast<float*>(target.pointer);

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

    ImGui::SetNextWindowPos(ImVec2(20.f, 180.f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(230.f * gui.m_scale, 0.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.f, 10.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
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
            ImGui::SameLine((std::max)(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - width - 12.f));
            ImGui::TextColored(gui.accent_color.to_vec4(), "%s", row.binding.c_str());
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}
}
