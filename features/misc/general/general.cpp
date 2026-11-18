#include "general.hpp"

namespace Misc::General {
void Publish(const Settings& settings) {
    Radar::SetEnabled(settings.radar);
    ClanTag::SetEnabled(settings.clantag);
    Scoreboard::Publish(settings.scoreboard, settings.scoreboard_scale_x,
                        settings.scoreboard_scale_y, settings.scoreboard_scale_weapon);
}

void FrameStage(int stage) {
    Radar::FrameStage(stage);
    ClanTag::FrameStage(stage);
    Scoreboard::FrameStage(stage);
}

void Shutdown() { ClanTag::Shutdown(); }
} // namespace Misc::General
