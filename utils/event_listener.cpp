#include "event_listener.hpp"
#include "memory.hpp"
#include "log.hpp"
#include "../sdk/interfaces/interfaces.hpp"
#include <string>
#include <vector>
namespace EventListener {
static Handler handler = nullptr;

class listener_t final : public IGameEventListener2 {
public:
    // "bullet_impact" fires on every bullet and "weapon_fire" on every shot, so logging each
    // arrival made the file useless. The first arrival of each event is the useful fact -
    // it proves the listener is registered and that the game delivers that event at all -
    // and repeats tell nothing new, so they stay out of the log.
    void FireGameEvent(IGameEvent* event) override {
        const char* name = event ? event->GetName() : nullptr;
        if (!name) return;
        // The feature handler runs for every arrival, before the once-per-name logging below.
        if (handler) handler(event);
        for (const auto& seen : logged) {
            if (seen == name) return;
        }
        logged.emplace_back(name);
        LOG_INFO("game event: %s (first seen; later arrivals are not logged)", name);
    }
    int GetEventDebugID() override { return 42; }
private:
    std::vector<std::string> logged;
} listener;
static std::vector<std::string> names;
static bool registered = false;
bool Setup(std::initializer_list<const char*> event_names) {
    if (!Interfaces::GameEvent) return false;
    using AddListener = bool (__thiscall*)(void*, IGameEventListener2*, const char*, bool);
    auto add = MEM::GetVFunc<AddListener>(Interfaces::GameEvent, 3);
    if (!add) return false;
    for (auto* name : event_names) {
        if (name && *name && add(Interfaces::GameEvent, &listener, name, false)) names.emplace_back(name);
    }
    registered = !names.empty();
    return registered;
}
void SetHandler(Handler new_handler) { handler = new_handler; }
void Destroy() {
    if (!registered || !Interfaces::GameEvent) return;
    using RemoveListener = void (__thiscall*)(void*, IGameEventListener2*);
    if (auto remove = MEM::GetVFunc<RemoveListener>(Interfaces::GameEvent, 5)) remove(Interfaces::GameEvent, &listener);
    registered = false; names.clear(); handler = nullptr;
}
}
