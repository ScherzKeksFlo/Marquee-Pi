#pragma once
#include <windows.h>
#include <map>
#include <string>
#include <vector>
#include "json.hpp"

struct Settings {
    std::wstring piUrl;
    std::wstring token;
    std::wstring hotkey = L"F1";
    bool autostart = false;
    std::map<std::string, std::string> gestures;
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
bool sendRetroArchHotkey(const std::wstring& text);
HttpResult piRequest(const Settings& settings, const std::wstring& method,
                     const std::wstring& path, const std::string& body = {},
                     const std::wstring& contentType = L"",
                     const std::wstring& extraHeader = L"", int timeoutMs = 8000);
std::string gamePayload(const GameMessage& game);
std::string gesturePayload(const Settings& settings);
GameMessage parseGameMessage(const std::string& json);
std::wstring errorText(const std::exception& error);
