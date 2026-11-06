#include "sound_esp.hpp"

#include "dependencies/source.hpp"
#include "settings.hpp"

#include <Windows.h>
#include "imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <mutex>

namespace {
using visuals_source::vector3;

constexpr float ping_lifetime_seconds = 1.35f;
constexpr ULONGLONG duplicate_guard_ms = 100;

enum class sound_kind : unsigned char { footstep, landing, weapon };

struct sound_ping {
    vector3 origin{};
    ULONGLONG created_ms = 0;
    sound_kind kind = sound_kind::footstep;
    bool has_origin = false;
    bool active = false;
};

std::array<sound_ping, 65> pings{};
std::array<ULONGLONG, 65> last_ping_ms{};
std::mutex ping_mutex;

bool contains(const char* text, const char* token) {
    return text && token && std::strstr(text, token) != nullptr;
}

bool is_footstep(const char* name) {
    return contains(name, "player/footsteps") || contains(name, "player\\footsteps");
}

bool is_landing(const char* name) {
    return (contains(name, "player/") || contains(name, "player\\")) &&
        (contains(name, "/land") || contains(name, "\\land"));
}

sound_kind classify(const char* name, bool& supported) {
    supported = true;
    if (is_footstep(name))
        return sound_kind::footstep;
    if (is_landing(name))
        return sound_kind::landing;
    if (contains(name, "weapons"))
        return sound_kind::weapon;
    supported = false;
    return sound_kind::footstep;
}

bool finite(const vector3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) &&
        std::fabs(value.x) < 32768.f && std::fabs(value.y) < 32768.f &&
        std::fabs(value.z) < 32768.f;
}

bool world_to_screen(const float matrix[4][4], const vector3& point,
                     const ImVec2& display, ImVec2& output) {
    const float x = matrix[0][0] * point.x + matrix[0][1] * point.y +
        matrix[0][2] * point.z + matrix[0][3];
    const float y = matrix[1][0] * point.x + matrix[1][1] * point.y +
        matrix[1][2] * point.z + matrix[1][3];
    const float w = matrix[3][0] * point.x + matrix[3][1] * point.y +
        matrix[3][2] * point.z + matrix[3][3];
    if (!std::isfinite(w) || w < 0.001f)
        return false;
    output = ImVec2(display.x * 0.5f + x / w * display.x * 0.5f,
                    display.y * 0.5f - y / w * display.y * 0.5f);
    return std::isfinite(output.x) && std::isfinite(output.y);
}

ImU32 ping_color(const c_color& base, float alpha) {
    return ImGui::GetColorU32(ImVec4(
        std::clamp(base.r, 0.f, 1.f), std::clamp(base.g, 0.f, 1.f),
        std::clamp(base.b, 0.f, 1.f), std::clamp(base.a * alpha, 0.f, 1.f)));
}

const char* label(sound_kind kind) {
    switch (kind) {
    case sound_kind::landing: return "LAND";
    case sound_kind::weapon: return "SHOT";
    default: return "STEP";
    }
}

