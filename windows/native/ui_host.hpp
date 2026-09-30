#pragma once
#include <ctime>
#include <string>
#include "core.hpp"
#include "pi_sync.hpp"

// Name of the media file holding a role on the Pi, from the marker file next to settings.ini
// ("active-media.txt", "boot-media.txt", "shutdown-media.txt"); empty when unset.
inline std::wstring roleFile(const wchar_t* marker) {
    try {
        std::wstring text = fromUtf8(readFile(dataDirectory() + L"\\" + marker));
        while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n')) text.pop_back();
        return text;
    } catch (...) { return L""; }
}
// "10.0.0.10" from "http://10.0.0.10:8765".
inline std::wstring piHost(const Settings& s) {
    std::wstring url = s.piUrl;
    if (url.rfind(L"http://", 0) == 0) url.erase(0, 7);
    const size_t end = url.find_first_of(L":/");
    return end == std::wstring::npos ? url : url.substr(0, end);
}

// Commands of the tray menu, shared by the flyout, the dashboard's quick actions and App.
enum MenuId {
    M_MEDIA = 1001, M_DEFAULT, M_RELOAD, M_REBOOT, M_SHUTDOWN,
    M_SETTINGS, M_OPEN_INI, M_LOAD_INI, M_EXIT
};

// Settings window pages in navigation order (matches Str::TabDashboard ...).
enum SettingsPage {
    PAGE_DASHBOARD, PAGE_CONNECTION, PAGE_MEDIA, PAGE_GESTURES, PAGE_RETROARCH, PAGE_GENERAL, PAGE_LOGS,
    SETTINGS_PAGES
};

// What the windows show about the Pi and the running game. Owned by the UI thread.
struct PiStatus {
    bool known = false;      // at least one status answer (or failure) has arrived
    bool connected = false;
    std::wstring title;      // what the Pi shows; empty = default media
    int latencyMs = -1;
    PiDisplay display;       // what the Pi reported about its screen
    std::time_t lastContact = 0;
    bool pipeListening = false;
    bool gameActive = false;
    std::wstring gameTitle, gameMarquee;
    std::time_t gameStarted = 0;
};

class UiHost {
public:
    virtual ~UiHost() = default;
    virtual HWND window() = 0;
    virtual PiSync& sync() = 0;
    virtual const PiStatus& status() const = 0;
    // Runs a tray command (M_DEFAULT ... M_EXIT); reports the outcome to the user itself.
    virtual void command(int id) = 0;
    // The UI language changed; refresh what the host shows (tray tip).
    virtual void languageChanged() = 0;
};
