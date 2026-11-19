#include "other.hpp"
#include "../../../utils/detourhook.hpp"
#include "../../../utils/log.hpp"
#include "../../../utils/memory.hpp"
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <windows.h>

namespace Misc::Other::ConsoleFilter {
namespace {
// tier0.dll exports the console print path as plain C functions, so MinHook can own them
// without a signature scan: no vtable index and no pattern can be stale here.
using PrintFn = void(__cdecl*)(const char* format, ...);

Detour_Hook g_con_msg_hook;
Detour_Hook g_con_color_msg_hook;
void* g_con_msg_original{};
void* g_con_color_msg_original{};
std::atomic<bool> g_enabled{false};
std::atomic<bool> g_want{false};
bool g_attach_tried{};
bool g_reported{};

// Both exports are variadic, so the detour formats the line once and forwards it to the
// original as "%s". That keeps the original's own colour handling intact.
void Forward(void* original_address, const char* format, va_list args) {
    char line[1024]{};
    _vsnprintf_s(line, sizeof(line), _TRUNCATE, format, args);
    if (ShouldFilter(line)) {
        if (!g_reported) {
            g_reported = true;
            LOG_INFO("other: console filter dropped \"%s\" (and following matches)", line);
        }
        return;
    }
    if (auto original = reinterpret_cast<PrintFn>(original_address))
        original("%s", line);
}

void __cdecl ConMsg(const char* format, ...) {
    va_list args;
    va_start(args, format);
    if (g_enabled.load(std::memory_order_relaxed) && format)
        Forward(g_con_msg_original, format, args);
    else if (auto original = reinterpret_cast<PrintFn>(g_con_msg_original))
        original("%s", format ? format : "");
    va_end(args);
}

void __cdecl ConColorMsg(const void* /*color*/, const char* format, ...) {
    // ConColorMsg takes the colour first; the colour is dropped on the forwarding path on
    // purpose, because the filter must not repaint the lines it keeps.
    va_list args;
    va_start(args, format);
    if (g_enabled.load(std::memory_order_relaxed) && format)
        Forward(g_con_color_msg_original, format, args);
    else if (auto original = reinterpret_cast<void(__cdecl*)(const void*, const char*, ...)>(
                 g_con_color_msg_original))
        original(nullptr, "%s", format ? format : "");
    va_end(args);
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
    void* con_msg = MEM::GetExport(tier0, "ConMsg");
    void* con_color_msg = MEM::GetExport(tier0, "ConColorMsg");
    if (!con_msg || !con_color_msg) {
        LOG_WARN("other: ConMsg/ConColorMsg not exported by tier0.dll; console filter stays off");
        return;
    }

    const bool msg_ok = g_con_msg_hook.attach(con_msg, reinterpret_cast<void*>(&ConMsg),
                                              &g_con_msg_original);
    const bool color_ok = g_con_color_msg_hook.attach(
        con_color_msg, reinterpret_cast<void*>(&ConColorMsg), &g_con_color_msg_original);
    if (!msg_ok && !color_ok) {
        LOG_WARN("other: console filter could not hook either tier0 print path");
        g_attach_tried = false;
        return;
    }
    LOG_INFO("other: console filter hooked tier0 %s%s", msg_ok ? "ConMsg" : "",
             color_ok ? (msg_ok ? " + ConColorMsg" : "ConColorMsg") : "");
}

void Shutdown() {
    g_con_msg_hook.detach();
    g_con_color_msg_hook.detach();
}
} // namespace Misc::Other::ConsoleFilter
