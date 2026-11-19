#include "other.hpp"
#include "../../../sdk/interfaces/interfaces.hpp"
#include "../../../utils/detourhook.hpp"
#include "../../../utils/log.hpp"
#include "../../../utils/memory.hpp"
#include "../../../utils/math/vector.hpp"
#include "../../../utils/offsets/offsets.hpp"
#include <atomic>
#include <windows.h>

namespace Misc::Other::Accept {
namespace {
// IEngineSoundClient::EmitSound1 - slot 5, the same table numbering as
// IBaseClientDLL::FrameStageNotify (37), which this build already hooks.
constexpr std::size_t kEmitSoundSlot = 5;

using EmitSoundFn = void(__fastcall*)(void*, void*, void*, int, int, const char*, unsigned int,
                                      const char*, float, int, float, int, int, const Vector*,
                                      const Vector*, void*, bool, float, int, int);
// SetLocalPlayerReady(const char*) - client.dll, found by the
// "55 8B EC 83 E4 F8 8B 4D 08 BA ? ? ? ? E8 ? ? ? ? 85 C0 75 12" signature. The prologue
// reads its argument from [ebp+8], i.e. it is a plain one-argument function.
using ReadyFn = bool(__cdecl*)(const char*);

VMT_Hook g_hook;
std::atomic<bool> g_enabled{false};
std::atomic<bool> g_want{false};
ReadyFn g_ready{};
bool g_resolved{};
bool g_attach_tried{};
bool g_reported{};

// A resolved signature is an RVA into client.dll; anything outside .text is refused so a
// stale pattern cannot become a wild call into the middle of an unrelated function.
ReadyFn ResolveReady() {
    if (g_resolved) return g_ready;
    g_resolved = true;

    const auto base = reinterpret_cast<std::uintptr_t>(MEM::GetModuleBase("client.dll"));
    const auto rva = offsets::Get("set_local_player_ready");
    std::uintptr_t text = 0;
    std::uintptr_t size = 0;
    if (!base || !rva) {
        LOG_WARN("other: set_local_player_ready not resolved; auto-accept stays off");
        return nullptr;
    }
    if (!MEM::GetSection(base, ".text", &text, &size) || rva < text - base ||
        rva >= text - base + size) {
        LOG_WARN("other: set_local_player_ready outside client.dll .text; auto-accept stays off");
        return nullptr;
    }
    g_ready = reinterpret_cast<ReadyFn>(base + rva);
    LOG_INFO("other: auto-accept handler at client.dll+0x%X", static_cast<unsigned>(rva));
    return g_ready;
}

void __fastcall EmitSound(void* self, void* edx, void* filter, int index, int channel,
                          const char* entry, unsigned int hash, const char* sample, float volume,
                          int seed, float attenuation, int flags, int pitch, const Vector* origin,
                          const Vector* direction, void* utl_vector, bool update, float time,
                          int speaker, int unk) {
    const auto original = g_hook.get_original<EmitSoundFn>();
    if (g_enabled.load(std::memory_order_relaxed) && IsAcceptSound(entry)) {
        if (const auto ready = ResolveReady(); ready && ready("") && !g_reported) {
            g_reported = true;
            LOG_INFO("other: auto-accept - match accepted");
        }
    }
    if (original)
        original(self, edx, filter, index, channel, entry, hash, sample, volume, seed, attenuation,
                 flags, pitch, origin, direction, utl_vector, update, time, speaker, unk);
}
} // namespace

void SetEnabled(bool enabled) {
    // Published from the render thread, patched from the game thread (see FrameStage).
    g_enabled.store(enabled, std::memory_order_relaxed);
    g_want.store(enabled, std::memory_order_relaxed);
}

void FrameStage(int /*stage*/) {
    if (!g_want.load(std::memory_order_relaxed) || g_attach_tried) return;
    g_attach_tried = true;
    if (!Interfaces::EngineSound) {
        LOG_WARN("other: IEngineSoundClient missing; auto-accept cannot hook EmitSound");
        return;
    }
    if (!g_hook.attach(Interfaces::EngineSound, kEmitSoundSlot,
                       reinterpret_cast<void*>(&EmitSound))) {
        LOG_WARN("other: auto-accept could not hook EmitSound slot %zu", kEmitSoundSlot);
        g_attach_tried = false;
        return;
    }
    LOG_INFO("other: auto-accept hooked EmitSound (slot %zu)", kEmitSoundSlot);
}

void Shutdown() { g_hook.detach(); }
} // namespace Misc::Other::Accept
