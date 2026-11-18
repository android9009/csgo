// Portable checks for the clan tag re-apply and restore policy: the rule that keeps
// SetClanTag from being called every frame.
#include "features/misc/general/general.hpp"
#include <cassert>
#include <cstdint>

namespace {
constexpr std::uintptr_t kLocal = 0x1000;
constexpr std::uintptr_t kOther = 0x2000;
constexpr std::uint32_t kInterval = Misc::ClanTag::kReapplyIntervalMs;

bool Apply(bool enabled, bool applied, const char* applied_tag, const char* desired,
           std::uintptr_t applied_local, std::uintptr_t local, std::uint32_t now,
           std::uint32_t last) {
    return Misc::ClanTag::ShouldApply(enabled, applied, applied_tag, desired, applied_local,
                                      local, now, last, kInterval);
}
} // namespace

int main() {
    // First enable, then a no-op until something actually changes.
    assert(Apply(true, false, "", "pastavar", 0, kLocal, 1000, 0));
    assert(!Apply(true, true, "pastavar", "pastavar", kLocal, kLocal, 2000, 1000));
    assert(Apply(true, true, "pastavar", "other", kLocal, kLocal, 2000, 1000));
    assert(Apply(true, true, "pastavar", "pastavar", kOther, kLocal, 2000, 1000));
    assert(Apply(true, true, "pastavar", "pastavar", kLocal, kLocal, 1000 + kInterval, 1000));
    assert(!Apply(true, true, "pastavar", "pastavar", kLocal, kLocal, 1000 + kInterval - 1, 1000));
    assert(!Apply(false, true, "pastavar", "pastavar", kLocal, kLocal, 9000, 1000));
    assert(!Apply(true, true, "pastavar", "pastavar", kLocal, 0, 9000, 1000));
    assert(!Apply(false, false, "", "pastavar", 0, kLocal, 1000, 0));
    // Tick counter wraparound (GetTickCount wraps after ~49.7 days).
    assert(!Apply(true, true, "pastavar", "pastavar", kLocal, kLocal, 100, 0xFFFFFFF0u));
    assert(Apply(true, true, "pastavar", "pastavar", kLocal, kLocal, 0x1760, 0xFFFFFFF0u));
    // Null tag pointers never crash and force a refresh.
    assert(Apply(true, true, nullptr, "pastavar", kLocal, kLocal, 2000, 1000));
    assert(Apply(true, true, "pastavar", nullptr, kLocal, kLocal, 2000, 1000));
    // The configured tag fits the engine's 15 character limit.
    assert(Misc::ClanTag::kTag != nullptr);
    std::size_t length = 0;
    while (Misc::ClanTag::kTag[length] != 0) ++length;
    assert(length > 0 && length <= 15);

    // Restore exactly once, and only for a tag this module actually applied.
    assert(Misc::ClanTag::ShouldRestore(false, true));
    assert(!Misc::ClanTag::ShouldRestore(true, true));
    assert(!Misc::ClanTag::ShouldRestore(false, false));
    assert(!Misc::ClanTag::ShouldRestore(true, false));
    return 0;
}
