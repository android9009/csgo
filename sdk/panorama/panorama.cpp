#include "panorama.hpp"
#include "../../utils/log.hpp"
#include "../../utils/memory.hpp"

namespace panorama {
namespace {
constexpr int kRunScriptIndex = 113;
void* g_run_script{};
bool g_resolved{};

bool InsideModule(const char* module, std::uintptr_t address) {
    if (!address) return false;
    const auto base = reinterpret_cast<std::uintptr_t>(MEM::GetModuleBase(module));
    std::uintptr_t text = 0;
    std::uintptr_t size = 0;
    if (!base || !MEM::GetSection(base, ".text", &text, &size)) return false;
    return address >= text && address < text + size;
}

// Pushes one extra argument and restores ESP after the call, so a RunScript whose
// real arity is one parameter shorter cannot unbalance the stack.
void Invoke(void* engine, void* panel, const char* js, const char* path) {
    void* saved = nullptr;
    __asm {
        mov saved, esp
        push 0                  // trailing bool added in later builds
        push 0                  // already compiled
        push 10                 // line, mirrors the values public callers pass
        push 8                  // character
        push path
        push js
        push panel
        mov ecx, engine         // __thiscall: this in ecx, the rest on the stack
        mov eax, g_run_script
        call eax                // indirect, so the symbol is never taken as code
        mov esp, saved          // undo whatever arity the callee actually popped
    }
}
} // namespace

void* ResolveRunScript(UIEngine* engine) {
    if (g_resolved) return g_run_script;
    g_resolved = true;
    if (!engine) return nullptr;
    const auto slot = reinterpret_cast<std::uintptr_t>((*reinterpret_cast<void***>(engine))[kRunScriptIndex]);
    if (!InsideModule("panorama.dll", slot)) {
        LOG_WARN("panorama: RunScript slot %d (%p) is outside panorama.dll; JS injection stays off",
                 kRunScriptIndex, reinterpret_cast<void*>(slot));
        return nullptr;
    }
    g_run_script = reinterpret_cast<void*>(slot);
    LOG_INFO("panorama: RunScript resolved at %p", g_run_script);
    return g_run_script;
}

void RunScript(UIEngine* engine, void* panel, const char* js, const char* path) {
    if (!engine || !panel || !js) return;
    if (!g_run_script && !ResolveRunScript(engine)) return;
    Invoke(engine, panel, js, path);
}
} // namespace panorama
