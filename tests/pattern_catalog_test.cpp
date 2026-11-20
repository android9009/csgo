// Portable checks for the signature catalogue: data integrity plus the resolution
// arithmetic. No Windows headers are involved, so this runs on any host.
#include "utils/patterns/catalog.hpp"
#include <cassert>
#include <cstring>
#include <iterator>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {
using Patterns::Definition;
using Patterns::Read32;

constexpr std::string_view kKnownModules[] = {
    "client.dll", "engine.dll", "materialsystem.dll", "server.dll",
    "shaderapidx9.dll", "gameoverlayrenderer.dll", "tier0.dll", "datacache.dll",
    "filesystem_stdio.dll", "vstdlib.dll", "studiorender.dll", "vguimatsurface.dll",
    "vphysics.dll", "vgui2.dll", "localize.dll",
};

bool KnownModule(std::string_view module) {
    for (const auto candidate : kKnownModules)
        if (candidate == module) return true;
    return false;
}

bool ValidSignature(std::string_view signature) {
    std::size_t tokens = 0;
    std::size_t index = 0;
    while (index < signature.size()) {
        while (index < signature.size() && signature[index] == ' ') ++index;
        if (index >= signature.size()) break;
        const auto begin = index;
        while (index < signature.size() && signature[index] != ' ') ++index;
        const auto token = signature.substr(begin, index - begin);
        if (token != "?" && token != "??") {
            if (token.size() != 2) return false;
            for (const auto c : token) {
                const bool hex = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
                if (!hex) return false;
            }
        }
        ++tokens;
    }
    return tokens > 0;
}

template <std::size_t N>
void Validate(const Definition (&catalog)[N], std::set<std::string_view>& names) {
    for (const auto& definition : catalog) {
        assert(!definition.name.empty());
        assert(!definition.module.empty());
        assert(KnownModule(definition.module));
        assert(ValidSignature(definition.signature));
        assert(definition.offset_count <= 4);
        for (unsigned i = 0; i < definition.offset_count; ++i) {
            assert(definition.offsets[i] >= 0);
            assert(definition.offsets[i] < 0x1000);
        }
        if (definition.call_target) assert(definition.offsets[0] > 0);
        assert(definition.extra > -0x1000 && definition.extra < 0x1000);
        // Names are the lookup key, so they must be globally unique.
        const bool inserted = names.insert(definition.name).second;
        assert(inserted);
        (void)inserted;
    }
}

// Deterministic stand-in for the target process memory, mapped at 0x2000.
constexpr std::uintptr_t kBase = 0x2000;
unsigned char g_memory[16]{};
std::uintptr_t g_last_read = 0;
bool FakeRead32(std::uintptr_t address, std::uint32_t& out) {
    g_last_read = address;
    const auto offset = address - kBase;
    if (offset > sizeof(g_memory) - sizeof(out)) return false;
    std::memcpy(&out, g_memory + offset, sizeof(out));
    return true;
}
void WriteMemory(std::uintptr_t address, std::uint32_t value) {
    std::memcpy(g_memory + (address - kBase), &value, sizeof(value));
}
bool FailingRead32(std::uintptr_t, std::uint32_t&) { return false; }

Patterns::Definition MakeDefinition(int offset, unsigned char count, bool relative,
                                    std::ptrdiff_t extra, bool call_target) {
    Patterns::Definition definition{};
    definition.name = "test";
    definition.module = "client.dll";
    definition.signature = "90";
    definition.offsets[0] = offset;
    definition.offset_count = count;
    definition.relative = relative;
    definition.extra = extra;
    definition.call_target = call_target;
    return definition;
}
} // namespace

