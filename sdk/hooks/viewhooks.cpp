#include "hooks.hpp"
#include "../../features/visuals/world_modulation.hpp"
#include "../../utils/detourhook.hpp"
#include "../../utils/log.hpp"
#include "../../utils/memory.hpp"
#include "../../utils/patterns/patterns.hpp"

namespace HooksInternal {
namespace {
// Visuals -> World: the material colour path (Nightmode / Sky tints).
Detour_Hook g_color_modulation_hook;
void* g_color_modulation_original{};
bool g_color_modulation_failed{};

// IEngineTrace... no: IMaterial::GetColorModulation (materialsystem.dll, `main`'s
// `get_color_modulation`). Verified in the supplied materialsystem.dll: `55 8B EC 83 EC ? 56 8B
// F1 8A 46` matches once at RVA 0xF90, the body writes the three floats through the three stack
// pointers and the epilogue is `ret 0xC`, so the thunk declares all three.
void __fastcall GetColorModulation(void* material, void* /*edx*/, float* r, float* g, float* b) {
    using Fn = void(__thiscall*)(void*, float*, float*, float*);
    const auto original = reinterpret_cast<Fn>(g_color_modulation_original);
    if (!original) return;
    original(material, r, g, b);
    if (g_color_modulation_failed) return;
    __try {
        WorldModulation::Tint(material, r, g, b);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_color_modulation_failed = true;
        LOG_ERROR("visuals world: exception in the GetColorModulation detour 0x%08lX; the world "
                  "tint stays off", GetExceptionCode());
    }
}

// The catalogue publishes module-relative addresses for these two, so the base has to be added.
std::uintptr_t ModuleFunction(const char* module, const char* name) {
    const auto rva = Patterns::Get(name);
    if (!rva) return 0;
    const auto base = reinterpret_cast<std::uintptr_t>(MEM::GetModuleBase(module));
    return base ? base + rva : 0;
}
} // namespace

void InstallWorldDetours() {
    const auto color_modulation = ModuleFunction("materialsystem.dll", "get_color_modulation");
    if (!color_modulation) {
        LOG_WARN("visuals world: get_color_modulation did not resolve; Nightmode and Sky stay "
                 "inactive");
    } else if (g_color_modulation_hook.attach(reinterpret_cast<void*>(color_modulation),
                                              reinterpret_cast<void*>(&GetColorModulation),
                                              &g_color_modulation_original)) {
        LOG_INFO("visuals world: colour-modulation detour attached at 0x%08lX",
                 static_cast<unsigned long>(color_modulation));
    } else {
        LOG_WARN("visuals world: the colour-modulation detour could not be created");
    }
}

void RemoveWorldDetours() {
    g_color_modulation_hook.detach();
}
} // namespace HooksInternal
