#pragma once
// The engine's game-event objects and listener interface, as a minimal vtable subset.
// Slot numbering is the published Source IGameEvent / IGameEventListeners2 /
// IGameEventManager2 layout - the same table `main` reads (structs.hpp c_game_event,
// interfaces.hpp c_game_event_listener2 / c_game_event_manager2).
//
//   IGameEvent:            0 ~  1 get_name  2 is_reliable 3 is_local  4 is_empty
//                          5 get_bool  6 get_int  7 get_uint64  8 get_float  9 get_string
//   IGameEventListeners2:  0 ~  1 FireGameEvent  2 IsEnabled  3 GetEventDebugId
//   IGameEventManager2:    0 ~  1 load_events  2 reset  3 add_listener  4 find_listener
//                          5 remove_listener  6 add_listener_global  7 create_event
//                          8 fire_event  9 fire_event_client_side

// IGameEvent (main structs.hpp c_game_event). Only the getters the misc features use.
// The getters are const, but the vtable read and the `this` the engine expects are
// non-const, so both go through one const_cast (an implicit const-strip would not
// compile, and a raw reinterpret_cast on the qualifier is implementation-defined).
class IGameEvent {
public:
    const char* GetName() const {
        using Fn = const char* (__thiscall*)(void*);
        auto self = const_cast<IGameEvent*>(this);
        auto fn = reinterpret_cast<Fn>((*reinterpret_cast<void***>(self))[1]);
        return fn ? fn(self) : nullptr;
    }
    int GetInt(const char* key, int def = 0) const {
        using Fn = int (__thiscall*)(void*, const char*, int);
        auto self = const_cast<IGameEvent*>(this);
        auto fn = reinterpret_cast<Fn>((*reinterpret_cast<void***>(self))[6]);
        return fn ? fn(self, key, def) : def;
    }
    const char* GetString(const char* key, const char* def = "") const {
        using Fn = const char* (__thiscall*)(void*, const char*, const char*);
        auto self = const_cast<IGameEvent*>(this);
        auto fn = reinterpret_cast<Fn>((*reinterpret_cast<void***>(self))[9]);
        return fn ? fn(self, key, def) : def;
    }
};

// IGameEventListeners2 - what the engine calls for every fired event. Declared as a real
// vtable class so the three slots line up; the manager is never given a pointer to it
// unless AddListenerGlobal succeeded.
class IGameEventListeners2 {
public:
    virtual ~IGameEventListeners2() {}
    virtual void FireGameEvent(IGameEvent* event) = 0;
    virtual bool IsEnabled() = 0;
    virtual int GetEventDebugId() = 0;
};

// IGameEventManager2 - only the listener registration slots are used.
class IGameEventManager2 {
public:
    bool AddListenerGlobal(IGameEventListeners2* listener, bool server_side) {
        using Fn = bool (__thiscall*)(void*, IGameEventListeners2*, bool);
        auto fn = reinterpret_cast<Fn>((*reinterpret_cast<void***>(this))[6]);
        return fn ? fn(this, listener, server_side) : false;
    }
    void RemoveListener(IGameEventListeners2* listener) {
        using Fn = void (__thiscall*)(void*, IGameEventListeners2*);
        auto fn = reinterpret_cast<Fn>((*reinterpret_cast<void***>(this))[5]);
        if (fn) fn(this, listener);
    }
};
