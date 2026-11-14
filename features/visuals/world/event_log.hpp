#pragma once

// World > Windows > Event logs, ported from the leaked event_log.hpp/.cpp.
// Semantics kept 1:1: a local shot is registered as "pending" and resolved
// either by server-confirmed damage (hit / kill) or by the deadline expiring
// (miss). Ring size, throttling and newest-first snapshot are unchanged.
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace player_visuals {

enum class event_category : std::uint8_t { info, shot, hit, kill, miss, evaded };

struct event_log_entry {
    std::uint64_t sequence{};
    std::string text{};
    event_category category{ event_category::info };
    std::chrono::steady_clock::time_point timestamp{};
    std::chrono::steady_clock::time_point deadline{};
    std::string source{};
    bool pending{};
    bool command_consumed{};
    bool seed{};
};

// Menu multibox order: Miss, Hit, Evaded.
struct event_log_filter {
    bool miss = true;
    bool hit = true;
    bool evaded = true;
};

class event_log_t {
public:
    void push(std::string text, event_category category = event_category::info);
    void begin_trigger_shot(std::string source, bool seed);
    void mark_latest_trigger_consumed();
    bool resolve_latest_trigger_shot(int damage, bool killed);
    std::vector<event_log_entry> snapshot(float lifetime_seconds, int maximum,
        const event_log_filter& filter);

private:
    mutable std::mutex m_mutex{};
    std::deque<event_log_entry> m_entries{};
    std::uint64_t m_sequence{};
};

inline event_log_t& event_log() {
    static event_log_t value{};
    return value;
}

} // namespace player_visuals
