#include "misc_events.hpp"
#include "../../../context.hpp"
#include "../../../sdk/interfaces/interfaces.hpp"
#include "../../../utils/log.hpp"
#include "../../../utils/memory.hpp"
#include "../general/general.hpp"
#include "../movement/movement.hpp"
#include "../buybot/buybot.hpp"
#include <cstring>

namespace Misc::Events {
namespace {
// The engine checks GetEventDebugID() against EVENT_DEBUG_ID_INIT (42) before
// dispatching every fired event. Any other value makes it print
// "GameEventListener2 callback in list that should NOT be - <event>!" for all
// events and, worse, it never calls FireGameEvent at all.
constexpr int kEventDebugIdInit = 42;

// The per-event (local) listener interface. Layout must match the engine's
// IGameEventListener2: 0 destructor, 1 FireGameEvent, 2 GetEventDebugID. There
// is no IsEnabled - that only exists on the global IGameEventListeners2.
class LocalGameEventListener {
public:
    virtual ~LocalGameEventListener() = default;
    virtual void FireGameEvent(IGameEvent* event) = 0;
    virtual int GetEventDebugID() = 0;
};

// Registered PER-EVENT (not globally). A global listener sits in the engine's
// global list, which is probed on every single fired event; when the debug id
// was wrong that flooded the console with "should NOT be" for every shot and
// impact. Listening only for the few events below keeps this out of that list
// entirely, so the engine checks this listener only when one of them fires.
class MiscEventListener : public LocalGameEventListener {
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
    int GetEventDebugID() override { return kEventDebugIdInit; }
};

// The only events the misc features consume.
constexpr const char* kEvents[] = {
    "round_start", "player_hurt", "player_death", "item_purchase", "player_say",
};

MiscEventListener* g_listener = nullptr;
} // namespace

bool Init() {
    if (!Interfaces::GameEvent) {
        LOG_WARN("misc events: IGameEventManager2 not resolved; event-driven features off");
        return false;
    }
    auto add = MEM::GetVFunc<bool (__thiscall*)(void*, LocalGameEventListener*, const char*, bool)>(Interfaces::GameEvent, 3);
    if (!add) {
        LOG_WARN("misc events: AddListener vfunc not resolved");
        return false;
    }
    g_listener = new MiscEventListener();
    bool any = false;
    __try {
        for (const char* event : kEvents)
            if (add(Interfaces::GameEvent, g_listener, event, false)) any = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    if (!any) {
        LOG_WARN("misc events: per-event game-event listener registration failed");
        delete g_listener;
        g_listener = nullptr;
        return false;
    }
    LOG_INFO("misc events: per-event game-event listener registered");
    return true;
}

void Shutdown() {
    if (g_listener && Interfaces::GameEvent) {
        auto remove = MEM::GetVFunc<void (__thiscall*)(void*, LocalGameEventListener*)>(Interfaces::GameEvent, 5);
        if (remove) {
            __try { remove(Interfaces::GameEvent, g_listener); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }
    delete g_listener;
    g_listener = nullptr;
}
} // namespace Misc::Events
