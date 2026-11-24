#include "force_crosshair.hpp"
#include <mutex>

namespace ForceCrosshair {
namespace {
Settings g_snapshot;
std::mutex g_mutex;
} // namespace

void Publish(const Settings& s) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_snapshot = s;
}

Settings Snapshot() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_snapshot;
}

void Contribute(VisualRuntime::FxFrame& frame) {
    if (!Snapshot().enabled) return;
    if (frame.line_count + 4 > static_cast<int>(frame.lines.size())) return;
    for (const auto& line : Detail::Arms())
        frame.lines[frame.line_count++] = line;
}
} // namespace ForceCrosshair
