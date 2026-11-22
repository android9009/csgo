#include "general.hpp"
#include "../../../context.hpp"
#include "../../../sdk/entity/player.hpp"
#include "../../../sdk/interfaces/interfaces.hpp"
#include "../../../utils/log.hpp"
#include <cmath>
#include <mmsystem.h>
#include <cstdint>
#include <cstring>
#include <vector>
#pragma comment(lib, "winmm.lib")

namespace Misc::General::Detail {
namespace {
// `main` plays user-supplied .wav files with Win32 PlaySoundA. This build's menu instead
// picks from named presets and ships no audio assets, so each preset is a short 16-bit PCM
// blip synthesised in memory and played the same way (SND_MEMORY | SND_ASYNC). Distinct
// frequency/waveform per preset so the selection is audible.
constexpr int kSampleRate = 22050;
constexpr int kDurationMs = 70;

struct Preset {
    float freq;
    int wave; // 0 sine, 1 square, 2 chirp-up, 3 two-tone
};
const Preset kPresets[] = {
    {880.f, 0},  // Bell
    {520.f, 1},  // CoD
    {180.f, 0},  // Fatality
    {660.f, 3},  // Arena switch
    {440.f, 2},  // Bepis
    {330.f, 1},  // Phonk
    {1320.f, 2}, // Sparkle
    {260.f, 3},  // Cock
};

std::vector<std::uint8_t> g_wav; // kept alive for the lifetime of the SND_MEMORY playback

bool Synthesize(const Preset& p, std::vector<std::uint8_t>& out) {
    const int samples = kSampleRate * kDurationMs / 1000;
    const int data_bytes = samples * 2;
    out.resize(44 + data_bytes);

    auto put = [&](size_t off, std::uint32_t v) { std::memcpy(out.data() + off, &v, 4); };
    std::memcpy(out.data(), "RIFF", 4);
    put(4, 36 + data_bytes);
    std::memcpy(out.data() + 8, "WAVEfmt ", 8);
    put(16, 16);                // fmt chunk size
    std::memcpy(out.data() + 20, "\x01\x00\x01\x00", 4); // PCM, mono
    put(24, kSampleRate);
    put(28, kSampleRate * 2);   // byte rate
    std::memcpy(out.data() + 32, "\x02\x00", 2);         // block align
    std::memcpy(out.data() + 34, "\x10\x00", 2);         // 16-bit
    std::memcpy(out.data() + 36, "data", 4);
    put(40, data_bytes);

    const double step = 2.0 * 3.14159265358979323846 * p.freq / kSampleRate;
    for (int i = 0; i < samples; ++i) {
        const double t = i / (double)samples;
        const double envelope = (1.0 - t) * (1.0 - t); // quick decay
        double s = 0.0;
        if (p.wave == 0)
            s = std::sin(step * i);
        else if (p.wave == 1)
            s = (std::sin(step * i) >= 0.0 ? 1.0 : -1.0) * 0.7;
        else if (p.wave == 2)
            s = std::sin(step * i * (1.0 + 2.0 * t)); // chirp up
        else
            s = std::sin(step * i) * (i < samples / 2 ? 1.0 : 0.55); // two-tone-ish
        const long v = (long)(s * envelope * 32767.0 * 0.6);
        std::memcpy(out.data() + 44 + i * 2, &v, 2);
    }
    return true;
}

void PlayPreset(int index) {
    if (index < 0 || index >= (int)(sizeof(kPresets) / sizeof(kPresets[0]))) return;
    const Preset p = kPresets[index];
    if (!Synthesize(p, g_wav)) return;
    static bool logged = false;
    __try {
        PlaySoundA(reinterpret_cast<LPSTR>(g_wav.data()), nullptr, SND_MEMORY | SND_ASYNC);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!logged) { logged = true; LOG_WARN("hitsound: PlaySound raised an exception"); }
    }
}

bool IsLocalAttacker(const IGameEvent* event) {
    if (!event || !Interfaces::Engine) return false;
    const int attacker = event->GetInt("attacker");
    if (!attacker) return false;
    return Interfaces::Engine->GetPlayerForUserID(attacker) == Interfaces::Engine->GetLocalPlayer();
}

// `main` c_bullet_tracers::on_player_hurt skips the sound when the victim is a teammate
// (player->is_teammate()); same rule for the kill sound.
bool IsTeammateVictim(const IGameEvent* event) {
    if (!event || !Interfaces::Engine || !ctx.local) return true;
    const int victim = event->GetInt("userid");
    if (!victim) return false;
    const int index = Interfaces::Engine->GetPlayerForUserID(victim);
    if (index <= 0) return false;
    if (const auto entity = Interfaces::EntityList ?
                               Interfaces::EntityList->GetClientEntity(index) : nullptr) {
        const auto player = static_cast<CCSPlayer*>(entity);
        const int team = player->m_iTeamNum();
        if (team != 2 && team != 3) return false; // spectating/sideless victim: not our team
        return team == ctx.local->m_iTeamNum();
    }
    return false;
}
} // namespace

void HitSoundOnHurt(const IGameEvent* event) {
    // A `player_hurt` where we are the attacker (and the victim is not a teammate) is a
    // landed hit.
    if (!HitSoundEnabled()) return;
    if (const auto sel = HitsoundSelect(); sel >= 1)
        if (IsLocalAttacker(event) && !IsTeammateVictim(event)) PlayPreset(sel - 1);
}

void HitSoundOnDeath(const IGameEvent* event) {
    // A `player_death` where we are the attacker (and the victim is not a teammate) is a kill.
    if (!KillSoundEnabled()) return;
    if (const auto sel = KillSoundSelect(); sel >= 1)
        if (IsLocalAttacker(event) && !IsTeammateVictim(event)) PlayPreset(sel - 1);
}
} // namespace Misc::General::Detail
