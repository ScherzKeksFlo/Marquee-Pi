#pragma once
#include <chrono>
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

// The screen of the Pi as it reports it in /v1/status. Older Pis send nothing: known stays false and
// the windows fall back to 800 x 480 with touch, the display the project started with.
struct PiDisplay {
    bool known = false;   // the Pi sent a display block
    int width = 0, height = 0;  // physical pixels; 0 until the kiosk page reported them
    bool touch = true;    // false: a view-only display
    bool cover = false;   // picture fit: cover (crops) instead of contain
    std::string model;    // "Raspberry Pi 3 Model B Plus Rev 1.3", may be empty
    std::string output;   // "HDMI-1"

    bool sizeKnown() const { return width > 0 && height > 0; }
    int shownWidth() const { return sizeKnown() ? width : 800; }
    int shownHeight() const { return sizeKnown() ? height : 480; }
    // height / width, the ratio the previews are drawn with
    double ratio() const { return double(shownHeight()) / double(shownWidth()); }
};

// What PiSync reports back. Both callbacks run on the thread that calls tick(), never
// while the internal lock is held.
struct PiSyncEvents {
    // title is empty while the Pi shows its default media; latencyMs is -1 when offline.
    std::function<void(bool connected, const std::string& title, int latencyMs)> status;
    // Every status poll that succeeded, with what the Pi says about its display.
    std::function<void(const PiDisplay& display)> display;
    std::function<void()> retroArchMenu;
    // A game was accepted by the Pi: request size and round trip.
    std::function<void(size_t bytes, int ms)> gameSent;
    // Artwork that was left out, and warnings the Pi returned for the last game payload.
    std::function<void(const std::string& message)> artworkSkipped;
    std::function<void(const std::string& message)> piWarning;
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
    PiDisplay piDisplay() const;

    // One polling cycle. The caller sleeps between cycles (about 750 ms).
    void tick();

private:
    PiTransport& transport;
    PiSyncEvents events;

    mutable std::mutex mutex;
    Settings current;
    PiDisplay shown;
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
