#pragma once
#include <windows.h>
#include <initializer_list>
#include <string>
#include <vector>
class IGameEvent {
public:
    const char* GetName() {
        using Fn = const char* (__thiscall*)(void*);
        return reinterpret_cast<Fn>((*reinterpret_cast<void***>(this))[1])(this);
    }
};
class IGameEventListener2 {
public:
    virtual ~IGameEventListener2() = default;
    virtual void FireGameEvent(IGameEvent* event) = 0;
    virtual int GetEventDebugID() = 0;
};
namespace EventListener {
    bool Setup(std::initializer_list<const char*> names);
    void Destroy();
}
