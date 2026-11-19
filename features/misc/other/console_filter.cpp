#include "other.hpp"
#include "../../../sdk/interfaces/interfaces.hpp"
#include "../../../utils/detourhook.hpp"
#include "../../../utils/log.hpp"
#include "../../../utils/memory.hpp"
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <windows.h>

namespace Misc::Other::ConsoleFilter {
namespace {
// tier0.dll's print functions are C++ symbols, so most of them are exported under their
// decorated names only (`?ConMsg@@YAXPBDZZ`); a couple are plain `extern "C"` exports.
// Both spellings are tried, newest first, and the one that resolved is logged.
struct Export {
    const char* symbol; // name passed to GetProcAddress
    const char* label;  // how it appears in the log
};

constexpr Export kConMsg[] = {
    {"?ConMsg@@YAXPBDZZ", "ConMsg (decorated)"},
    {"ConMsg", "ConMsg"},
};
constexpr Export kConColorMsg[] = {
    {"?ConColorMsg@@YAXABVColor@@PBDZZ", "ConColorMsg (decorated)"},
    {"?ConColorMsg@@YAXABVTColor@@PBDZZ", "ConColorMsg (decorated, TColor)"},
    {"ConColorMsg", "ConColorMsg"},
};
constexpr Export kMsg[] = {
    {"?Msg@@YAXPBDZZ", "Msg (decorated)"},
    {"Msg", "Msg"},
};

using PrintFn = void(__cdecl*)(const char*, ...);
using ColorPrintFn = void(__cdecl*)(const void*, const char*, ...);

Detour_Hook g_con_msg_hook;
Detour_Hook g_con_color_msg_hook;
Detour_Hook g_msg_hook;
void* g_con_msg_original{};
void* g_con_color_msg_original{};
void* g_msg_original{};
std::atomic<bool> g_enabled{false};
std::atomic<bool> g_want{false};
bool g_attach_tried{};
bool g_reported{};

// Formats the line once and forwards it to the original as "%s", so the game's own
// output path (and its colour handling) stays intact.
bool ShouldDrop(const char* format, va_list args) {
    char line[1024]{};
    _vsnprintf_s(line, sizeof(line), _TRUNCATE, format, args);
    if (!ShouldFilter(line)) return false;
    if (!g_reported) {
        g_reported = true;
        LOG_INFO("other: console filter dropped \"%s\" (and following matches)", line);
    }
    return true;
}

void __cdecl ConMsg(const char* format, ...) {
    auto original = reinterpret_cast<PrintFn>(g_con_msg_original);
    if (!original) return;
    if (!g_enabled.load(std::memory_order_relaxed) || !format) {
        if (format) original("%s", format);
        return;
    }
    va_list args;
    va_start(args, format);
    const bool drop = ShouldDrop(format, args);
    va_end(args);
    if (!drop) original("%s", format);
}

void __cdecl ConColorMsg(const void* color, const char* format, ...) {
    auto original = reinterpret_cast<ColorPrintFn>(g_con_color_msg_original);
    if (!original) return;
    if (!g_enabled.load(std::memory_order_relaxed) || !format) {
        if (format) original(color, "%s", format);
        return;
    }
    va_list args;
    va_start(args, format);
    const bool drop = ShouldDrop(format, args);
    va_end(args);
    // The colour is forwarded unchanged: the filter must not repaint what it keeps.
    if (!drop) original(color, "%s", format);
}

void __cdecl Msg(const char* format, ...) {
    auto original = reinterpret_cast<PrintFn>(g_msg_original);
    if (!original) return;
    if (!g_enabled.load(std::memory_order_relaxed) || !format) {
        if (format) original("%s", format);
        return;
    }
    va_list args;
    va_start(args, format);
    const bool drop = ShouldDrop(format, args);
    va_end(args);
    if (!drop) original("%s", format);
}

// Resolves one export out of its candidate spellings and installs a detour on it.
bool HookOne(const Export* candidates, std::size_t count, void* module, void* detour,
             Detour_Hook& hook, void** original, const char* what) {
    for (std::size_t i = 0; i < count; ++i) {
        void* address = MEM::GetExport(module, candidates[i].symbol);
        if (!address) continue;
        if (!hook.attach(address, detour, original))
            LOG_WARN("other: console filter found %s but could not hook it", candidates[i].label);
        else
            LOG_INFO("other: console filter hooked tier0!%s", candidates[i].label);
        return true;
    }
    LOG_WARN("other: console filter - tier0.dll does not export %s; that path stays unfiltered", what);
    return false;
}
} // namespace

void SetEnabled(bool enabled) {
    // Published from the render thread, patched from the game thread (see FrameStage): a
    // MinHook installation writes executable code, which must not happen while the game
    // thread may be inside the function being hooked.
    g_enabled.store(enabled, std::memory_order_relaxed);
    g_want.store(enabled, std::memory_order_relaxed);
}

void FrameStage(int /*stage*/) {
    if (!g_want.load(std::memory_order_relaxed) || g_attach_tried) return;
    g_attach_tried = true;

    const auto tier0 = MEM::GetModuleBase("tier0.dll");
    if (!tier0) {
        LOG_WARN("other: tier0.dll missing; console filter stays off");
        return;
    }

    std::size_t hooked = 0;
    hooked += HookOne(kConMsg, std::size(kConMsg), tier0, reinterpret_cast<void*>(&ConMsg),
                      g_con_msg_hook, &g_con_msg_original, "ConMsg") ? 1u : 0u;
    hooked += HookOne(kConColorMsg, std::size(kConColorMsg), tier0,
                      reinterpret_cast<void*>(&ConColorMsg), g_con_color_msg_hook,
                      &g_con_color_msg_original, "ConColorMsg") ? 1u : 0u;
    hooked += HookOne(kMsg, std::size(kMsg), tier0, reinterpret_cast<void*>(&Msg), g_msg_hook,
                      &g_msg_original, "Msg") ? 1u : 0u;

    if (!hooked) {
        LOG_WARN("other: console filter could not hook any tier0 print path; feature off");
        g_attach_tried = false;
        return;
    }
    LOG_INFO("other: console filter active on %zu of 3 tier0 print paths", hooked);
}

void Shutdown() {
    g_con_msg_hook.detach();
    g_con_color_msg_hook.detach();
    g_msg_hook.detach();
}
} // namespace Misc::Other::ConsoleFilter
