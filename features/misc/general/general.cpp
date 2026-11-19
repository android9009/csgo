#include "general.hpp"

namespace Misc::General {
void Publish(const Settings& settings) {
    Radar::SetEnabled(settings.radar);
    ClanTag::SetEnabled(settings.clantag);
    Scoreboard::Publish(settings.scoreboard, settings.scoreboard_scale_x,
                        settings.scoreboard_scale_y, settings.scoreboard_scale_weapon);
    Chat::SetEnabled(settings.reveal_enemy_chat);
    Ragdoll::Publish(settings.ragdoll_thrust, settings.ragdoll_percent);
    Ping::Publish(settings.ping_spike, settings.ping_spike_ms);
}

void FrameStage(int stage) {
    Chat::FrameStage(stage);
    Ping::FrameStage(stage);
    Radar::FrameStage(stage);
    ClanTag::FrameStage(stage);
    Scoreboard::FrameStage(stage);
    Ragdoll::FrameStage(stage);
}

void Shutdown() {
    Ping::Shutdown();
    Chat::Shutdown();
    ClanTag::Shutdown();
}
} // namespace Misc::General
