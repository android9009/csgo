#include "general.hpp"

namespace Misc::General {
void Publish(const Settings& settings) {
    Radar::SetEnabled(settings.radar);
    ClanTag::SetEnabled(settings.clantag);
    Chat::SetEnabled(settings.reveal_enemy_chat);
    Ping::Publish(settings.ping_spike, settings.ping_spike_ms);
}

void FrameStage(int stage) {
    Chat::FrameStage(stage);
    Ping::FrameStage(stage);
    Radar::FrameStage(stage);
    ClanTag::FrameStage(stage);
}

void Shutdown() {
    Ping::Shutdown();
    Chat::Shutdown();
    ClanTag::Shutdown();
}
} // namespace Misc::General
