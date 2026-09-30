#pragma once
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include "core.hpp"

// The seam between PiSync and the Pi. Production uses WinHTTP (WinHttpTransport);
// tests script a fake Pi. Failures are reported as HttpError (HTTP status) or any
// other std::exception (network errors), exactly like piRequest.
struct PiTransport {
    virtual ~PiTransport() = default;
    virtual HttpResult request(const Settings& settings, const std::wstring& method,
                               const std::wstring& path, const std::string& body = {},
                               const std::wstring& contentType = L"", int timeoutMs = 8000) = 0;
};

struct WinHttpTransport : PiTransport {
    HttpResult request(const Settings& settings, const std::wstring& method,
                       const std::wstring& path, const std::string& body,
                       const std::wstring& contentType, int timeoutMs) override {
        return piRequest(settings, method, path, body, contentType, L"", timeoutMs);
    }
};

// What PiSync reports back. Both callbacks run on the thread that calls tick(), never
// while the internal lock is held.
struct PiSyncEvents {
    // title is empty while the Pi shows its default media.
    std::function<void(bool connected, const std::string& title)> status;
    std::function<void()> retroArchMenu;
    // Artwork that was left out and Pi warnings about the last game payload.
    std::function<void(const std::string& message)> warning;
};

// Reconciles what Windows wants the Pi to show (current game session, gesture and
// language settings) with what the Pi has. Intent methods may be called from any
// thread; tick() belongs to one polling thread.
class PiSync {
public:
    PiSync(Settings initial, PiTransport& transport, PiSyncEvents events = {});

    void gameStarted(GameMessage game);
    void gameExited();
    void settingsChanged(Settings next);

    // One-shot Pi commands. They throw when the Pi cannot be reached or refuses.
    void showDefault();
    void reloadDisplay();
    void rebootPi();
    void shutdownPi(int timeoutMs = 8000);

    Settings settings() const;
    bool inGame() const;

    // One polling cycle. The caller sleeps between cycles (about 750 ms).
    void tick();

private:
    PiTransport& transport;
    PiSyncEvents events;

    mutable std::mutex mutex;
    Settings current;
    std::optional<GameMessage> game;
    uint64_t gameVersion = 0, configVersion = 0;
    bool needsSync = false, pendingDefault = false, gestureDirty = true;

    // Owned by the polling thread.
    std::string instance;
    int64_t cursor = 0;
    bool cursorReady = false;
    int cycle = 0, heartbeat = 0;
    bool online = false;
    bool statusDown = false;  // the last status poll failed: poll every tick until it works

    void post(const std::wstring& path, int timeoutMs = 8000);
};
