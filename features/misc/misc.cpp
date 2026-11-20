#include "misc.hpp"
#include "buybot/buybot.hpp"
#include "general/general.hpp"
#include "other/other.hpp"
#include "../../sdk/interfaces/interfaces.hpp"
#include "../../utils/log.hpp"
#include <windows.h>

namespace Misc {
namespace {
// One stale read stops the whole runtime for the session instead of throwing on
// every frame, so the log is not flooded and the game is not destabilised.
bool g_failed{};

void FrameStageImpl(int stage) {
    General::FrameStage(stage);
    Other::FrameStage(stage);
}
} // namespace

void FrameStage(int stage) {
    if (g_failed) return;
    if (!Interfaces::Engine || !Interfaces::EntityList) return;
    // SEH: entity and netvar pointers belong to the game and can go stale between
    // frames. The per-feature functions hold no objects that need unwinding.
    __try { FrameStageImpl(stage); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        g_failed = true;
        LOG_ERROR("misc: frame-stage exception 0x%08lX; misc runtime stopped", GetExceptionCode());
    }
}

void Shutdown() {
    __try { Other::Shutdown(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        LOG_ERROR("misc: exception while removing the OTHER hooks during unload");
    }
    __try { Buybot::Shutdown(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        LOG_ERROR("misc: exception while resetting the buy bot at shutdown");
    }
    __try { General::Shutdown(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        LOG_ERROR("misc: exception while restoring the clan tag at shutdown");
    }
}
} // namespace Misc
