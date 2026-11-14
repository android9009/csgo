// Optional Darkside background/media services are isolated from the core GUI.
// Their D3D11/WIC/WinRT implementations are not part of the D3D9 renderer. The
// widgets and their original layout remain available; these providers report
// that no animated texture or active media session is available.

#include "shader_bg.hpp"
#include "spotify.hpp"
#include "gifbg.hpp"

#include <Windows.h>
#include <shellapi.h>

namespace shader_bg {
namespace {
state_t current{};
}

state_t& state() { return current; }
int count() { return 0; }
int builtin_count() { return 0; }
const char* name(int) { return ""; }
const char* icon(int) { return ""; }
void update(ImVec2, ImVec2) {}
void* view(ImVec2) { return nullptr; }
bool draw_into(ImDrawList*, ImVec2, ImVec2, float, float) { return false; }
void shutdown() {}
} // namespace shader_bg

void menu_shader_background(ImDrawList*, ImVec2, ImVec2, float) {}

namespace spotify {
namespace {
snapshot_t current{ status_t::not_running };
lyrics_t current_lyrics{};
}

void set_lyrics_enabled(bool on) {
    current_lyrics.state = on ? lyrics_state_t::none : lyrics_state_t::off;
    current_lyrics.lines.clear();
    current_lyrics.synced = false;
}

const lyrics_t& lyrics() { return current_lyrics; }
int lyrics_index(double) { return -1; }
void start() {}
void stop() {}
void tick(float) {}
const snapshot_t& state() { return current; }
void* art_view() { return nullptr; }
void toggle_play() {}
void next() {}
void previous() {}
void seek(double) {}
void set_volume(float value) {
    current.volume = value;
    current.has_volume = true;
}
void set_shuffle(bool on) { current.shuffle = on; }
void cycle_repeat() {
    current.repeat = current.repeat == repeat_t::off ? repeat_t::all
        : current.repeat == repeat_t::all ? repeat_t::one : repeat_t::off;
}
void launch() {
    ShellExecuteW(nullptr, L"open", L"spotify:", nullptr, nullptr, SW_SHOWNORMAL);
}
} // namespace spotify

namespace gifbg {
namespace {
bool is_enabled = false;
}

void set_enabled(bool on) { is_enabled = on; }
bool enabled() { return is_enabled; }
void set_track(const std::string&, const std::string&) {}
void tick(float) {}
void* view() { return nullptr; }
bool loaded() { return false; }
void start() {}
void stop() { is_enabled = false; }
} // namespace gifbg
