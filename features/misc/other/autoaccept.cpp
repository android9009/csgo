#include "other.hpp"
#include "../../../sdk/interfaces/interfaces.hpp"
#include "../../../utils/detourhook.hpp"
#include "../../../utils/log.hpp"
#include "../../../utils/memory.hpp"
#include "../../../utils/math/vector.hpp"
#include "../../../utils/offsets/offsets.hpp"
#include <atomic>
#include <cstring>
#include <windows.h>

namespace Misc::Other {
namespace {
// IEngineSound::EmitSound - slot 5, the same table numbering as IBaseClientDLL::FrameStage
// Notify (37) which this build already hooks (this repo's base branch accept.cpp).
constexpr std::size_t kEmitSoundSlot = 5;

using EmitSoundFn = void(__fastcall*)(void*, void*, void*, int, int, const char*, unsigned int,
                                      const char*, float, int, float, int, int, const Vector*,
                                      const Vector*, void*, bool, float, int, int);
// SetLocalPlayerReady(const char*) - client.dll, found by the "set_local_player_ready"
// signature already in this build's catalogue. The prologue reads its argument from [ebp+8],
// i.e. a plain one-argument function.
using ReadyFn = bool(__cdecl*)(const char*);

VMT_Hook g_hook;
ReadyFn g_ready{};
bool g_resolved{};
bool g_attach_tried{};
bool g_reported{};

// The match-accept popup plays this Panorama sound entry; anything else is ignored.
bool IsAcceptSound(const char* sound_entry) {
    return sound_entry && std::strcmp(sound_entry, "UIPanorama.popup_accept_match_beep") == 0;
}

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
        LOG_WARN("other auto-accept: set_local_player_ready not resolved; auto-accept stays off");
        return nullptr;
    }
    if (!MEM::GetSection(base, ".text", &text, &size) || rva < text - base ||
        rva >= text - base + size) {
        LOG_WARN("other auto-accept: set_local_player_ready outside client.dll .text; auto-accept stays off");
        return nullptr;
    }
    g_ready = reinterpret_cast<ReadyFn>(base + rva);
    LOG_INFO("other auto-accept: handler at client.dll+0x%X", static_cast<unsigned int>(rva));
    return g_ready;
}

// When the match-accept popup sound plays, answer the prompt ourselves.
void __fastcall EmitSound(void* self, void* edx, void* filter, int index, int channel,
                          const char* entry, unsigned int hash, const char* sample, float volume,
                          int seed, float attenuation, int flags, int pitch, const Vector* origin,
                          const Vector* direction, void* utl_vector, bool update, float time,
                          int speaker, int unk) {
    const auto original = g_hook.get_original<EmitSoundFn>();
    if (Detail::AutoAccept() && IsAcceptSound(entry)) {
        if (const auto ready = ResolveReady(); ready && ready("") && !g_reported) {
            g_reported = true;
            LOG_INFO("other auto-accept: match accepted");
        }
    }
    if (original)
        original(self, edx, filter, index, channel, entry, hash, sample, volume, seed,
                 attenuation, flags, pitch, origin, direction, utl_vector, update, time,
                 speaker, unk);
}
} // namespace

namespace Detail {
// Attached lazily, once the engine sound interface is up and the user turned the item on.
void AutoAcceptFrameStage(int) {
    if (!Detail::AutoAccept() || g_attach_tried) return;
    if (!Interfaces::EngineSound) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            LOG_WARN("other auto-accept: IEngineSound not captured yet; retrying");
        }
        return; // interface not up yet: retry next frame
    }
    g_attach_tried = true;
    if (!g_hook.attach(Interfaces::EngineSound, kEmitSoundSlot,
                       reinterpret_cast<void*>(&EmitSound))) {
        g_attach_tried = false; // allow a retry
        LOG_WARN("other auto-accept: could not hook EmitSound slot %zu", kEmitSoundSlot);
        return;
    }
    LOG_INFO("other auto-accept: EmitSound (slot %zu) hooked", kEmitSoundSlot);
}

void AutoAcceptShutdown() { g_hook.detach(); }
} // namespace Detail
} // namespace Misc::Other
