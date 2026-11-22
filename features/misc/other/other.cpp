#include "other.hpp"
#include "../../../sdk/convars.hpp"
#include "../../../sdk/interfaces/interfaces.hpp"
#include "../../../utils/log.hpp"
#include <atomic>

namespace Misc::Other {
namespace {
std::atomic<bool> g_sv_pure{false};
std::atomic<bool> g_sv_cheats{false};
std::atomic<bool> g_console_filter{false};
std::atomic<bool> g_autoaccept{false};
std::atomic<bool> g_radar{false};

// `main` c_event_logs::filter_console, ported: the developer console only shows lines that
// contain con_filter_text, so enabling the game's filter with a junk string hides the game's
// spam. First call performs the one-shot reset the same way `main` does (developer=0,
// con_filter_enable=1, empty text), after which the text flips between the junk filter and
// empty as the item is toggled.
namespace ConsoleFilterState {
bool set_console = true;
bool log_value = true;
const char* const kJunk = "IrWL5106TZZKNFPz4P4Gl3pSN?J370f5hi373ZjPg%VOVh6lN";
}

void SyncConsoleFilter() {
    if (!Interfaces::ConVar) return;

    auto* enable = ConVars::Find("con_filter_enable");
    auto* text = ConVars::Find("con_filter_text");
    if (!enable || !text) return;

    if (ConsoleFilterState::set_console) {
        ConsoleFilterState::set_console = false;
        if (auto* developer = ConVars::Find("developer")) ConVars::SetInt(developer, 0);
        ConVars::SetInt(enable, 1);
        ConVars::SetString(text, "");
    }

    const bool filter = Detail::ConsoleFilter();
    if (ConsoleFilterState::log_value != filter) {
        ConsoleFilterState::log_value = filter;
        ConVars::SetString(text, filter ? ConsoleFilterState::kJunk : "");
    }
}
} // namespace

void Publish(const Settings& settings) {
    g_sv_pure.store(settings.sv_pure);
    g_sv_cheats.store(settings.sv_cheats);
    g_console_filter.store(settings.console_filter);
    g_autoaccept.store(settings.autoaccept);
    g_radar.store(settings.radar);
}

namespace Detail {
bool SvPure() { return g_sv_pure.load(); }
bool SvCheats() { return g_sv_cheats.load(); }
bool ConsoleFilter() { return g_console_filter.load(); }
bool AutoAccept() { return g_autoaccept.load(); }
bool Radar() { return g_radar.load(); }
} // namespace Detail

void FrameStage(int stage) {
    SyncConsoleFilter();
    Detail::RadarFrameStage(stage);
    Detail::SvPureFrameStage(stage);
    Detail::AutoAcceptFrameStage(stage);

    // "Bypass SV Cheats" stays documented-inactive: in `main` it is a build-specific VMT
    // rewrite of the sv_cheats ConVar's get_int slot, which this build has not verified.
    static bool sv_cheats_logged = false;
    if (Detail::SvCheats() && !sv_cheats_logged) {
        sv_cheats_logged = true;
        LOG_WARN("misc other: Bypass SV Cheats is selected but inactive in this build "
                 "(needs the sv_cheats ConVar get_int VMT slot, not verified here)");
    }
}

void Shutdown() {
    Detail::RadarShutdown();
    Detail::SvPureShutdown();
    Detail::AutoAcceptShutdown();
}
} // namespace Misc::Other
