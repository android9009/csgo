#include "misc_events.hpp"
#include "../../../context.hpp"
#include "../../../sdk/interfaces/interfaces.hpp"
#include "../../../utils/log.hpp"
#include "../general/general.hpp"
#include "../movement/movement.hpp"
#include "../buybot/buybot.hpp"
#include <cstring>

namespace Misc::Events {
namespace {
// The engine checks GetEventDebugID() against EVENT_DEBUG_ID_INIT (42) before
// dispatching every fired event. Any other value makes it print
// "GameEventListener2 callback in list that should NOT be - <event>!" for all
// events (the console spam on every skin selection) and, worse, it never calls
// FireGameEvent at all - the event-driven features below were dead while this
// was anything but 42.
constexpr int kEventDebugIdInit = 42;

class MiscEventListener : public IGameEventListeners2 {
public:
    void FireGameEvent(IGameEvent* event) override {
        // The handlers are display/log state; a bad event read must not take
        // the game down from inside the engine's dispatch.
        __try {
            if (!event) return;
            const char* name = event->GetName();
            if (!name) return;

            if (!std::strcmp(name, "round_start")) {
                Buybot::OnRoundStart();
                Movement::Reset();
                General::OnRoundStart();
            } else if (!std::strcmp(name, "player_hurt")) {
                General::OnPlayerHurt(event);
            } else if (!std::strcmp(name, "player_death")) {
                General::OnPlayerDeath(event);
            } else if (!std::strcmp(name, "item_purchase")) {
                General::OnItemPurchase(event);
            } else if (!std::strcmp(name, "player_say")) {
                General::OnPlayerSay(event);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    bool IsEnabled() override { return true; }
    int GetEventDebugId() override { return kEventDebugIdInit; }
};

MiscEventListener* g_listener = nullptr;
} // namespace

bool Init() {
    if (!Interfaces::GameEvent) {
        LOG_WARN("misc events: IGameEventManager2 not resolved; event-driven features off");
        return false;
    }
    g_listener = new MiscEventListener();
    __try {
        if (Interfaces::GameEvent->AddListenerGlobal(g_listener, false)) {
            LOG_INFO("misc events: global game-event listener registered");
            return true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LOG_WARN("misc events: AddListenerGlobal raised an exception");
    }
    delete g_listener;
    g_listener = nullptr;
    return false;
}

void Shutdown() {
    if (g_listener && Interfaces::GameEvent) {
        __try { Interfaces::GameEvent->RemoveListener(g_listener); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    delete g_listener;
    g_listener = nullptr;
}
} // namespace Misc::Events
