#pragma once
// In CS:GO (Source 1) fields are exposed through RecvTables/datamaps, not Source 2 SchemaSystem.
// This small facade keeps feature code independent of how a field was resolved.
#include <string_view>
#include <cstddef>
#include "netvars.hpp"
namespace SDK {
    enum class FieldSource { RecvTable, DataMap, StaticOffset };
    struct FieldSchema {
        std::string_view name;
        std::ptrdiff_t offset;
        FieldSource source;
        bool valid() const { return offset > 0; }
    };
    inline FieldSchema NetField(std::string_view table, std::string_view name) {
        return {name, PropManager::Get().GetOffset(table, name), FieldSource::RecvTable};
    }
    inline FieldSchema DataField(datamap_t* map, std::string_view name) {
        return {name, PropManager::Get().GetDataMapOffset(map, name), FieldSource::DataMap};
    }
}
