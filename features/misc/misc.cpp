#include "misc.hpp"
#include "../../context.hpp"
#include "../../sdk/datatypes/c_user_cmd.hpp"
#include "../../sdk/hooks/hooks.hpp"
#include "../../utils/log.hpp"
#include "buybot/buybot.hpp"
#include "events/misc_events.hpp"
#include "general/general.hpp"
#include "movement/movement.hpp"
#include "other/other.hpp"

namespace Misc {
namespace {
bool g_initialized = false;
} // namespace

bool Init() {
    if (g_initialized) return true;
    g_initialized = true;

    // The event listener drives the buy bot and the hurt/naded/buy logs + hit sounds.
    Events::Init();
    // The ping spike needs the two engine network detours; it stays off if they don't resolve.
    General::Detail::PingInstall();
    LOG_INFO("misc runtime: initialised (features wait for the menu to publish settings)");
    return true;
}

void FrameStage(int stage) {
    // SEH lives in the frame-stage hook; this only dispatches to the per-tab frame stages.
    General::FrameStage(stage);
    Other::FrameStage(stage);
}

void CreateMove(CUserCmd* cmd) {
    Movement::CreateMove(cmd);
    Buybot::Tick();
}

void Shutdown() {
    __try {
        Buybot::Shutdown();
        Movement::Shutdown();
        General::Shutdown();
        Other::Shutdown();
        General::Detail::PingRemove();
        Events::Shutdown();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LOG_ERROR("misc: exception during shutdown 0x%08lX", GetExceptionCode());
    }
    g_initialized = false;
}
} // namespace Misc

// The CreateMove VMT hook body. The original first builds the command from the user input;
// the misc movement assist then edits that command in place, and the buy bot issues its
// latched purchase. The client simulates the (edited) command after the hook returns.
namespace HooksInternal {
bool __fastcall CreateMove(void* ecx, void* edx, float input_sample_time, void* cmd) {
    auto original = Hooks::GetCreateMoveOriginal();
    const bool created = original ? original(ecx, edx, input_sample_time, cmd) : false;
    if (created) {
        ctx.in_createmove = true;
        __try {
            Misc::CreateMove(static_cast<CUserCmd*>(cmd));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            static bool logged = false;
            if (!logged) { logged = true; LOG_ERROR("misc: CreateMove raised 0x%08lX", GetExceptionCode()); }
        }
        ctx.in_createmove = false;
    }
    return created;
}
} // namespace HooksInternal
