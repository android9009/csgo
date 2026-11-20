#include "hooks.hpp"
#include "../datatypes/c_user_cmd.hpp"
#include "../interfaces/interfaces.hpp"
#include "../../features/misc/movement/movement.hpp"
#include "../../utils/log.hpp"

namespace HooksInternal {
// ClientMode::CreateMove - vtable slot 24 (main: CREATE_MOVE_IDX = 24).
//
// The original is called first, exactly like `main` does, so the game has already written
// its own command (buttons, movement values, angles). Everything the movement module does
// afterwards is a correction of that command, never a replacement of the input: bhop only
// adds or removes IN_JUMP, the strafe only rewrites forwardmove/sidemove, quick stop only
// writes when no movement button is held.
bool __fastcall CreateMove(void* ecx, void* edx, float input_sample_time, void* cmd) {
    const auto original = Hooks::GetCreateMoveOriginal();
    if (!original) return false;
    const bool result = original(ecx, edx, input_sample_time, cmd);
    auto* command = static_cast<CUserCmd*>(cmd);
    if (command) {
        __try { Misc::Movement::CreateMove(command); }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            static bool logged = false;
            if (!logged) {
                logged = true;
                LOG_ERROR("movement: exception on the create-move hook 0x%08lX; input untouched from now on",
                          GetExceptionCode());
            }
        }
    }
    return result;
}
} // namespace HooksInternal
