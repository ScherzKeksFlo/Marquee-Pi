#pragma once
#include <windows.h>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include "json.hpp"

inline constexpr size_t MARQUEE_PI_MAX_REQUEST_BYTES = 32 * 1024 * 1024;
inline constexpr size_t MARQUEE_PI_JSON_HEADROOM_BYTES = 1024 * 1024;
inline constexpr size_t MARQUEE_PI_ARTWORK_BUDGET_BYTES =
    (MARQUEE_PI_MAX_REQUEST_BYTES - MARQUEE_PI_JSON_HEADROOM_BYTES) * 3 / 4;

struct Settings {
    std::wstring piUrl;
    std::wstring token;
    std::wstring hotkey = L"F1";
    bool retroArchNetworkControl = false;
    int retroArchNetworkPort = 55355;
    bool autostart = false;
    // "auto" follows the Windows display language, otherwise "en" or "de".
    std::string language = "auto";
    // Long press keeps opening the Pi touch menu unless it is reassigned.
    std::map<std::string, std::string> gestures{{"long-press", "touch_menu"}};
    bool configured() const;
};

struct GameMessage {
    std::string action;
    std::wstring title;
    std::wstring marquee;
    std::wstring controls;
    std::wstring boxArt;
    std::wstring logo;
};

struct HttpResult {
    DWORD status = 0;
    std::string body;
};

struct HttpError : std::runtime_error {
    DWORD status;
    HttpError(DWORD code, const std::string& message) : std::runtime_error(message), status(code) {}
};

std::wstring fromUtf8(const std::string& s);
std::string toUtf8(const std::wstring& s);
std::wstring dataDirectory();
std::wstring settingsPath();
std::wstring mediaDirectory();
bool fileExists(const std::wstring& path);
std::string readFile(const std::wstring& path);
void writeFile(const std::wstring& path, const std::string& data);
Settings loadSettings();
void saveSettings(const Settings& settings);
bool autostartEnabled();
void setAutostart(bool enabled);
bool validHotkey(const std::wstring& text);
bool isUser32ShutdownEvent(const std::wstring& xml);
bool isPowerOffType(std::wstring value);
bool sendRetroArchHotkey(const std::wstring& text);
bool sendRetroArchNetworkCommand(int port);
HttpResult piRequest(const Settings& settings, const std::wstring& method,
                     const std::wstring& path, const std::string& body = {},
                     const std::wstring& contentType = L"",
                     const std::wstring& extraHeader = L"", int timeoutMs = 8000);
std::string gamePayload(const GameMessage& game);
// Reasons why artwork was left out of the last payloads built on this thread; clears the list.
std::vector<std::string> takeArtworkErrors();
std::vector<std::string> gameWarnings(const std::string& response);
std::string gesturePayload(const Settings& settings);
// Tells the Pi which language its touch menu should use (the resolved UI language).
std::string languagePayload(const Settings& settings);
GameMessage parseGameMessage(const std::string& json);
std::wstring errorText(const std::exception& error);
