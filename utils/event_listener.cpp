#include "event_listener.hpp"
#include "memory.hpp"
#include "log.hpp"
#include "../sdk/interfaces/interfaces.hpp"
#include <string>
namespace EventListener {
class listener_t final : public IGameEventListener2 {
public:
    void FireGameEvent(IGameEvent* event) override {
        if (event && event->GetName()) LOG_INFO("game event: %s", event->GetName());
    }
    int GetEventDebugID() override { return 42; }
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
void Destroy() {
    if (!registered || !Interfaces::GameEvent) return;
    using RemoveListener = void (__thiscall*)(void*, IGameEventListener2*);
    if (auto remove = MEM::GetVFunc<RemoveListener>(Interfaces::GameEvent, 5)) remove(Interfaces::GameEvent, &listener);
    registered = false; names.clear();
}
}
