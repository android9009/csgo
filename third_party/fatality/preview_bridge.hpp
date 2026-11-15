#pragma once
#include <atomic>
namespace FatalityPreviewBridge {
inline std::atomic<bool> visible{false};
inline std::atomic<float> yaw{180.f};
inline std::atomic<float> pitch{0.f};
inline std::atomic<float> distance{329.f};
inline std::atomic<void*> texture{nullptr};
}
