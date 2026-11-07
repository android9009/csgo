#include "event_log.hpp"

#include <algorithm>
#include <cstdio>

namespace player_visuals {

namespace {
constexpr std::size_t k_max_entries = 64;
} // namespace

void event_log_t::push(std::string text, const event_category category) {
    if (text.empty())
        return;
    std::scoped_lock lock(m_mutex);
    m_entries.push_back({ ++m_sequence, std::move(text), category,
        std::chrono::steady_clock::now() });
    while (m_entries.size() > k_max_entries)
        m_entries.pop_front();
}

void event_log_t::begin_trigger_shot(std::string source, const bool seed) {
    if (source.empty())
        return;
    const auto now = std::chrono::steady_clock::now();
    std::scoped_lock lock(m_mutex);
    m_entries.push_back({ ++m_sequence, source + ": shot pending", event_category::shot,
        now, now + std::chrono::milliseconds(1200), std::move(source), true, false, seed });
    while (m_entries.size() > k_max_entries)
        m_entries.pop_front();
}

void event_log_t::mark_latest_trigger_consumed() {
    const auto now = std::chrono::steady_clock::now();
    std::scoped_lock lock(m_mutex);
    for (auto it = m_entries.rbegin(); it != m_entries.rend(); ++it) {
        if (!it->pending || it->category != event_category::shot)
            continue;
        if (now > it->deadline)
            break;
        it->command_consumed = true;
        it->text = it->source + ": shot registered";
        return;
    }
}

bool event_log_t::resolve_latest_trigger_shot(const int damage, const bool killed) {
    if (damage <= 0)
        return false;
    const auto now = std::chrono::steady_clock::now();
    std::scoped_lock lock(m_mutex);
    for (auto it = m_entries.rbegin(); it != m_entries.rend(); ++it) {
        if (!it->pending || it->category != event_category::shot)
            continue;
        if (now > it->deadline)
            break;
        it->pending = false;
        it->command_consumed = true;
        it->category = killed ? event_category::kill : event_category::hit;
        it->timestamp = now;
        char buffer[320];
        std::snprintf(buffer, sizeof(buffer), "%s: %s - %d damage", it->source.c_str(),
            killed ? "kill" : "hit", damage);
        it->text = buffer;
        return true;
    }
    return false;
}

std::vector<event_log_entry> event_log_t::snapshot(const float lifetime_seconds,
                                                   const int maximum,
                                                   const event_log_filter& filter) {
    const auto now = std::chrono::steady_clock::now();
    const auto lifetime = std::chrono::duration<float>(
        std::clamp(lifetime_seconds, 0.5f, 20.f));
    const int limit = std::clamp(maximum, 1, 12);
    std::vector<event_log_entry> result{};
    result.reserve(static_cast<std::size_t>(limit));
    std::scoped_lock lock(m_mutex);
    for (auto& entry : m_entries) {
        if (!entry.pending || now < entry.deadline)
            continue;
        entry.pending = false;
        entry.category = event_category::miss;
        entry.timestamp = now;
        if (!entry.command_consumed)
            entry.text = entry.source + ": shot rejected - input not consumed";
        else if (entry.seed)
            entry.text = entry.source + ": miss - no server damage (possible tick/seed mismatch)";
        else
            entry.text = entry.source + ": miss - no server-confirmed damage";
    }
    for (auto it = m_entries.rbegin(); it != m_entries.rend() &&
        static_cast<int>(result.size()) < limit; ++it) {
        bool visible = true;
        switch (it->category) {
        case event_category::miss: visible = filter.miss; break;
        case event_category::hit:
        case event_category::kill: visible = filter.hit; break;
        case event_category::evaded: visible = filter.evaded; break;
        default: visible = true; break;
        }
        if (visible && now - it->timestamp <= lifetime)
            result.push_back(*it);
    }
    std::reverse(result.begin(), result.end());
    return result;
}

} // namespace player_visuals
