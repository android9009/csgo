#include "buybot.hpp"
#include "../../../context.hpp"
#include "../../../sdk/entity/player.hpp"
#include "../../../sdk/interfaces/interfaces.hpp"
#include "../../../utils/log.hpp"
#include "../../../utils/memory.hpp"
#include "../../../utils/patterns/patterns.hpp"
#include <windows.h>
#include <cstdint>

namespace Misc::Buybot {
namespace {
// Published by menu/bridges/misc_runtime.hpp on the render thread, read on the game
// thread; each field is its own atomic so a tick can never see a half-updated selection.
std::atomic<int> g_primary{PrimaryNone};
std::atomic<int> g_pistol{PistolNone};
std::atomic<int> g_armor{ArmorNone};
std::atomic<bool> g_defuser{false};
std::atomic<bool> g_taser{false};
std::atomic<bool> g_grenades{false};

Settings Snapshot() {
    Settings settings;
    settings.primary = g_primary.load(std::memory_order_relaxed);
    settings.pistol = g_pistol.load(std::memory_order_relaxed);
    settings.armor = g_armor.load(std::memory_order_relaxed);
    settings.defuser = g_defuser.load(std::memory_order_relaxed);
    settings.taser = g_taser.load(std::memory_order_relaxed);
    settings.grenades = g_grenades.load(std::memory_order_relaxed);
    return settings;
}

// IEngineClient slot 114 is only used after its function pointer is proven to live in
// engine.dll's .text: a wrong index in a 200-entry vtable would be a wild call.
bool ClientCmdUsable() {
    static std::atomic<int> state{0}; // 0 unknown, 1 usable, -1 refused
    const int cached = state.load(std::memory_order_acquire);
    if (cached != 0) return cached > 0;
    bool usable = false;
    __try {
        IEngineClient* engine = Interfaces::Engine;
        auto* table = engine ? *reinterpret_cast<void***>(engine) : nullptr;
        const auto fn = table ? reinterpret_cast<std::uintptr_t>(table[kClientCmdUnrestrictedSlot]) : 0;
        std::uintptr_t start = 0, size = 0;
        const auto base = reinterpret_cast<std::uintptr_t>(MEM::GetModuleBase("engine.dll"));
        if (fn && base && MEM::GetSection(base, ".text", &start, &size) && size)
            usable = fn >= start && fn < start + size;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        usable = false;
    }
    if (!usable) {
        static bool reported = false;
        if (!reported) {
            reported = true;
            LOG_WARN("buy bot: IEngineClient slot %d does not point into engine.dll; auto buy stays off",
                     static_cast<int>(kClientCmdUnrestrictedSlot));
        }
    }
    state.store(usable ? 1 : -1, std::memory_order_release);
    return usable;
}

bool g_rules_reported{};
bool g_active_reported{};
bool g_buy_reported{};
Window g_window;
} // namespace

void Publish(const Settings& settings) {
    g_primary.store(settings.primary, std::memory_order_relaxed);
    g_pistol.store(settings.pistol, std::memory_order_relaxed);
    g_armor.store(settings.armor, std::memory_order_relaxed);
    g_defuser.store(settings.defuser, std::memory_order_relaxed);
    g_taser.store(settings.taser, std::memory_order_relaxed);
    g_grenades.store(settings.grenades, std::memory_order_relaxed);
}

namespace Detail {
void* GameRules() {
    // Resolved once: the operand of `A1 ? ? ? ?` is the address of the client's global
    // game-rules pointer, so one dereference gives the object `main` reads as
    // `**offsets::game_rules`.
    static std::atomic<void*> cached{nullptr};
    static std::atomic<bool> attempted{false};
    if (void* rules = cached.load(std::memory_order_acquire)) return rules;
    bool expected = false;
    if (!attempted.compare_exchange_strong(expected, true)) return nullptr;

    void* rules = nullptr;
    const auto operand = Patterns::Get("game_rules");
    if (!operand) {
        if (!g_rules_reported) {
            g_rules_reported = true;
            LOG_WARN("buy bot: the game_rules signature did not resolve; auto buy stays off");
        }
        return nullptr;
    }
    __try {
        rules = *reinterpret_cast<void**>(operand);
        if (rules) {
            // m_bFreezePeriod is a bool: anything else means the signature matched a
            // different global and writing nothing is the only safe answer.
            const auto freeze = *reinterpret_cast<const unsigned char*>(
                reinterpret_cast<const unsigned char*>(rules) + kFreezePeriodOffset);
            if (freeze > 1) rules = nullptr;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        rules = nullptr;
    }
    if (!rules) {
        if (!g_rules_reported) {
            g_rules_reported = true;
            LOG_WARN("buy bot: game rules object failed validation; auto buy stays off");
        }
        return nullptr;
    }
    cached.store(rules, std::memory_order_release);
    LOG_INFO("buy bot: game rules at %p (freeze byte +0x%02X)", rules,
             static_cast<int>(kFreezePeriodOffset));
    return rules;
}

bool FreezePeriod(void* rules) {
    if (!rules) return false;
    bool freeze = false;
    __try {
        freeze = *reinterpret_cast<const unsigned char*>(
            reinterpret_cast<const unsigned char*>(rules) + kFreezePeriodOffset) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        freeze = false;
    }
    return freeze;
}
} // namespace Detail

void Tick() {
    const Settings settings = Snapshot();
    if (!Configured(settings)) {
        if (g_window.open) g_window.Update(false);
        return;
    }
    if (!Interfaces::Engine) return;

    bool in_game = false;
    __try { in_game = Interfaces::Engine->IsInGame() && Interfaces::Engine->IsConnected(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { in_game = false; }

    void* rules = in_game ? Detail::GameRules() : nullptr;
    const bool freeze = in_game && Detail::FreezePeriod(rules);
    g_window.Update(freeze);

    CCSPlayer* local = ctx.local;
    bool alive = false;
    __try { alive = local && local->IsAlive(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { alive = false; }

    const float now = Interfaces::Globals ? Interfaces::Globals->curtime : 0.f;
    if (!ShouldAttempt(settings, in_game, alive, g_window.open, g_window, now)) return;

    if (!ClientCmdUsable()) return;
    const std::string command = BuildCommand(settings);
    if (command.empty()) return;
    if (!g_active_reported) {
        g_active_reported = true;
        LOG_INFO("buy bot: armed (freeze-time purchases, up to %d attempts per round)", kMaxAttempts);
    }
    __try { Interfaces::Engine->ExecuteClientCmd(command.c_str()); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!g_buy_reported) {
            g_buy_reported = true;
            LOG_ERROR("buy bot: client command threw 0x%08lX; auto buy disabled", GetExceptionCode());
        }
        return;
    }
    g_window.Note(now);
    // One line per purchase is the feature's own feedback; there is no per-tick logging.
    LOG_INFO("buy bot: attempt %d/%d sent \"%s\"", g_window.attempts, kMaxAttempts, command.c_str());
}

void Shutdown() {
    g_window = Window{};
    g_rules_reported = false;
    g_active_reported = false;
    g_buy_reported = false;
}
} // namespace Misc::Buybot
