#include "general.hpp"
#include "../../../context.hpp"
#include "../../../sdk/convars.hpp"
#include "../../../sdk/entity/player.hpp"
#include "../../../sdk/interfaces/interfaces.hpp"
#include "../../../utils/detourhook.hpp"
#include "../../../utils/log.hpp"
#include "../../../utils/memory.hpp"
#include "../../../utils/offsets/offsets.hpp"
#include <atomic>
#include <windows.h>

namespace Misc::Ping {
namespace {
// CNetChan layout, ported from this repository's `main` branch (`structs.hpp`,
// class c_net_channel): 0x18 bytes of sequence bookkeeping, then
//   +0x18 out_sequence_nr, +0x1C in_sequence_nr, +0x20 out_sequence_nr_ack,
//   +0x24 out_reliable_state, +0x28 in_reliable_state, +0x2C choked_packets.
constexpr std::uintptr_t kInSequence = 0x1C;
constexpr std::uintptr_t kInReliableState = 0x28;
// FLOW_OUTGOING, as used by `main`'s ping spike.
constexpr int kFlowOutgoing = 1;

// Detour signatures: CNetChan::SendDatagram and CNetChan::ProcessPacket in engine.dll.
using SendDatagramFn = int(__fastcall*)(void*, void*, void*);
using ProcessPacketFn = void(__fastcall*)(void*, void*, void*, bool);

Detour_Hook g_send_hook;
Detour_Hook g_process_hook;
void* g_send_original{};
void* g_process_original{};

std::atomic<bool> g_want{false};
std::atomic<bool> g_enabled{false};
std::atomic<float> g_ms{0.f};
bool g_attach_tried{};
bool g_reported{};
bool g_bad_netchan{};

// `main` alternates the spike: one datagram after every freshly processed packet is sent
// untouched, which is what keeps the sequence offset from looking like a constant shift.
bool g_flip{false};
int g_last_reliable{-1};

std::uintptr_t ModuleRva(const char* module, const char* name) {
    const auto rva = offsets::Get(name);
    if (!rva) return 0;
    const auto base = reinterpret_cast<std::uintptr_t>(MEM::GetModuleBase(module));
    std::uintptr_t text = 0;
    std::uintptr_t size = 0;
    if (!base || !MEM::GetSection(base, ".text", &text, &size)) return 0;
    if (rva < text - base || rva >= text - base + size) return 0;
    return base + rva;
}

std::uintptr_t ResolveFunction() {
    for (const char* name : {"send_datagram", "send_datagram_alt"}) {
        const auto address = ModuleRva("engine.dll", name);
        if (address) return address;
    }
    return 0;
}

// The connection the engine is currently using: CClientState::net_channel. The offset
// comes from the signature catalogue with the snapshot value as the fallback, because a
// missing pattern must not turn into a null pointer dereference.
void* NetChannel() {
    if (!Interfaces::ClientState) return nullptr;
    std::uintptr_t offset = offsets::Get("clientstate_net_channel");
    if (!offset) offset = offsets::fallback::clientstate_net_channel;
    if (!offset) return nullptr;
    __try {
        return *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(Interfaces::ClientState) + offset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// INetChannelInfo::GetLatency(FLOW_OUTGOING). The interface comes from a vtable slot, so
// its own vtable is validated against engine.dll before it is called: a wrong slot 78
// would otherwise be a call through garbage.
float OutgoingLatency() {
    if (!Interfaces::Engine) return 0.f;
    void* info = nullptr;
    __try {
        info = Interfaces::Engine->GetNetChannelInfo();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0.f;
    }
    if (!info) return 0.f;

    const auto base = reinterpret_cast<std::uintptr_t>(MEM::GetModuleBase("engine.dll"));
    if (!base) return 0.f;
    void** vtable = *reinterpret_cast<void***>(info);
    if (!vtable) return 0.f;
    const auto latency_fn = reinterpret_cast<std::uintptr_t>(vtable[9]);
    std::uintptr_t text = 0;
    std::uintptr_t size = 0;
    if (!MEM::GetSection(base, ".text", &text, &size) || latency_fn < text || latency_fn >= text + size)
        return 0.f;

    float latency = 0.f;
    __try {
        latency = static_cast<INetChannelInfo*>(info)->GetLatency(kFlowOutgoing);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0.f;
    }
    if (!(latency >= 0.f && latency <= 1.f)) return 0.f; // unreadable or not a latency
    return latency;
}

// `main`: max(cl_interp, cl_interp_ratio / cl_updaterate). Zero means "unknown", which
// only makes the spike slightly larger than requested.
float LerpTime() {
    const float interp = ConVars::Float("cl_interp", 0.f, 0.5f, 0.f);
    const float ratio = ConVars::Float("cl_interp_ratio", 0.f, 10.f, 0.f);
    const float rate = ConVars::Float("cl_updaterate", 1.f, 1000.f, 0.f);
    if (!(rate > 0.f) || !(ratio > 0.f)) return interp;
    const float from_ratio = ratio / rate;
    return interp > from_ratio ? interp : from_ratio;
}

float MaxUnlag() {
    return ConVars::Float("sv_maxunlag", kMinMaxUnlag, kMaxMaxUnlag, kDefaultMaxUnlag);
}

// A real CNetChan is a C++ object from engine.dll, so its vtable has to live in that
// module. Checking it is what keeps a wrong client-state offset from writing into some
// unrelated structure: without this, a stale offset is a silent memory corruption.
bool LooksLikeNetChannel(void* netchan) {
    if (!netchan) return false;
    const auto base = reinterpret_cast<std::uintptr_t>(MEM::GetModuleBase("engine.dll"));
    std::uintptr_t text = 0;
    std::uintptr_t size = 0;
    if (!base || !MEM::GetSection(base, ".text", &text, &size)) return false;
    void** vtable = nullptr;
    __try {
        vtable = *reinterpret_cast<void***>(netchan);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (!vtable) return false;
    const auto first = reinterpret_cast<std::uintptr_t>(vtable[0]);
    return first >= text && first < text + size;
}

void ApplySpike(void* netchan) {
    if (!ctx.in_game || !ctx.local || !ctx.local->IsAlive()) return;
    if (!netchan || netchan != NetChannel()) return;
    if (!LooksLikeNetChannel(netchan)) {
        if (!g_bad_netchan) {
            g_bad_netchan = true;
            LOG_ERROR("misc: ping spike - the client-state net-channel offset points at "
                      "something that is not a CNetChan; feature disabled");
        }
        g_enabled.store(false, std::memory_order_relaxed);
        return;
    }

    const float interval = Interfaces::Globals ? Interfaces::Globals->interval_per_tick : 0.f;
    const int ticks = TicksForSeconds(
        SpikeSeconds(g_ms.load(std::memory_order_relaxed), OutgoingLatency(), LerpTime(), MaxUnlag()),
        interval);
    if (!ticks) return;

    // Alternating: the first datagram after a processed packet is left alone.
    if (g_flip) {
        g_flip = false;
        return;
    }

    auto* sequence = reinterpret_cast<int*>(reinterpret_cast<std::uintptr_t>(netchan) + kInSequence);
    const int before = *sequence;
    // A real sequence number is a small positive counter; anything else means the offset
    // is wrong for this build, and subtracting from it would corrupt the connection.
    if (!IsSequenceNumber(before)) {
        if (!g_bad_netchan) {
            g_bad_netchan = true;
            LOG_ERROR("misc: ping spike - in_sequence_nr at +0x%X reads %d, which is not a "
                      "sequence number; feature disabled", static_cast<unsigned>(kInSequence), before);
        }
        g_enabled.store(false, std::memory_order_relaxed);
        return;
    }
    if (!ShouldSpike(before, ticks)) return;
    *sequence = before - ticks;

    if (!g_reported) {
        g_reported = true;
        LOG_INFO("misc: ping spike active (%d ticks on the incoming sequence: %d -> %d)",
                 ticks, before, *sequence);
    }
}

int __fastcall SendDatagram(void* netchan, void* edx, void* datagram) {
    const auto original = reinterpret_cast<SendDatagramFn>(g_send_original);
    if (!original) return 0;
    if (g_enabled.load(std::memory_order_relaxed)) {
        __try {
            ApplySpike(netchan);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_enabled.store(false, std::memory_order_relaxed);
            LOG_ERROR("misc: ping spike exception 0x%08lX; feature disabled", GetExceptionCode());
        }
    }
    return original(netchan, edx, datagram);
}

void __fastcall ProcessPacket(void* netchan, void* edx, void* packet, bool header) {
    const auto original = reinterpret_cast<ProcessPacketFn>(g_process_original);
    if (!original) return;
    original(netchan, edx, packet, header);

    if (!g_enabled.load(std::memory_order_relaxed)) return;
    __try {
        if (!ctx.in_game || !ctx.local || !ctx.local->IsAlive()) {
            g_flip = true;
            return;
        }
        if (!netchan || netchan != NetChannel()) return;
        const int reliable = *reinterpret_cast<int*>(reinterpret_cast<std::uintptr_t>(netchan) + kInReliableState);
        if (reliable != g_last_reliable) g_flip = true;
        g_last_reliable = reliable;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}
} // namespace

void Publish(bool enabled, float milliseconds) {
    g_want.store(enabled, std::memory_order_relaxed);
    g_enabled.store(enabled, std::memory_order_relaxed);
    if (!(milliseconds > 0.f)) milliseconds = 0.f;
    if (milliseconds > static_cast<float>(kMaxSpikeMs)) milliseconds = static_cast<float>(kMaxSpikeMs);
    g_ms.store(milliseconds, std::memory_order_relaxed);
}

void FrameStage(int /*stage*/) {
    if (!g_want.load(std::memory_order_relaxed) || g_attach_tried) return;
    g_attach_tried = true;

    const auto send = ResolveFunction();
    if (!send) {
        LOG_WARN("misc: ping spike - send_datagram signature not found in engine.dll; feature off");
        return;
    }

    const bool send_ok = g_send_hook.attach(reinterpret_cast<void*>(send),
                                            reinterpret_cast<void*>(&SendDatagram), &g_send_original);
    if (!send_ok) {
        LOG_WARN("misc: ping spike could not hook send_datagram; feature off");
        return;
    }

    // The alternating half is optional: without it the spike still works, it just stays
    // applied on every datagram instead of every other one.
    const auto engine_base = reinterpret_cast<std::uintptr_t>(MEM::GetModuleBase("engine.dll"));
    const auto process = ModuleRva("engine.dll", "process_packet");
    if (process && engine_base) {
        if (!g_process_hook.attach(reinterpret_cast<void*>(process),
                                   reinterpret_cast<void*>(&ProcessPacket), &g_process_original))
            LOG_WARN("misc: ping spike could not hook process_packet; spike stays unflipped");
    } else {
        LOG_WARN("misc: ping spike - process_packet signature not found; spike stays unflipped");
    }

    LOG_INFO("misc: ping spike hooked send_datagram (engine.dll+0x%X)",
             static_cast<unsigned>(send - engine_base));

    // One line that says whether the ConVar path works at all: a misresolved ICvar slot
    // shows up here as "not found" instead of silently falling back to the defaults.
    LOG_INFO("misc: ping spike convars sv_maxunlag=%s cl_interp=%s cl_interp_ratio=%s "
             "cl_updaterate=%s (max_unlag=%.3fs lerp=%.3fs)",
             ConVars::Exists("sv_maxunlag") ? "found" : "missing",
             ConVars::Exists("cl_interp") ? "found" : "missing",
             ConVars::Exists("cl_interp_ratio") ? "found" : "missing",
             ConVars::Exists("cl_updaterate") ? "found" : "missing",
             static_cast<double>(MaxUnlag()), static_cast<double>(LerpTime()));
}

void Shutdown() {
    g_want.store(false, std::memory_order_relaxed);
    g_enabled.store(false, std::memory_order_relaxed);
    g_send_hook.detach();
    g_process_hook.detach();
}
} // namespace Misc::Ping
