#include "general.hpp"
#include "../../../utils/log.hpp"

namespace {
bool g_reported_ping_spike{};
} // namespace

namespace Misc::General {
void Publish(const Settings& settings) {
    Radar::SetEnabled(settings.radar);
    ClanTag::SetEnabled(settings.clantag);
    Scoreboard::Publish(settings.scoreboard, settings.scoreboard_scale_x,
                        settings.scoreboard_scale_y, settings.scoreboard_scale_weapon);
    Chat::SetEnabled(settings.reveal_enemy_chat);
    Ragdoll::Publish(settings.ragdoll_thrust, settings.ragdoll_percent);

    // Ping spike is a menu entry without an implementation: spiking the outgoing datagram
    // rate needs either a netchan hook this SDK cannot address or a ConVar write path that
    // is not verified for this build. It reports instead of silently doing nothing.
    if (settings.ping_spike && !g_reported_ping_spike) {
        g_reported_ping_spike = true;
        LOG_WARN("misc: Ping spike is not implemented in this build (%.0f ms requested; see "
                 "docs/misc-runtime.md)", static_cast<double>(settings.ping_spike_ms));
    }
}

void FrameStage(int stage) {
    Chat::FrameStage(stage);
    Radar::FrameStage(stage);
    ClanTag::FrameStage(stage);
    Scoreboard::FrameStage(stage);
    Ragdoll::FrameStage(stage);
}

void Shutdown() {
    Chat::Shutdown();
    ClanTag::Shutdown();
}
} // namespace Misc::General
