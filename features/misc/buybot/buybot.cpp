#include "buybot.hpp"
#include "../../../context.hpp"
#include "../../../sdk/entity/player.hpp"
#include "../../../sdk/interfaces/interfaces.hpp"
#include "../../../utils/log.hpp"
#include <atomic>

namespace Misc::Buybot {
namespace {
std::atomic<Settings> g_settings{};
std::atomic<bool> g_should_buy{false};
bool g_reported{};

Settings Snapshot() { return g_settings.load(std::memory_order_relaxed); }

void Append(std::string& out, const char* command) {
    if (!out.empty()) out += ' ';
    out += "buy ";
    out += command;
    out += ';';
}

// `main` c_buy_bot::run command table, one line, expensive groups first.
std::string BuildCommand(const Settings& s) {
    std::string command;
    switch (s.primary) {
    case PrimaryAk47: Append(command, "ak47"); Append(command, "m4a1"); Append(command, "m4a1_silencer"); break;
    case PrimarySg556: Append(command, "aug"); Append(command, "sg556"); break;
    case PrimaryAwp: Append(command, "awp"); break;
    case PrimarySsg08: Append(command, "ssg08"); break;
    case PrimaryScar20: Append(command, "scar20"); Append(command, "g3sg1"); break;
    case PrimaryNegev: Append(command, "negev"); break;
    default: break;
    }
    switch (s.pistol) {
    case PistolDeagle: Append(command, "deagle"); break;
    case PistolElites: Append(command, "elite"); break;
    case PistolTec9: Append(command, "tec9"); Append(command, "fn57"); break;
    case PistolP250: Append(command, "p250"); break;
    case PistolCz75a: Append(command, "cz75a"); break;
    case PistolRevolver: Append(command, "revolver"); break;
    default: break;
    }
    switch (s.armor) {
    case ArmorKevlar: Append(command, "vest"); break;
    case ArmorKevlarHelmet: Append(command, "vesthelm"); break;
    default: break;
    }
    if (s.defuser) Append(command, "defuser");
    if (s.taser) Append(command, "taser");
    if (s.grenades) {
        Append(command, "hegrenade");
        Append(command, "molotov");
        Append(command, "incgrenade");
        Append(command, "smokegrenade");
        Append(command, "flashbang");
    }
    return command;
}
} // namespace

void Publish(const Settings& settings) { g_settings.store(settings, std::memory_order_relaxed); }

void OnRoundStart() { g_should_buy.store(true, std::memory_order_relaxed); }

void Tick() {
    if (!g_should_buy.exchange(false, std::memory_order_relaxed)) return;
    if (!ctx.in_game || !ctx.local || !ctx.local->IsAlive()) return;
    if (!Interfaces::Engine) return;

    const Settings s = Snapshot();
    const std::string command = BuildCommand(s);
    if (command.empty()) return;

    if (!g_reported) {
        g_reported = true;
        LOG_INFO("buy bot: round start, sending \"%s\"", command.c_str());
    }
    Interfaces::Engine->ExecuteClientCmd(command.c_str());
}

void Shutdown() { g_should_buy.store(false, std::memory_order_relaxed); }
} // namespace Misc::Buybot
