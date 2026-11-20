// Portable checks for the BUY BOT: the weapon table, the command string it produces and the
// once-per-round scheduler. The feature's runtime (game rules, client command) needs the
// game and is not part of this test.
#include "features/misc/buybot/buybot.hpp"
#include <cassert>
#include <cstdio>
#include <string>

using namespace Misc::Buybot;

static void Expect(const std::string& actual, const std::string& expected) {
    if (actual != expected) {
        std::printf("buy bot: expected \"%s\", got \"%s\"\n", expected.c_str(), actual.c_str());
        assert(false);
    }
}

int main() {
    // Nothing selected: the group box is off, and it produces no command at all.
    Settings empty;
    assert(!Configured(empty));
    assert(BuildCommand(empty).empty());

    // Every single group counts as "configured" on its own.
    {
        Settings s; s.primary = PrimaryAwp; assert(Configured(s));
        Settings p; p.pistol = PistolP250; assert(Configured(p));
        Settings a; a.armor = ArmorKevlar; assert(Configured(a));
        Settings d; d.defuser = true; assert(Configured(d));
        Settings t; t.taser = true; assert(Configured(t));
        Settings g; g.grenades = true; assert(Configured(g));
    }
    // Out-of-range values (a corrupt config) are not a purchase.
    {
        Settings s; s.primary = 99; assert(!Configured(s)); assert(BuildCommand(s).empty());
        Settings p; p.pistol = -1; assert(!Configured(p));
    }

    // The primary table, as main's buy_bot.cpp spells it.
    {
        Settings s; s.primary = PrimaryAk47;
        Expect(BuildCommand(s), "buy ak47; buy m4a1; buy m4a1_silencer;");
        s.primary = PrimarySg556; Expect(BuildCommand(s), "buy aug; buy sg556;");
        s.primary = PrimaryAwp; Expect(BuildCommand(s), "buy awp;");
        s.primary = PrimarySsg08; Expect(BuildCommand(s), "buy ssg08;");
        s.primary = PrimaryScar20; Expect(BuildCommand(s), "buy scar20; buy g3sg1;");
        s.primary = PrimaryNegev; Expect(BuildCommand(s), "buy negev;");
    }
    // The pistol table. CZ75-Auto and the R8 are their own menu entries here, so they no
    // longer ride along with the Desert Eagle as they do in main.
    {
        Settings s; s.pistol = PistolDeagle; Expect(BuildCommand(s), "buy deagle;");
        s.pistol = PistolElites; Expect(BuildCommand(s), "buy elite;");
        s.pistol = PistolTec9; Expect(BuildCommand(s), "buy tec9; buy fn57;");
        s.pistol = PistolP250; Expect(BuildCommand(s), "buy p250;");
        s.pistol = PistolCz75a; Expect(BuildCommand(s), "buy cz75a;");
        s.pistol = PistolRevolver; Expect(BuildCommand(s), "buy revolver;");
    }
    // Armor: vest is kevlar, vesthelm is kevlar + helmet.
    {
        Settings s; s.armor = ArmorKevlar; Expect(BuildCommand(s), "buy vest;");
        s.armor = ArmorKevlarHelmet; Expect(BuildCommand(s), "buy vesthelm;");
    }
    // Everything at once: the expensive groups come first (main's order), so money that
    // runs out is spent on the rifle rather than on a grenade.
    {
        Settings s;
        s.primary = PrimaryAk47; s.pistol = PistolP250; s.armor = ArmorKevlarHelmet;
        s.defuser = true; s.taser = true; s.grenades = true;
        Expect(BuildCommand(s),
               "buy ak47; buy m4a1; buy m4a1_silencer; buy p250; buy vesthelm; buy defuser; "
               "buy taser; buy hegrenade; buy molotov; buy incgrenade; buy smokegrenade; buy flashbang;");
    }

    // Window edges: opening resets the budget, closing clears it.
    {
        Window window;
        Settings s; s.primary = PrimaryAwp;
        assert(window.Update(true));          // rising edge
        assert(!window.Update(true));         // still open
        window.Note(5.f);
        assert(window.attempts == 1);
        assert(!window.Update(false));        // falling edge
        assert(window.attempts == 0 && window.last_attempt < 0.f);
        assert(!window.open);
        assert(window.Update(true));          // next round starts a fresh window
        assert(window.attempts == 0);
    }

    // The scheduler: only a live player, in a running game, inside the buy window.
    {
        Settings s; s.primary = PrimaryAwp;
        Window window;
        window.Update(true);
        assert(ShouldAttempt(s, true, true, true, window, 10.f));
        assert(!ShouldAttempt(s, true, false, true, window, 10.f));   // dead
        assert(!ShouldAttempt(s, false, true, true, window, 10.f));   // not in game
        assert(!ShouldAttempt(s, true, true, false, window, 10.f));   // no freeze time
        assert(!ShouldAttempt(Settings{}, true, true, true, window, 10.f)); // nothing to buy

        // One attempt, then a one second pause, then two more, then silence.
        window.Note(10.f);
        assert(window.attempts == 1);
        assert(!ShouldAttempt(s, true, true, true, window, 10.5f));
        assert(ShouldAttempt(s, true, true, true, window, 11.f));
        window.Note(11.f);
        assert(!ShouldAttempt(s, true, true, true, window, 12.f) == false);
        window.Note(12.f);
        assert(!ShouldAttempt(s, true, true, true, window, 20.f)); // budget spent
        assert(window.attempts == kMaxAttempts);
    }

    // A restarted clock (new map) must not leave the round without a purchase.
    {
        Settings s; s.primary = PrimaryAwp;
        Window window;
        window.Update(true);
        window.Note(100.f);
        assert(ShouldAttempt(s, true, true, true, window, 1.f));
    }

    std::puts("buybot_test: ok");
    return 0;
}
