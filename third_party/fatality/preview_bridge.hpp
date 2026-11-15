#pragma once
#include <atomic>
namespace FatalityPreviewBridge {
inline std::atomic<bool> visible{false};
inline std::atomic<bool> skins_selecting{false};
inline std::atomic<float> yaw{180.f};
inline std::atomic<float> distance{310.f};
inline std::atomic<void*> texture{nullptr};
}
