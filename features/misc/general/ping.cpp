#include "general.hpp"
#include "../../../context.hpp"
#include "../../../sdk/interfaces/interfaces.hpp"
#include "../../../sdk/third_party/gui/src/gui/values.h"
#include "../../../utils/detourhook.hpp"
#include "../../../utils/log.hpp"
#include "../../../utils/memory.hpp"
#include "../../../utils/patterns/catalog.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>

namespace Misc::General::Detail {
namespace {
// The client's INetChannel lives at client_state+0x9C; the field offsets below are the
// published layout `main` reads through its c_net_channel (structs.hpp), which we mirror
// with plain offsets because this build has no net-channel struct.
constexpr std::uintptr_t kNetchanInSequence = 0x1C;
constexpr std::uintptr_t kNetchanInReliable = 0x28;

std::atomic<bool> g_flipped_state{false};
std::atomic<int> g_last_reliable_state{-1};

Detour_Hook g_send_datagram_hook;
Detour_Hook g_process_packet_hook;
void* g_send_datagram_original = nullptr;
void* g_process_packet_original = nullptr;
bool g_installed = false;

std::uintptr_t EngineFunction(const char* name) {
    const auto rva = Patterns::Get(name);
    if (!rva) return 0;
    const auto base = reinterpret_cast<std::uintptr_t>(MEM::GetModuleBase("engine.dll"));
    return base ? base + rva : 0;
}

int ReadInt(void* netchan, std::uintptr_t offset) {
    __try { return *reinterpret_cast<int*>(reinterpret_cast<std::uintptr_t>(netchan) + offset); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

void WriteInt(void* netchan, std::uintptr_t offset, int value) {
    __try { *reinterpret_cast<int*>(reinterpret_cast<std::uintptr_t>(netchan) + offset) = value; }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

void* ClientNetchan() {
    if (!Interfaces::ClientState) return nullptr;
    __try { return *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(Interfaces::ClientState) + 0x9C); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

// `main` c_ping_spike::on_net_chan: drop the incoming sequence back by the spike amount so
// the server thinks we are that far behind. Only when we are not mid-resync (flipped).
void OnNetChan(void* netchan, float latency_seconds) {
    if (g_flipped_state.exchange(false, std::memory_order_relaxed)) return;

    const auto interval = Interfaces::Globals ? Interfaces::Globals->interval_per_tick : 0.f;
    if (interval <= 0.f) return;
    const int ticks = static_cast<int>(0.5f + latency_seconds / interval);
    if (ticks <= 0) return;

    __try {
        if (ReadInt(netchan, kNetchanInSequence) > ticks)
            WriteInt(netchan, kNetchanInSequence, ReadInt(netchan, kNetchanInSequence) - ticks);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// `main` c_ping_spike::on_procces_packet: a change in the reliable state means the network
// just resynced, so arm the flip to skip the next spike.
void OnProcessPacket() {
    if (!PingEnabled() || !ctx.in_game || !ctx.local || !ctx.local->IsAlive()) {
        g_flipped_state.store(true, std::memory_order_relaxed);
        return;
    }
    const auto netchan = ClientNetchan();
    if (!netchan) return;

    const int reliable = ReadInt(netchan, kNetchanInReliable);
    if (reliable != g_last_reliable_state.load(std::memory_order_relaxed))
        g_flipped_state.store(true, std::memory_order_relaxed);
    g_last_reliable_state.store(reliable, std::memory_order_relaxed);
}

int __fastcall SendDatagramDetour(void* netchan, void* edx, void* datagram) {
    using Fn = int(__fastcall*)(void*, void*, void*);
    const auto original = reinterpret_cast<Fn>(g_send_datagram_original);

    if (PingEnabled() && ctx.in_game && ctx.local && ctx.local->IsAlive() &&
        netchan == ClientNetchan()) {
        // spike_amount in seconds, minus the real outgoing latency and the interpolation
        // window, clamped to the unlag buffer (`main` detour_hooks send_datagram).
        float spike = PingMs() * 0.001f;
        // The outgoing latency comes from the engine's per-connection view of the same
        // channel (verified slot 78), not from the raw field reads above.
        if (Interfaces::Engine) {
            if (auto info = reinterpret_cast<INetChannelInfo*>(Interfaces::Engine->GetNetChannelInfo()))
                spike -= info->GetLatency(1); // FLOW_OUTGOING
        }
        spike -= ctx.lerp_time;
        const float correct = std::clamp(spike, 0.f, ctx.max_unlag > 0.f ? ctx.max_unlag : 0.2f);

        const int saved_seq = ReadInt(netchan, kNetchanInSequence);
        const int saved_rel = ReadInt(netchan, kNetchanInReliable);
        OnNetChan(netchan, correct);
        int result = original ? original(netchan, edx, datagram) : 0;
        WriteInt(netchan, kNetchanInSequence, saved_seq);
        WriteInt(netchan, kNetchanInReliable, saved_rel);
        return result;
    }
    return original ? original(netchan, edx, datagram) : 0;
}

void __fastcall ProcessPacketDetour(void* netchan, void* edx, void* packet, bool header) {
    using Fn = void(__fastcall*)(void*, void*, void*, bool);
    const auto original = reinterpret_cast<Fn>(g_process_packet_original);
    if (original) original(netchan, edx, packet, header);
    OnProcessPacket();
}
} // namespace

void PingReset() {
    g_flipped_state.store(false, std::memory_order_relaxed);
    g_last_reliable_state.store(-1, std::memory_order_relaxed);
}

// Installed from the Misc facade once, removed on shutdown. Both engine functions resolve
// from the pattern catalogue; if either is missing the spike simply stays off.
bool PingInstall() {
    if (g_installed) return true;
    auto send_datagram = EngineFunction("send_datagram");
    if (!send_datagram) send_datagram = EngineFunction("send_datagram_alt");
    const auto process_packet = EngineFunction("process_packet");
    if (!send_datagram || !process_packet) {
        LOG_WARN("ping spike: network patterns did not resolve; the feature stays off");
        return false;
    }
    if (g_send_datagram_hook.attach(reinterpret_cast<void*>(send_datagram),
                                    reinterpret_cast<void*>(&SendDatagramDetour),
                                    &g_send_datagram_original) &&
        g_process_packet_hook.attach(reinterpret_cast<void*>(process_packet),
                                     reinterpret_cast<void*>(&ProcessPacketDetour),
                                     &g_process_packet_original)) {
        g_installed = true;
        LOG_INFO("ping spike: SendDatagram/ProcessPacket detours attached");
        return true;
    }
    LOG_WARN("ping spike: could not attach the network detours; the feature stays off");
    return false;
}

void PingRemove() {
    g_send_datagram_hook.detach();
    g_process_packet_hook.detach();
    g_installed = false;
}
} // namespace Misc::General::Detail