int main() {
    // ---- alternative spellings share a base name -----------------------------------
    {
        using Patterns::AlternativeBase;
        assert(AlternativeBase("server_rank_reveal_all_alt1") == "server_rank_reveal_all");
        assert(AlternativeBase("server_rank_reveal_all_alt2") == "server_rank_reveal_all");
        assert(AlternativeBase("send_datagram_alt") == "send_datagram");
        // A name of its own, and a suffix that is not a numbered spelling.
        assert(AlternativeBase("server_rank_reveal_all") == "server_rank_reveal_all");
        assert(AlternativeBase("alternate_thing") == "alternate_thing");
        assert(AlternativeBase("thing_altx") == "thing_altx");
        assert(AlternativeBase("thing_alt12") == "thing_alt12");
    }

    // Catalogue integrity.
    assert(std::size(Patterns::kLegacySnapshotCatalog) == 58);
    assert(std::size(Patterns::kPortedCatalog) == 105);
    assert(std::size(Patterns::kReferenceCatalog) == 3);
    std::set<std::string_view> names;
    Validate(Patterns::kLegacySnapshotCatalog, names);
    Validate(Patterns::kPortedCatalog, names);
    Validate(Patterns::kReferenceCatalog, names);
    assert(names.size() == 58 + 105 + 3);
    // Every ported name is reachable through offsets::Get() as well.
    assert(names.count("setup_bones") == 1);
    assert(names.count("update_animstate") == 1);
    assert(names.count("dwLocalPlayer") == 1);
    assert(names.count("set_local_player_ready") == 1);
    assert(names.count("send_datagram") == 1);
    assert(names.count("send_datagram_alt") == 1);
    assert(names.count("process_packet") == 1);
    // The buy bot's freeze-time source: both spellings of `main`'s game-rules load.
    assert(names.count("game_rules") == 1);
    assert(names.count("game_rules_alt") == 1);

    // Address: the match itself, published as an RVA, plus `extra`.
    {
        std::uintptr_t value = 0;
        auto definition = MakeDefinition(0, 0, true, 0, false);
        assert(Patterns::Resolve(definition, 0x1100, 0x1000, &FakeRead32, value));
        assert(value == 0x100);
        definition.extra = 0x29; // return address inside the matched function
        assert(Patterns::Resolve(definition, 0x1100, 0x1000, &FakeRead32, value));
        assert(value == 0x129);
    }

    // Pointer: add the offset, read the absolute operand, subtract the module base.
    {
        WriteMemory(kBase + 4, 0x3000);
        std::uintptr_t value = 0;
        auto definition = MakeDefinition(4, 1, true, 0, false);
        assert(Patterns::Resolve(definition, kBase, 0x1000, &FakeRead32, value));
        assert(g_last_read == kBase + 4);
        assert(value == 0x2000);
    }

    // Offset: same read, but the value is a raw field offset and must not be rebased.
    {
        WriteMemory(kBase + 8, 0x2E00);
        std::uintptr_t value = 0;
        auto definition = MakeDefinition(8, 1, false, 0, false);
        assert(Patterns::Resolve(definition, 0x2000, 0x1000, &FakeRead32, value));
        assert(value == 0x2E00);
    }

    // CallTarget: rel32 is relative to the end of the instruction.
    {
        WriteMemory(kBase + 1, static_cast<std::uint32_t>(0x8000 - (0x2001 + 4))); // E8 at 0x2000
        std::uintptr_t value = 0;
        auto definition = MakeDefinition(1, 0, true, 0, true);
        assert(Patterns::Resolve(definition, 0x2000, 0x1000, &FakeRead32, value));
        assert(value == 0x8000 - 0x1000);
        // A negative displacement resolves backwards.
        WriteMemory(kBase + 1, static_cast<std::uint32_t>(0x1100 - (0x2001 + 4)));
        assert(Patterns::Resolve(definition, 0x2000, 0x1000, &FakeRead32, value));
        assert(value == 0x100);
    }

    // Failure paths: no match, unreadable operand, uninitialised global, no module.
    {
        std::uintptr_t value = 0;
        auto definition = MakeDefinition(0, 0, true, 0, false);
        assert(!Patterns::Resolve(definition, 0, 0x1000, &FakeRead32, value));
        assert(!Patterns::Resolve(definition, 0x1100, 0, &FakeRead32, value));
        auto pointer = MakeDefinition(4, 1, true, 0, false);
        assert(!Patterns::Resolve(pointer, kBase, 0x1000, &FailingRead32, value));
        WriteMemory(kBase + 4, 0); // operand not initialised yet
        assert(!Patterns::Resolve(pointer, 0x2000, 0x1000, &FakeRead32, value));
        WriteMemory(kBase + 4, 0x10); // below the module base: not a module pointer
        assert(!Patterns::Resolve(pointer, 0x2000, 0x1000, &FakeRead32, value));
    }
    return 0;
}
