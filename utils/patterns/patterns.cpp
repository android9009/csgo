#include "patterns.hpp"
#include "../memory.hpp"
#include "../log.hpp"
#include <cstring>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>
#include <windows.h>

namespace Patterns {
static std::unordered_map<std::string, std::uintptr_t> s_values;
static std::vector<std::string_view> s_missing;
static std::vector<Definition> s_definitions;

const std::vector<Definition>& Definitions() {
    if (s_definitions.empty()) {
        s_definitions.reserve(std::size(kLegacySnapshotCatalog) + std::size(kPortedCatalog) +
                              std::size(kReferenceCatalog));
        s_definitions.insert(s_definitions.end(), std::begin(kLegacySnapshotCatalog), std::end(kLegacySnapshotCatalog));
        s_definitions.insert(s_definitions.end(), std::begin(kPortedCatalog), std::end(kPortedCatalog));
        s_definitions.insert(s_definitions.end(), std::begin(kReferenceCatalog), std::end(kReferenceCatalog));
    }
    return s_definitions;
}
const std::vector<std::string_view>& Missing() { return s_missing; }

std::uintptr_t Get(std::string_view name) {
    auto it = s_values.find(std::string(name));
    return it == s_values.end() ? 0u : it->second;
}

static bool ReadTargetU32(std::uintptr_t address, std::uint32_t& out) {
    if (!address) return false;
#if defined(_MSC_VER)
    __try { out = *reinterpret_cast<const std::uint32_t*>(address); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { out = 0; return false; }
#else
    std::memcpy(&out, reinterpret_cast<const void*>(address), sizeof(out));
    return true;
#endif
}

bool Init() {
    s_values.clear(); s_missing.clear();
    std::size_t payloads = 0;
    for (const auto& def : Definitions()) {
        const auto match = MEM::FindPattern(def.module, def.signature);
        if (!match) { s_missing.push_back(def.name); continue; }
        const auto module_base = reinterpret_cast<std::uintptr_t>(MEM::GetModuleBase(def.module));
        std::uintptr_t value = 0;
        if (!Resolve(def, match, module_base, &ReadTargetU32, value)) {
            s_missing.push_back(def.name);
            continue;
        }
        s_values.emplace(std::string(def.name), value);
        if (!def.call_target && def.offset_count) ++payloads; // dereferenced global/offset
    }
    const auto total = Definitions().size();
    LOG_INFO("patterns: %zu definitions, %zu resolved, %zu missing (%zu dereferenced)",
             total, s_values.size(), s_missing.size(), payloads);
    // Core pointers required for a usable base. Optional signatures can remain unavailable.
    if (!s_missing.empty()) {
        // The names, not just the count: a feature that stays off because its signature
        // moved is otherwise indistinguishable from one that is simply unused.
        std::string names;
        for (const auto& name : s_missing) {
            if (!names.empty()) names += ", ";
            names += name;
        }
        LOG_WARN("%zu pattern(s) not found; pinned fallback offsets will be used where available: %s",
                 s_missing.size(), names.c_str());
    }
    return MEM::GetModuleBase("client.dll") && MEM::GetModuleBase("engine.dll");
}
} // namespace Patterns
