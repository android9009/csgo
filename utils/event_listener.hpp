#pragma once
#include <windows.h>
#include <initializer_list>
#include <string>
#include <vector>
// The published IGameEvent layout: slot 0 is the destructor, 1 GetName, 2 IsReliable,
// 3 IsLocal, 4 IsEmpty, 5 GetBool, 6 GetInt, 7 GetUint64, 8 GetFloat, 9 GetString. The
// listener below has always used GetName at 1 and the events it logged arrived with the right
// names, so the rest of the layout is the same one the SDK documents.
class IGameEvent {
public:
    const char* GetName() {
        using Fn = const char* (__thiscall*)(void*);
        return reinterpret_cast<Fn>((*reinterpret_cast<void***>(this))[1])(this);
    }
    bool GetBool(const char* key, bool fallback = false) {
        using Fn = bool (__thiscall*)(void*, const char*, bool);
        return reinterpret_cast<Fn>((*reinterpret_cast<void***>(this))[5])(this, key, fallback);
    }
    int GetInt(const char* key, int fallback = 0) {
        using Fn = int (__thiscall*)(void*, const char*, int);
        return reinterpret_cast<Fn>((*reinterpret_cast<void***>(this))[6])(this, key, fallback);
    }
    float GetFloat(const char* key, float fallback = 0.f) {
        using Fn = float (__thiscall*)(void*, const char*, float);
        return reinterpret_cast<Fn>((*reinterpret_cast<void***>(this))[8])(this, key, fallback);
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
    // One consumer for the single listener the module registers: the features that need the
    // events (Visuals -> Misc -> Other today) install their handler here instead of adding a
    // second listener and a second registration.
    using Handler = void (*)(IGameEvent*);
    void SetHandler(Handler handler);
}
