#include "other.hpp"
#include "../../../utils/log.hpp"

namespace Misc::Other {
namespace {
bool g_reported_pure{};
bool g_reported_cheats{};
} // namespace

void Publish(const Settings& settings) {
    Accept::SetEnabled(settings.auto_accept);
    ConsoleFilter::SetEnabled(settings.console_filter);

    // sv_pure and sv_cheats are menu entries without an implementation: reaching a ConVar
    // needs either a verified ConVar layout (the client's ICvar is only forward-declared in
    // this SDK) or a filesystem vtable index, and neither is documented well enough to ship
    // as a wild call. They are reported once instead of silently doing nothing.
    if (settings.sv_pure && !g_reported_pure) {
        g_reported_pure = true;
        LOG_WARN("other: Bypass SV Pure is not implemented in this build (needs a verified "
                 "IBaseFileSystem hook; see docs/misc-runtime.md)");
    }
    if (settings.sv_cheats && !g_reported_cheats) {
        g_reported_cheats = true;
        LOG_WARN("other: Bypass SV Cheats is not implemented in this build (needs a verified "
                 "ConVar write path; see docs/misc-runtime.md)");
    }
}

void FrameStage(int stage) {
    Accept::FrameStage(stage);
    ConsoleFilter::FrameStage(stage);
}

void Shutdown() {
    Accept::Shutdown();
    ConsoleFilter::Shutdown();
}
} // namespace Misc::Other
