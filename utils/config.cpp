#include "config.hpp"

#include "log.hpp"
#include <windows.h>
#include <fstream>
#include <json.h>

namespace Config {
settings_t settings{};

using nlohmann::json;

static std::string FilePath() {
    char appdata[MAX_PATH]{};
    DWORD length = GetEnvironmentVariableA("APPDATA", appdata, MAX_PATH);
    if (!length || length >= MAX_PATH) return "csgo_base.json";
    const std::string dir = std::string(appdata) + "\\csgo_base";
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir + "\\config.json";
}

static json ColorToJson(const Color& c) {
    return json::array({ c.r, c.g, c.b, c.a });
}

static Color ColorFromJson(const json& j, const Color& fallback) {
    if (!j.is_array() || j.size() < 3) return fallback;
    const uint8_t r = j[0].get<uint8_t>();
    const uint8_t g = j[1].get<uint8_t>();
    const uint8_t b = j[2].get<uint8_t>();
    const uint8_t a = j.size() > 3 ? j[3].get<uint8_t>() : 255;
    return Color(r, g, b, a);
}

bool Save() {
    json j;
    j["player_esp"] = settings.player_esp;
    j["player_names"] = settings.player_names;
    j["bunnyhop"] = settings.bunnyhop;
    j["menu_key"] = settings.menu_key;
    j["enemy_color"] = ColorToJson(settings.enemy_color);
    j["friendly_color"] = ColorToJson(settings.friendly_color);

    std::ofstream out(FilePath(), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << j.dump(4) << '\n';
    return static_cast<bool>(out);
}

bool Load() {
    std::ifstream in(FilePath(), std::ios::binary);
    if (!in) return false;
    json j;
    try {
        in >> j;
    } catch (const std::exception& e) {
        LOG_ERROR("config parse failed: %s", e.what());
        return false;
    }
    settings.player_esp = j.value("player_esp", settings.player_esp);
    settings.player_names = j.value("player_names", settings.player_names);
    settings.bunnyhop = j.value("bunnyhop", settings.bunnyhop);
    settings.menu_key = j.value("menu_key", settings.menu_key);
    if (j.contains("enemy_color"))
        settings.enemy_color = ColorFromJson(j["enemy_color"], settings.enemy_color);
    if (j.contains("friendly_color"))
        settings.friendly_color = ColorFromJson(j["friendly_color"], settings.friendly_color);
    return true;
}
}
