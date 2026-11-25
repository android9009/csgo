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
static std::vector<std::string_view> s_resolved;

// Base-name grouping lives in catalog.hpp (Patterns::AlternativeBase), where the portable
// tests pin it.
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
    s_values.clear(); s_missing.clear(); s_resolved.clear();
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
        s_resolved.push_back(def.name);
        if (!def.call_target && def.offset_count) ++payloads; // dereferenced global/offset
    }
    const auto total = Definitions().size();
    LOG_INFO("patterns: %zu definitions, %zu resolved, %zu unmatched (%zu dereferenced)",
             total, s_values.size(), s_missing.size(), payloads);
    // A signature that did not match is only interesting when nothing else covers it: the
    // catalogue carries alternative spellings of the same function ("_alt1", "_alt2"), and
    // one of them matching is the normal case on any given build. Reporting them as a
    // failure would train the reader to ignore the log.
    std::string alternatives;
    std::string unresolved;
    std::vector<std::string_view> reported; // one line per group, not one per spelling
    for (const auto& name : s_missing) {
        const auto base = Patterns::AlternativeBase(name);
        std::string_view matched;
        for (std::string_view sibling : s_resolved) {
            if (Patterns::AlternativeBase(sibling) == base) { matched = sibling; break; }
        }
        if (!matched.empty()) {
            bool already = false;
            for (const auto& seen : reported) if (seen == base) already = true;
            if (!already) {
                reported.push_back(base);
                if (!alternatives.empty()) alternatives += ", ";
                alternatives += std::string(base) + " (via " + std::string(matched) + ")";
            }
            continue;
        }
        if (!unresolved.empty()) unresolved += ", ";
        unresolved += name;
    }
    if (!alternatives.empty())
        LOG_INFO("patterns: matched through an alternative spelling: %s", alternatives.c_str());
    // The remaining names are reported by their owner, not here: a feature that stays off
    // because of them says so itself, with the consequence spelled out.
    if (unresolved.empty())
        LOG_INFO("patterns: every signature has a match");
    else
        LOG_INFO("patterns: no match, owner decides: %s", unresolved.c_str());
    return MEM::GetModuleBase("client.dll") && MEM::GetModuleBase("engine.dll");
}
} // namespace Patterns