void draw_ping(ImDrawList* draw, const sound_ping& ping, const c_color& base,
               float age, const ImVec2& display, const float matrix[4][4]) {
    if (age < 0.f || age >= ping_lifetime_seconds || !finite(ping.origin))
        return;

    const float remaining = 1.f - age / ping_lifetime_seconds;
    const float alpha = remaining * remaining;
    const ImU32 ink = ping_color(base, alpha);
    const ImU32 shadow = ImGui::GetColorU32(ImVec4(0.f, 0.f, 0.f, alpha * 0.72f));
    const float radius = 12.f + 72.f * (1.f - remaining);
    constexpr int segments = 28;
    std::array<ImVec2, segments> ring{};
    bool projected = true;

    for (int i = 0; i < segments; ++i) {
        const float angle = 6.28318530718f * static_cast<float>(i) / segments;
        const vector3 point{
            ping.origin.x + std::cos(angle) * radius,
            ping.origin.y + std::sin(angle) * radius,
            ping.origin.z + 3.f
        };
        if (!world_to_screen(matrix, point, display, ring[i])) {
            projected = false;
            break;
        }
    }

    if (projected) {
        draw->AddPolyline(ring.data(), segments, shadow, ImDrawFlags_Closed, 3.f);
        draw->AddPolyline(ring.data(), segments, ink, ImDrawFlags_Closed, 1.6f);
    }

    ImVec2 center;
    if (!world_to_screen(matrix, ping.origin, display, center) ||
        center.x < -80.f || center.x > display.x + 80.f ||
        center.y < -40.f || center.y > display.y + 40.f) {
        return;
    }

    const char* text = label(ping.kind);
    const ImVec2 size = ImGui::CalcTextSize(text);
    const ImVec2 pos(center.x - size.x * 0.5f, center.y - size.y - 6.f);
    draw->AddText(ImVec2(pos.x + 1.f, pos.y + 1.f), shadow, text);
    draw->AddText(pos, ink, text);
    draw->AddCircleFilled(center, 2.5f, ink, 12);
}
} // namespace

namespace sound_esp {
void on_sound(int entity_index, int speaker_entity, const char* sound_entry,
              bool has_origin, float x, float y, float z) {
    bool supported = false;
    const sound_kind kind = classify(sound_entry, supported);
    if (!supported || !visuals_source::ready() || !visuals_source::is_in_game())
        return;

    int player_index = 0;
    const int candidates[] = { speaker_entity, entity_index };
    for (const int candidate : candidates) {
        if (candidate > 0 && candidate <= 64 && visuals_source::player_side_for_sound(candidate) == 0) {
            player_index = candidate;
            break;
        }
    }
    if (player_index == 0)
        return;

    vector3 origin{ x, y, z };
    const bool have_position = has_origin && finite(origin);
    if (!have_position)
        origin = {};

    const ULONGLONG now = GetTickCount64();
    std::scoped_lock lock(ping_mutex);
    if (now - last_ping_ms[player_index] < duplicate_guard_ms)
        return;
    last_ping_ms[player_index] = now;
    pings[player_index] = { origin, now, kind, have_position, true };
}

void render(const visuals_source::frame_data& frame) {
    const auto& settings = player_visuals::settings(0);
    if (!settings.esp_enabled || !settings.sounds) {
        std::scoped_lock lock(ping_mutex);
        pings = {};
        last_ping_ms = {};
        return;
    }
    if (!frame.in_game) {
        std::scoped_lock lock(ping_mutex);
        pings = {};
        last_ping_ms = {};
        return;
    }
    if (!frame.has_view_matrix || !ImGui::GetCurrentContext())
        return;

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    if (display.x < 1.f || display.y < 1.f)
        return;

    std::array<sound_ping, 65> current{};
    {
        const ULONGLONG now = GetTickCount64();
        std::scoped_lock lock(ping_mutex);
        for (std::size_t index = 1; index < pings.size(); ++index) {
            if (pings[index].active &&
                now - pings[index].created_ms >= static_cast<ULONGLONG>(ping_lifetime_seconds * 1000.f)) {
                pings[index].active = false;
            }
            current[index] = pings[index];
        }
    }

    const ULONGLONG now = GetTickCount64();
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    for (std::size_t index = 1; index < current.size(); ++index) {
        auto ping = current[index];
        if (!ping.active)
            continue;
        if (!ping.has_origin) {
            const auto found = std::find_if(frame.players.begin(), frame.players.end(),
                [index](const auto& player) { return player.index == static_cast<int>(index); });
            if (found == frame.players.end() || !finite(found->origin))
                continue;
            ping.origin = found->origin;
            ping.has_origin = true;
        }
        const float age = static_cast<float>(now - ping.created_ms) / 1000.f;
        draw_ping(draw, ping, settings.sounds_color, age, display, frame.view_matrix);
    }
}
} // namespace sound_esp
