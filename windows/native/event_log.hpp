#pragma once
#include <ctime>
#include <mutex>
#include <string>
#include <vector>

// In-memory ring buffer behind the Logs tab and the dashboard's recent events.
// Sources: "pipe", "api", "conn", "media", "shutdown". Safe to call from any thread.
enum class LogLevel { Info, Warn, Error };

struct LogEntry {
    std::time_t time = 0;
    LogLevel level = LogLevel::Info;
    std::string source;
    std::wstring message;
};

class EventLog {
public:
    static constexpr size_t CAPACITY = 500;

    void add(LogLevel level, const std::string& source, const std::wstring& message);
    // Newest first.
    std::vector<LogEntry> snapshot() const;
    void clear();
    // Bumped on every add; lets a window notice that there is something new to draw.
    unsigned revision() const;

private:
    mutable std::mutex mutex;
    std::vector<LogEntry> entries;  // oldest first
    unsigned changes = 0;
};

EventLog& eventLog();
inline void logEvent(LogLevel level, const std::string& source, const std::wstring& message) {
    eventLog().add(level, source, message);
}
