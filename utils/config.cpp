#include "config.hpp"
#include "../features/menu/config.hpp"
#include "log.hpp"
#include <windows.h>
#include <fstream>
#include <string>
#include <sstream>
namespace Config {
config_t settings{};
static std::string FilePath() {
    char appdata[MAX_PATH]{};
    DWORD length = GetEnvironmentVariableA("APPDATA", appdata, MAX_PATH);
    if (!length || length >= MAX_PATH) return "csgo_base.json";
    const std::string dir = std::string(appdata) + "\\csgo_base";
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir + "\\config.json";
}
bool Save() {
    std::ofstream out(FilePath(), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << "{\n"
        << "  \"player_esp\": " << (settings.player_esp ? "true" : "false") << ",\n"
        << "  \"player_names\": " << (settings.player_names ? "true" : "false") << ",\n"
        << "  \"bunnyhop\": " << (settings.bunnyhop ? "true" : "false") << ",\n"
        << "  \"menu_key\": " << settings.menu_key << ",\n"
        << "  \"enemy_color\": [" << int(settings.enemy_color.r) << ',' << int(settings.enemy_color.g) << ',' << int(settings.enemy_color.b) << "]\n"
        << "}\n";
    return static_cast<bool>(out);
}
static bool ReadBool(const std::string& text, const char* key, bool& value) {
    const std::string token = std::string("\"") + key + "\"";
    auto at = text.find(token); if (at == std::string::npos) return false;
    at = text.find(':', at + token.size()); if (at == std::string::npos) return false;
    at = text.find_first_not_of(" \t\r\n", at + 1); if (at == std::string::npos) return false;
    if (text.compare(at, 4, "true") == 0) { value = true; return true; }
    if (text.compare(at, 5, "false") == 0) { value = false; return true; }
    return false;
}
bool Load() {
    std::ifstream in(FilePath(), std::ios::binary);
    if (!in) return false;
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ReadBool(text, "player_esp", settings.player_esp);
    ReadBool(text, "player_names", settings.player_names);
    ReadBool(text, "bunnyhop", settings.bunnyhop);
    const std::string token = "\"menu_key\"";
    auto at = text.find(token);
    if (at != std::string::npos) {
        at = text.find(':', at + token.size());
        if (at != std::string::npos) { try { settings.menu_key = std::stoi(text.substr(at + 1)); } catch (...) {} }
    }
    return true;
}
}
