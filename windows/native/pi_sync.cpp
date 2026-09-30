#include "pi_sync.hpp"

PiSync::PiSync(Settings initial, PiTransport& transport, PiSyncEvents events)
    : transport(transport), events(std::move(events)), current(std::move(initial)) {}

void PiSync::gameStarted(GameMessage message) {
    std::lock_guard<std::mutex> guard(mutex);
    game = std::move(message);
    needsSync = true; pendingDefault = false; ++gameVersion;
}
void PiSync::gameExited() {
    std::lock_guard<std::mutex> guard(mutex);
    game.reset(); needsSync = false; pendingDefault = true; ++gameVersion;
}
void PiSync::settingsChanged(Settings next) {
    std::lock_guard<std::mutex> guard(mutex);
    current = std::move(next); gestureDirty = true; needsSync = game.has_value(); ++configVersion;
}
Settings PiSync::settings() const {
    std::lock_guard<std::mutex> guard(mutex);
    return current;
}
bool PiSync::inGame() const {
    std::lock_guard<std::mutex> guard(mutex);
    return game.has_value();
}
void PiSync::post(const std::wstring& path, int timeoutMs) {
    transport.request(settings(), L"POST", path, {}, L"", timeoutMs);
}
void PiSync::showDefault() {
    post(L"/v1/default");
    std::lock_guard<std::mutex> guard(mutex);
    game.reset(); needsSync = false; pendingDefault = false; ++gameVersion;
}
void PiSync::reloadDisplay() { post(L"/v1/reload"); }
void PiSync::rebootPi() { post(L"/v1/reboot"); }
void PiSync::shutdownPi(int timeoutMs) { post(L"/v1/shutdown", timeoutMs); }

void PiSync::tick() {
    static const std::wstring json = L"application/json; charset=utf-8";
    Settings copy;
    std::optional<GameMessage> now;
    uint64_t version = 0;
    bool dirty, sync, reset;
    {
        std::lock_guard<std::mutex> guard(mutex);
        copy = current; now = game; version = gameVersion;
        dirty = gestureDirty; sync = needsSync; reset = pendingDefault;
    }
    const bool statusCycle = cycle % 7 == 0;
    if (statusCycle || statusDown) {
        try {
            auto response = transport.request(copy, L"GET", L"/v1/status");
            auto status = mini::parse(response.body);
            const bool recovered = statusDown;
            online = true; statusDown = false;
            if (events.status && (statusCycle || recovered)) events.status(true, status.get("game_title").value(""));
        } catch (...) {
            const bool wasDown = statusDown;
            online = false; statusDown = true;
            if (events.status && (statusCycle || !wasDown)) events.status(false, "");
            std::lock_guard<std::mutex> guard(mutex);
            gestureDirty = true;
            if (game) needsSync = true;
        }
    }
    if (online && copy.configured()) {
        if (statusCycle && now && ++heartbeat % 3 == 0) {
            try { transport.request(copy, L"POST", L"/v1/heartbeat"); } catch (...) {}
        }
        try {
            if (dirty) {
                transport.request(copy, L"POST", L"/v1/gesture-config", gesturePayload(copy), json);
                try {  // a Pi without language support answers 404; the gestures above still count
                    transport.request(copy, L"POST", L"/v1/language", languagePayload(copy), json);
                } catch (const HttpError& error) {
                    if (error.status != 404) throw;
                }
                std::lock_guard<std::mutex> guard(mutex);
                if (configVersion == 0 || (current.gestures == copy.gestures &&
                                           current.language == copy.language)) gestureDirty = false;
            }
            if (reset) {
                transport.request(copy, L"POST", L"/v1/default");
                std::lock_guard<std::mutex> guard(mutex);
                if (gameVersion == version) pendingDefault = false;
            } else if (sync && now) {
                try {
                    const std::string payload = gamePayload(*now);
                    for (const auto& problem : takeArtworkErrors())
                        if (events.warning) events.warning("not sent: " + problem);
                    auto response = transport.request(copy, L"POST", L"/v1/game", payload, json);
                    for (const auto& warning : gameWarnings(response.body))
                        if (events.warning) events.warning(warning);
                    std::lock_guard<std::mutex> guard(mutex);
                    if (gameVersion == version) needsSync = false;
                } catch (const HttpError& error) {
                    if (error.status >= 400 && error.status < 500) {
                        std::lock_guard<std::mutex> guard(mutex);
                        if (gameVersion == version) needsSync = false;
                    } else {
                        online = false;
                    }
                } catch (...) {
                    online = false;
                }
            }
        } catch (...) {}
        try {
            auto response = transport.request(copy, L"GET",
                                              L"/v1/gesture-events?after=" + std::to_wstring(cursor));
            auto payload = mini::parse(response.body);
            std::string nextInstance = payload.get("instance_id").value();
            if (nextInstance != instance) {
                // The first instance we see was already served by this tick; only a
                // changed instance means the Pi restarted and lost its state.
                const bool restarted = !instance.empty();
                instance = nextInstance; cursor = 0; cursorReady = false;
                if (restarted) {
                    std::lock_guard<std::mutex> guard(mutex);
                    gestureDirty = true;
                    if (game) needsSync = true;
                }
            } else {
                const auto& received = payload.get("events").items;
                for (const auto& event : received) {
                    int64_t id = event.get("id").integer();
                    if (id <= cursor) continue;
                    cursor = id;
                    if (cursorReady && now && event.get("action").value() == "retroarch_menu" &&
                        events.retroArchMenu)
                        events.retroArchMenu();
                }
                if (!cursorReady && received.size() < 20) cursorReady = true;
            }
        } catch (...) {}
    }
    ++cycle;
}
