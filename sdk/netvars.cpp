#include "netvars.hpp"
#include "interfaces/interfaces.hpp"
#include <string>
#include <algorithm>

PropManager& PropManager::Get() {
    static PropManager instance;
    return instance;
}
bool PropManager::Create() {
    if (!Interfaces::Client) return false;
    classes = Interfaces::Client->GetAllClasses();
    ready = classes != nullptr;
    return ready;
}
void PropManager::Destroy() {
    std::lock_guard<std::mutex> lock(mutex);
    cache.clear(); classes = nullptr; ready = false;
}
bool PropManager::WalkTable(RecvTable* table, std::string_view prop, std::ptrdiff_t accumulated,
                            std::ptrdiff_t& out) const {
    if (!table || !table->m_pProps) return false;
    for (int i = 0; i < table->m_nProps; ++i) {
        const RecvProp& item = table->m_pProps[i];
        if (!item.m_pVarName || item.m_pVarName[0] == '0') continue;
        const auto next = accumulated + item.m_Offset;
        if (prop == item.m_pVarName) { out = next; return true; }
        if (item.m_pDataTable && WalkTable(item.m_pDataTable, prop, next, out)) return true;
    }
    return false;
}
bool PropManager::WalkDataMap(datamap_t* map, std::string_view field,
                              std::ptrdiff_t accumulated, std::ptrdiff_t& out) const {
    if (!map) return false;
    for (int i = 0; i < map->dataNumFields; ++i) {
        const auto& item = map->dataDesc[i];
        if (item.fieldName && field == item.fieldName) {
            out = accumulated + item.fieldOffset[0]; return true;
        }
        if (item.td && WalkDataMap(item.td, field, accumulated + item.fieldOffset[0], out)) return true;
    }
    return WalkDataMap(map->baseMap, field, accumulated, out);
}
std::ptrdiff_t PropManager::GetOffset(std::string_view table, std::string_view prop) {
    if (!ready || table.empty() || prop.empty()) return 0;
    std::string key(table); key.push_back('\x1f'); key.append(prop);
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;
    }
    std::ptrdiff_t result = 0;
    bool found = false;
    for (auto* cls = classes; cls && !found; cls = cls->m_pNext) {
        auto* recv = cls->m_pRecvTable;
        if (recv && recv->m_pNetTableName && table == recv->m_pNetTableName)
            found = WalkTable(recv, prop, 0, result);
    }
    if (!found) return 0;
    std::lock_guard<std::mutex> lock(mutex);
    cache.emplace(std::move(key), result);
    return result;
}
std::ptrdiff_t PropManager::GetDataMapOffset(datamap_t* map, std::string_view field) const {
    std::ptrdiff_t result = 0;
    return WalkDataMap(map, field, 0, result) ? result : 0;
}
