#pragma once
#include <atomic>
namespace FatalityPreviewBridge {
inline std::atomic<bool> visible{false};
inline std::atomic<bool> skins_selecting{false};
// Set by the visible viewport and CT/T selector; Source render hook consumes it.
inline std::atomic<bool> skins_team_ct{true};
inline std::atomic<bool> render_ct{true};
inline std::atomic<float> yaw{180.f};
inline std::atomic<float> distance{310.f};
inline std::atomic<void*> texture{nullptr};
}
