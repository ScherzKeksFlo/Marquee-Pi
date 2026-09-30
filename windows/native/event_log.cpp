#include "event_log.hpp"

void EventLog::add(LogLevel level, const std::string& source, const std::wstring& message) {
    std::lock_guard<std::mutex> guard(mutex);
    entries.push_back({std::time(nullptr), level, source, message});
    if (entries.size() > CAPACITY) entries.erase(entries.begin(), entries.begin() + (entries.size() - CAPACITY));
    ++changes;
}
std::vector<LogEntry> EventLog::snapshot() const {
    std::lock_guard<std::mutex> guard(mutex);
    return std::vector<LogEntry>(entries.rbegin(), entries.rend());
}
void EventLog::clear() {
    std::lock_guard<std::mutex> guard(mutex);
    entries.clear();
    ++changes;
}
unsigned EventLog::revision() const {
    std::lock_guard<std::mutex> guard(mutex);
    return changes;
}
EventLog& eventLog() {
    static EventLog instance;
    return instance;
}
