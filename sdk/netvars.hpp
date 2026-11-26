#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <mutex>
#include "clientclass.hpp"
#include "datamap.hpp"

class PropManager {
public:
    static PropManager& Get();
    bool Create();
    void Destroy();
    std::ptrdiff_t GetOffset(std::string_view table, std::string_view prop);
    std::ptrdiff_t GetDataMapOffset(datamap_t* map, std::string_view field) const;
    bool IsReady() const { return ready; }
private:
    bool WalkTable(RecvTable* table, std::string_view prop, std::ptrdiff_t accumulated, std::ptrdiff_t& out) const;
    bool WalkDataMap(datamap_t* map, std::string_view field, std::ptrdiff_t accumulated, std::ptrdiff_t& out) const;
    ClientClass* classes = nullptr;
    bool ready = false;
    std::unordered_map<std::string, std::ptrdiff_t> cache;
    mutable std::mutex mutex;
};

// Typed accessors cache a resolved RecvProp offset once the client classes are available.
#define NETVAR(type, method, table, prop) \
    type& method() { const auto _offset = PropManager::Get().GetOffset(table, prop); \
        return *reinterpret_cast<type*>(reinterpret_cast<std::uintptr_t>(this) + _offset); } \
    const type& method() const { const auto _offset = PropManager::Get().GetOffset(table, prop); \
        return *reinterpret_cast<const type*>(reinterpret_cast<std::uintptr_t>(this) + _offset); }
#define NETVAR_OFFSET(type, method, offset_value) \
    type& method() { return *reinterpret_cast<type*>(reinterpret_cast<std::uintptr_t>(this) + (offset_value)); } \
    const type& method() const { return *reinterpret_cast<const type*>(reinterpret_cast<std::uintptr_t>(this) + (offset_value)); }

#define NETVAR_FALLBACK(type, method, table, prop, fallback_offset) \
    type& method() { static const std::ptrdiff_t _offset = []() { const auto found = PropManager::Get().GetOffset(table, prop); return found ? found : static_cast<std::ptrdiff_t>(fallback_offset); }(); \
        return *reinterpret_cast<type*>(reinterpret_cast<std::uintptr_t>(this) + _offset); } \
    const type& method() const { static const std::ptrdiff_t _offset = []() { const auto found = PropManager::Get().GetOffset(table, prop); return found ? found : static_cast<std::ptrdiff_t>(fallback_offset); }(); \
        return *reinterpret_cast<const type*>(reinterpret_cast<std::uintptr_t>(this) + _offset); }
