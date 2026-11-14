#pragma once
#include "recv.hpp"
using CreateClientClassFn = void* (*)(int, int);
using CreateEventFn = void* (*)();
struct ClientClass {
    CreateClientClassFn m_pCreateFn;
    CreateEventFn m_pCreateEventFn;
    const char* m_pNetworkName;
    RecvTable* m_pRecvTable;
    ClientClass* m_pNext;
    int m_ClassID;
};
