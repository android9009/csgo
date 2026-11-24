#include "hooks.hpp"
#include "../../menu/preview/model_preview.hpp"
#include "../../utils/config.hpp"
#include "../../utils/log.hpp"
#include "../../menu/menu.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <exception>

bool Menu_IsOpen() { return Config::settings.show_menu; }

namespace HooksInternal {
namespace {
using Addr = ULONG_PTR; // the Windows pointer-sized int (32-bit here)

// The DLL's own base, so exception / return addresses are logged as stable RVAs
// ("dll+0x1A2B34") that csgo_base.pdb can resolve; the image base changes with
// every injection, absolute addresses are only useful together with a memory map.
Addr DllBase() {
    static const Addr base = reinterpret_cast<Addr>(GetModuleHandleA("csgo_release.dll"));
    return base;
}

void FormatAddress(Addr address, char* out, size_t out_size) {
    const Addr base = DllBase();
    if (base && address >= base)
        std::snprintf(out, out_size, "dll+0x%lX", static_cast<unsigned long>(address - base));
    else
        std::snprintf(out, out_size, "%p", reinterpret_cast<void*>(static_cast<std::uintptr_t>(address)));
}

// The render-thread call stack at the fault site. RVAs for our module, absolute
// addresses for the rest (d3d9.dll, client.dll, ...).
void LogStackBacktrace() {
    void* frames[10];
    const unsigned count = CaptureStackBackTrace(1, 10, frames, nullptr);
    for (unsigned i = 0; i < count; ++i) {
        char where[64];
        FormatAddress(reinterpret_cast<Addr>(frames[i]), where, sizeof(where));
        LOG_ERROR("  backtrace %u: %s", i, where);
    }
}

// Every render fault is counted; at most one full report per second (the hook is
// per-frame, a looping fault must not drown the log). The first report in a
// process is always printed - that is the one the menu breaks on.
void LogRenderFault(const char* kind, EXCEPTION_POINTERS* info) {
    static std::atomic<std::uint32_t> faults{0};
    static std::chrono::steady_clock::time_point last{};
    const auto now = std::chrono::steady_clock::now();
    if (now - last < std::chrono::seconds(1)) return;
    last = now;
    const std::uint32_t number = faults.fetch_add(1, std::memory_order_relaxed) + 1;
    const int phase = FatalityMenu::render_phase();
    if (!info || !info->ExceptionRecord) {
        LOG_ERROR("menu render fault #%u (%s) phase=%d open=%d: no exception record", number, kind, phase, Menu_IsOpen());
        LogStackBacktrace();
        return;
    }
    auto* record = info->ExceptionRecord;
    char where[64];
    FormatAddress(record->ExceptionAddress, where, sizeof(where));
    if (record->ExceptionCode == 0xC0000005) {
        // Access violation: the access type and the faulting data address say a lot
        // (a tiny data address = member access through a null/stale pointer).
        const bool is_write = record->ExceptionInformation[0] == 1;
        char data_where[64];
        FormatAddress(record->ExceptionInformation[1], data_where, sizeof(data_where));
        LOG_ERROR("menu render fault #%u: AV %s %s at %s phase=%d open=%d",
                  number, is_write ? "write" : "read", data_where, where, phase, Menu_IsOpen());
    } else {
        LOG_ERROR("menu render fault #%u: 0x%08lX (%s) at %s phase=%d open=%d",
                  number, record->ExceptionCode, kind, where, phase, Menu_IsOpen());
    }
    LogStackBacktrace();
}

// A C++ exception (bad_alloc, out_of_range, ...) must be caught with a C++
// handler: it unwinds properly and carries a message. SEH and C++ exception
// handling cannot coexist in one function (C2712/C2713), so the catch lives
// here and the SEH net for hardware faults (AVs) stays in EndScene.
void RenderWithCatch(IDirect3DDevice9* dev, HWND hwnd, bool open) {
    try {
        FatalityMenu::Render(dev, hwnd, open);
    } catch (const std::exception& e) {
        LOG_ERROR("menu render C++ exception: %s (phase=%d open=%d)", e.what(), FatalityMenu::render_phase(), Menu_IsOpen());
        LogStackBacktrace();
    } catch (...) {
        LOG_ERROR("menu render C++ exception: unknown type (phase=%d open=%d)", FatalityMenu::render_phase(), Menu_IsOpen());
        LogStackBacktrace();
    }
}
} // namespace

int LogRenderException(EXCEPTION_POINTERS* info) {
    LogRenderFault("seh", info);
    return EXCEPTION_EXECUTE_HANDLER;
}
long __stdcall EndScene(void* device) {
    auto* dev = static_cast<IDirect3DDevice9*>(device);
    if (!dev) return D3D_OK;
    // Preserve the game's complete device state around the original GUI renderer.
    IDirect3DStateBlock9* state = nullptr;
    if (SUCCEEDED(dev->CreateStateBlock(D3DSBT_ALL, &state)) && state) state->Capture();
    __try {
        RenderWithCatch(dev, FindWindowA("Valve001", nullptr), Config::settings.show_menu);
    } __except (LogRenderException(GetExceptionInformation())) {
    }
    if (state) { state->Apply(); state->Release(); }
    auto original = Hooks::GetEndSceneOriginalFor(dev);
    return original ? original(dev) : D3D_OK;
}
long __stdcall Reset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* params) {
    auto original = Hooks::GetResetOriginalFor(dev);
    if (!original) return D3DERR_INVALIDCALL;
    ModelPreview::BeforeReset();
    FatalityMenu::BeforeReset(dev);
    const auto result = original(dev, params);
    FatalityMenu::AfterReset(dev, result);
    return result;
}
}
