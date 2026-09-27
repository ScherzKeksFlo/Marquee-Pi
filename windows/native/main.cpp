#include "core.hpp"
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <winevt.h>
#include <filesystem>
#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>
#include <algorithm>
#include <sstream>

namespace fs = std::filesystem;
constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT WM_STATUS = WM_APP + 2;
constexpr UINT WM_GAME = WM_APP + 3;
constexpr UINT WM_APP_HOTKEY = WM_APP + 4;
constexpr UINT WM_ERROR = WM_APP + 5;
constexpr wchar_t PIPE_NAME[] = L"\\\\.\\pipe\\ArcadePiDisplayGameEvents";
constexpr int ICON_CONNECTED = 101, ICON_DISCONNECTED = 102;
enum MenuId {
    M_MEDIA = 1001, M_DEFAULT, M_RELOAD, M_REBOOT, M_SHUTDOWN,
    M_SETTINGS, M_OPEN_INI, M_LOAD_INI, M_EXIT
};
enum SettingId {
    S_URL = 2001, S_TOKEN, S_SHOW_TOKEN, S_HELP, S_HOTKEY,
    S_STARTUP, S_MEDIA, S_OPEN_INI, S_SAVE, S_CANCEL,
    S_GESTURE0 = 2020
};
enum MediaId { D_LIST = 3001, D_ADD, D_PREVIEW, D_ACTIVATE, D_REMOVE };
struct StatusUpdate { bool connected; std::wstring title; };
struct SettingsWindow;
struct MediaWindow;
class App;
static App* currentApp = nullptr;
static const wchar_t* ACTION_LABELS[] = {
    L"Keine Aktion", L"Marquee anzeigen", L"Box Art anzeigen",
    L"LaunchBox-Logo anzeigen", L"Steuerungsbelegung anzeigen",
    L"Standardanimation anzeigen", L"RetroArch-Menü öffnen"
};
static const char* ACTION_IDS[] = {
    "none", "marquee", "box_art", "logo", "controls", "default", "retroarch_menu"
};
static const char* GESTURE_IDS[] = { "swipe-down", "swipe-up", "swipe-right", "swipe-left" };

static void alert(HWND owner, const std::wstring& message, const wchar_t* title = L"Marquee-Pi") {
    MessageBoxW(owner, message.c_str(), title, MB_OK | MB_ICONINFORMATION);
}
static std::wstring readText(HWND control) {
    int n = GetWindowTextLengthW(control);
    std::wstring text(size_t(n) + 1, L'\0');
    GetWindowTextW(control, text.data(), n + 1);
    text.resize(n);
    size_t a = text.find_first_not_of(L" \t\r\n"), b = text.find_last_not_of(L" \t\r\n");
    return a == std::wstring::npos ? L"" : text.substr(a, b - a + 1);
}
static HWND control(HWND parent, const wchar_t* type, const wchar_t* title,
                    DWORD style, int x, int y, int width, int height, int id = 0) {
    HWND h = CreateWindowExW(0, type, title, style | WS_CHILD | WS_VISIBLE,
                             x, y, width, height, parent, (HMENU)(INT_PTR)id,
                             GetModuleHandleW(nullptr), nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    return h;
}
static std::wstring timeStamp() {
    SYSTEMTIME t{}; GetLocalTime(&t);
    wchar_t value[64];
    swprintf(value, 64, L"%04u-%02u-%02uT%02u:%02u:%02u", t.wYear, t.wMonth, t.wDay,
             t.wHour, t.wMinute, t.wSecond);
    return value;
}
static void logShutdown(const std::wstring& line) {
    try {
        std::wstring path = dataDirectory() + L"\\shutdown.log";
        HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;
        std::string data = toUtf8(timeStamp() + L" " + line + L"\r\n");
        DWORD written = 0; WriteFile(h, data.data(), DWORD(data.size()), &written, nullptr);
        CloseHandle(h);
    } catch (...) {}
}
static std::wstring extractTag(const std::wstring& xml, const std::wstring& marker) {
    size_t start = xml.find(marker);
    if (start == std::wstring::npos) return L"";
    start = xml.find(L'>', start);
    if (start == std::wstring::npos) return L"";
    size_t end = xml.find(L"</Data>", start + 1);
    if (end == std::wstring::npos) return L"";
    return xml.substr(start + 1, end - start - 1);
}
static std::wstring recentShutdownType(FILETIME startedUtc) {
    EVT_HANDLE query = EvtQuery(nullptr, L"System", L"*[System[(EventID=1074)]]", EvtQueryChannelPath | EvtQueryReverseDirection);
    if (!query) return L"";
    std::wstring result;
    for (int i = 0; i < 10 && result.empty(); ++i) {
        EVT_HANDLE event = nullptr;
        DWORD returned = 0;
        if (!EvtNext(query, 1, &event, 0, 0, &returned)) break;
        DWORD needed = 0, count = 0;
        EvtRender(nullptr, event, EvtRenderEventXml, 0, nullptr, &needed, &count);
        std::vector<wchar_t> buffer(needed / sizeof(wchar_t) + 1);
        if (EvtRender(nullptr, event, EvtRenderEventXml, DWORD(buffer.size() * sizeof(wchar_t)),
                      buffer.data(), &needed, &count)) {
            std::wstring xml(buffer.data());
            if (xml.find(L"Microsoft-Windows-User32") != std::wstring::npos ||
                xml.find(L"Name=\"User32\"") != std::wstring::npos) {
                size_t t = xml.find(L"SystemTime=");
                bool recent = false;
                if (t != std::wstring::npos && t + 22 < xml.size()) {
                    wchar_t q = xml[t + 11];
                    std::wstring iso = xml.substr(t + 12, 19);
                    if ((q == L'\'' || q == L'"') && iso.size() == 19) {
                        SYSTEMTIME st{};
                        int values[6]{};
                        if (swscanf(iso.c_str(), L"%d-%d-%dT%d:%d:%d",
                                   &values[0], &values[1], &values[2],
                                   &values[3], &values[4], &values[5]) == 6) {
                            st.wYear = WORD(values[0]); st.wMonth = WORD(values[1]); st.wDay = WORD(values[2]);
                            st.wHour = WORD(values[3]); st.wMinute = WORD(values[4]); st.wSecond = WORD(values[5]);
                            FILETIME a{}, b{}; SYSTEMTIME now{}; GetSystemTime(&now);
                            if (SystemTimeToFileTime(&st, &a) && SystemTimeToFileTime(&now, &b)) {
                                ULARGE_INTEGER first{}, last{};
                                first.LowPart = a.dwLowDateTime; first.HighPart = a.dwHighDateTime;
                                last.LowPart = b.dwLowDateTime; last.HighPart = b.dwHighDateTime;
                                ULARGE_INTEGER started{};
                                started.LowPart = startedUtc.dwLowDateTime;
                                started.HighPart = startedUtc.dwHighDateTime;
                                recent = first.QuadPart >= started.QuadPart &&
                                         last.QuadPart >= first.QuadPart &&
                                         last.QuadPart - first.QuadPart < 120ULL * 10000000ULL;
                            }
                        }
                    }
                }
                if (recent) {
                    result = extractTag(xml, L"Name=\"param5\"");
                    if (result.empty()) result = extractTag(xml, L"Name='param5'");
                }
            }
        }
        EvtClose(event);
    }
    EvtClose(query);
    return result;
}
static bool isPowerOff(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return std::towlower(c); });
    return value == L"shutdown" || value == L"power off" ||
           value == L"herunterfahren" || value == L"ausschalten";
}

class App {
public:
    HWND hwnd = nullptr, settingsWindow = nullptr, mediaWindow = nullptr;
    HICON onlineIcon = nullptr, offlineIcon = nullptr;
    Settings settings;
    std::mutex mutex;
    std::optional<GameMessage> game;
    uint64_t gameVersion = 0, configVersion = 0;
    bool needsSync = false, pendingDefault = false, gestureDirty = true;
    std::atomic<bool> stopping{false};
    std::atomic<HANDLE> activePipe{INVALID_HANDLE_VALUE};
    std::thread polling, pipe;
    UINT taskbarCreated = 0;
    bool connected = false;
    FILETIME startedUtc{};
    std::wstring status = L"Pi: Verbindung wird geprüft";

    explicit App(Settings initial) : settings(std::move(initial)) {}
    bool start(HINSTANCE instance);
    void close();
    void addIcon(bool add);
    void setStatus(const StatusUpdate& update);
    void menu();
    void command(int id);
    void showSettings();
    void showMedia();
    void reloadSettings();
    void onGame(GameMessage message);
    void pollLoop();
    void pipeLoop();
    void onShutdown();
    void postError(const std::wstring& message) {
        auto* text = new std::wstring(message);
        if (!PostMessageW(hwnd, WM_ERROR, 0, (LPARAM)text)) delete text;
    }
};

static LRESULT CALLBACK mainProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp);
static LRESULT CALLBACK settingsProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp);
static LRESULT CALLBACK mediaProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp);

bool App::start(HINSTANCE instance) {
    WNDCLASSW klass{};
    klass.lpfnWndProc = mainProc;
    klass.hInstance = instance;
    klass.lpszClassName = L"MarqueePiTrayWindow";
    if (!RegisterClassW(&klass)) return false;
    hwnd = CreateWindowExW(0, klass.lpszClassName, L"Marquee-Pi", WS_OVERLAPPED,
                           0, 0, 0, 0, nullptr, nullptr, instance, this);
    if (!hwnd) return false;
    onlineIcon = LoadIconW(instance, MAKEINTRESOURCEW(ICON_CONNECTED));
    offlineIcon = LoadIconW(instance, MAKEINTRESOURCEW(ICON_DISCONNECTED));
    if (!onlineIcon || !offlineIcon) return false;
    GetSystemTimeAsFileTime(&startedUtc);
    taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    addIcon(true);
    try {
        if (settings.autostart || autostartEnabled() != settings.autostart)
            setAutostart(settings.autostart);
    } catch (const std::exception& e) { alert(hwnd, errorText(e), L"Autostart"); }
    polling = std::thread(&App::pollLoop, this);
    pipe = std::thread(&App::pipeLoop, this);
    return true;
}
void App::close() {
    stopping = true;
    if (hwnd) {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data); data.hWnd = hwnd; data.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &data);
    }
    HANDLE active = activePipe.load();
    if (active != INVALID_HANDLE_VALUE) CancelIoEx(active, nullptr);
    HANDLE wake = CreateFileW(PIPE_NAME, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (wake != INVALID_HANDLE_VALUE) {
        DWORD n = 0; WriteFile(wake, "\n", 1, &n, nullptr); CloseHandle(wake);
    }
    if (pipe.joinable()) pipe.join();
    if (polling.joinable()) polling.join();
}
void App::addIcon(bool add) {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = hwnd;
    data.uID = 1;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = WM_TRAY;
    data.hIcon = connected ? onlineIcon : offlineIcon;
    wcscpy_s(data.szTip, connected ? L"Marquee-Pi - Pi verbunden" : L"Marquee-Pi - Pi nicht erreichbar");
    Shell_NotifyIconW(add ? NIM_ADD : NIM_MODIFY, &data);
}
void App::setStatus(const StatusUpdate& update) {
    connected = update.connected;
    status = connected ? L"Pi verbunden - " + update.title : L"Pi nicht erreichbar";
    addIcon(false);
}
void App::menu() {
    HMENU popup = CreatePopupMenu();
    AppendMenuW(popup, MF_STRING | MF_GRAYED, 0, status.c_str());
    AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(popup, MF_STRING, M_MEDIA, L"Standardmedien verwalten...");
    AppendMenuW(popup, MF_STRING, M_DEFAULT, L"Standardlogo anzeigen");
    AppendMenuW(popup, MF_STRING, M_RELOAD, L"Anzeige neu laden");
    AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(popup, MF_STRING, M_REBOOT, L"Pi neu starten");
    AppendMenuW(popup, MF_STRING, M_SHUTDOWN, L"Pi herunterfahren");
    AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(popup, MF_STRING, M_SETTINGS, L"Einstellungen...");
    AppendMenuW(popup, MF_STRING, M_OPEN_INI, L"INI-Datei öffnen");
    AppendMenuW(popup, MF_STRING, M_LOAD_INI, L"Einstellungen neu laden");
    AppendMenuW(popup, MF_STRING, M_EXIT, L"Programm beenden");
    POINT point{}; GetCursorPos(&point);
    SetForegroundWindow(hwnd);
    int id = TrackPopupMenu(popup, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, hwnd, nullptr);
    DestroyMenu(popup);
    if (id) command(id);
}
void App::command(int id) {
    if (id == M_SETTINGS) { showSettings(); return; }
    if (id == M_MEDIA) { showMedia(); return; }
    if (id == M_OPEN_INI) {
        ShellExecuteW(hwnd, L"open", settingsPath().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    if (id == M_LOAD_INI) { reloadSettings(); return; }
    if (id == M_EXIT) { DestroyWindow(hwnd); return; }
    if (id == M_REBOOT || id == M_SHUTDOWN) {
        const wchar_t* prompt = id == M_REBOOT ? L"Pi wirklich neu starten?" :
            L"Pi wirklich herunterfahren? Er startet erst nach einem neuen Stromzyklus.";
        if (MessageBoxW(hwnd, prompt, L"Marquee-Pi", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    }
    try {
        Settings copy;
        { std::lock_guard<std::mutex> guard(mutex); copy = settings; }
        const wchar_t* endpoint = id == M_DEFAULT ? L"/v1/default" :
                                  id == M_RELOAD ? L"/v1/reload" :
                                  id == M_REBOOT ? L"/v1/reboot" : L"/v1/shutdown";
        piRequest(copy, L"POST", endpoint);
        if (id == M_DEFAULT) {
            std::lock_guard<std::mutex> guard(mutex);
            game.reset(); needsSync = false; pendingDefault = false; ++gameVersion;
        }
    } catch (const std::exception& error) { alert(hwnd, errorText(error)); }
}
void App::reloadSettings() {
    try {
        Settings loaded = loadSettings();
        if (!loaded.configured() || !validHotkey(loaded.hotkey))
            throw std::runtime_error("INI has invalid Pi URL, token or hotkey");
        setAutostart(loaded.autostart);
        {
            std::lock_guard<std::mutex> guard(mutex);
            settings = loaded; gestureDirty = true; needsSync = game.has_value(); ++configVersion;
        }
    } catch (const std::exception& error) { alert(hwnd, errorText(error), L"INI neu laden"); }
}
void App::onGame(GameMessage message) {
    std::lock_guard<std::mutex> guard(mutex);
    if (message.action == "game") {
        game = std::move(message);
        needsSync = true; pendingDefault = false; ++gameVersion;
    } else if (message.action == "exit") {
        game.reset(); needsSync = false; pendingDefault = true; ++gameVersion;
    }
}
void App::pollLoop() {
    std::string instance;
    int64_t cursor = 0;
    int cycle = 0, heartbeat = 0;
    bool online = false;
    while (!stopping) {
        Settings copy;
        std::optional<GameMessage> current;
        uint64_t version = 0;
        bool dirty, sync, reset;
        {
            std::lock_guard<std::mutex> guard(mutex);
            copy = settings; current = game; version = gameVersion;
            dirty = gestureDirty; sync = needsSync; reset = pendingDefault;
        }
        if (cycle % 7 == 0) {
            try {
                auto response = piRequest(copy, L"GET", L"/v1/status");
                auto json = mini::parse(response.body);
                online = true;
                std::wstring title = fromUtf8(json.get("game_title").value("Standardmedium"));
                if (title.empty()) title = L"Standardmedium";
                auto* update = new StatusUpdate{true, title};
                if (!PostMessageW(hwnd, WM_STATUS, 0, (LPARAM)update)) delete update;

            } catch (...) {
                online = false;
                auto* update = new StatusUpdate{false, L""};
                if (!PostMessageW(hwnd, WM_STATUS, 0, (LPARAM)update)) delete update;
                std::lock_guard<std::mutex> guard(mutex);
                gestureDirty = true;
                if (game) needsSync = true;
            }
        }
        if (online && copy.configured()) {
            if (cycle % 7 == 0 && current && ++heartbeat % 3 == 0) {
                try { piRequest(copy, L"POST", L"/v1/heartbeat"); } catch (...) {}
            }
            try {
                if (dirty) {
                    piRequest(copy, L"POST", L"/v1/gesture-config", gesturePayload(copy),
                              L"application/json; charset=utf-8");
                    std::lock_guard<std::mutex> guard(mutex);
                    if (configVersion == 0 || settings.gestures == copy.gestures) gestureDirty = false;
                }
                if (reset) {
                    piRequest(copy, L"POST", L"/v1/default");
                    std::lock_guard<std::mutex> guard(mutex);
                    if (gameVersion == version) pendingDefault = false;
                } else if (sync && current) {
                    piRequest(copy, L"POST", L"/v1/game", gamePayload(*current),
                              L"application/json; charset=utf-8");
                    std::lock_guard<std::mutex> guard(mutex);
                    if (gameVersion == version) needsSync = false;
                }
            } catch (...) {}
            try {
                auto response = piRequest(copy, L"GET", L"/v1/gesture-events?after=" + std::to_wstring(cursor));
                auto events = mini::parse(response.body);
                std::string nextInstance = events.get("instance_id").value();
                if (nextInstance != instance) {
                    instance = nextInstance; cursor = 0;
                    std::lock_guard<std::mutex> guard(mutex);
                    gestureDirty = true;
                    if (game) needsSync = true;
                } else {
                    for (const auto& event : events.get("events").items) {
                        int64_t id = event.get("id").integer();
                        if (id <= cursor) continue;
                        cursor = id;
                        if (current && event.get("action").value() == "retroarch_menu")
                            PostMessageW(hwnd, WM_APP_HOTKEY, 0, 0);
                    }
                }
            } catch (...) {}
        }
        ++cycle;
        for (int i = 0; i < 15 && !stopping; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}
void App::pipeLoop() {
    while (!stopping) {
        HANDLE handle = CreateNamedPipeW(PIPE_NAME, PIPE_ACCESS_INBOUND,
                                         PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                         1, 131072, 131072, 0, nullptr);
        if (handle == INVALID_HANDLE_VALUE) { std::this_thread::sleep_for(std::chrono::seconds(1)); continue; }
        activePipe = handle;
        bool connectedPipe = ConnectNamedPipe(handle, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
        if (connectedPipe) {
            std::string line;
            char block[4096];
            DWORD read = 0;
            while (line.size() < 100000 && ReadFile(handle, block, sizeof(block), &read, nullptr) && read) {
                line.append(block, read);
                if (line.find('\n') != std::string::npos) break;
            }
            size_t end = line.find('\n');
            if (end != std::string::npos) line.resize(end);
            if (!stopping && !line.empty() && line.size() <= 100000) {
                try {
                    auto* game = new GameMessage(parseGameMessage(line));
                    if (!PostMessageW(hwnd, WM_GAME, 0, (LPARAM)game)) delete game;
                } catch (...) {}
            }
        }
        DisconnectNamedPipe(handle);
        CloseHandle(handle);
    }
}
void App::onShutdown() {
    std::wstring type = recentShutdownType(startedUtc);
    if (!isPowerOff(type)) {
        logShutdown(L"Pi bleibt eingeschaltet; Windows-Typ: " + (type.empty() ? L"unbekannt" : type));
        return;
    }
    try {
        Settings copy;
        { std::lock_guard<std::mutex> guard(mutex); copy = settings; }
        piRequest(copy, L"POST", L"/v1/shutdown", {}, L"", L"", 3000);
        logShutdown(L"Pi-Shutdown-Befehl gesendet; Windows-Typ: " + type);
    } catch (const std::exception& error) {
        logShutdown(L"Pi-Shutdown fehlgeschlagen: " + errorText(error));
    }
}

struct SettingsWindow {
    App* app = nullptr;
    HWND hwnd = nullptr, url = nullptr, token = nullptr, hotkey = nullptr, autostart = nullptr;
    HWND showToken = nullptr, gestures[4]{};
    explicit SettingsWindow(App* owner) : app(owner) {}
    void create() {
        Settings current;
        { std::lock_guard<std::mutex> guard(app->mutex); current = app->settings; }
        control(hwnd, L"STATIC", L"Verbindung zum Pi", WS_GROUP, 20, 15, 220, 24);
        control(hwnd, L"STATIC", L"Pi-Adresse", 0, 20, 48, 160, 22);
        url = control(hwnd, L"EDIT", current.piUrl.c_str(), WS_BORDER | ES_AUTOHSCROLL,
                      190, 45, 490, 25, S_URL);
        control(hwnd, L"STATIC", L"Zugriffstoken", 0, 20, 83, 160, 22);
        token = control(hwnd, L"EDIT", current.token.c_str(), WS_BORDER | ES_AUTOHSCROLL | ES_PASSWORD,
                        190, 80, 490, 25, S_TOKEN);
        showToken = control(hwnd, L"BUTTON", L"Token anzeigen", BS_AUTOCHECKBOX,
                            190, 112, 135, 26, S_SHOW_TOKEN);
        control(hwnd, L"BUTTON", L"Hilfe: Token erstellen", BS_PUSHBUTTON,
                340, 112, 180, 27, S_HELP);
        control(hwnd, L"STATIC", L"Wischgesten", WS_GROUP, 20, 155, 220, 22);
        const wchar_t* names[] = {L"Oben nach unten", L"Unten nach oben", L"Links nach rechts", L"Rechts nach links"};
        for (int i = 0; i < 4; ++i) {
            int y = 184 + i * 35;
            control(hwnd, L"STATIC", names[i], 0, 20, y + 3, 165, 22);
            HWND combo = control(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
                                 190, y, 360, 250, S_GESTURE0 + i);
            gestures[i] = combo;
            for (const wchar_t* label : ACTION_LABELS) SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)label);
            std::string selected = "none";
            auto it = current.gestures.find(GESTURE_IDS[i]);
            if (it != current.gestures.end()) selected = it->second;
            int index = 0;
            for (int j = 0; j < 7; ++j) if (selected == ACTION_IDS[j]) index = j;
            SendMessageW(combo, CB_SETCURSEL, index, 0);
        }
        control(hwnd, L"STATIC", L"RetroArch-Tastenkombination", 0, 20, 332, 170, 22);
        hotkey = control(hwnd, L"EDIT", current.hotkey.c_str(), WS_BORDER | ES_AUTOHSCROLL,
                         190, 329, 160, 25, S_HOTKEY);
        control(hwnd, L"STATIC", L"Beispiele: F1, Ctrl+F1, Shift+F1 (nur im aktiven RetroArch-Fenster).",
                0, 190, 358, 480, 32);
        control(hwnd, L"STATIC", L"Allgemein", WS_GROUP, 20, 393, 220, 22);
        autostart = control(hwnd, L"BUTTON", L"Mit Windows starten", BS_AUTOCHECKBOX,
                            20, 422, 180, 26, S_STARTUP);
        SendMessageW(autostart, BM_SETCHECK, current.autostart ? BST_CHECKED : BST_UNCHECKED, 0);
        control(hwnd, L"BUTTON", L"Standardmedien verwalten...", BS_PUSHBUTTON,
                220, 420, 210, 28, S_MEDIA);
        control(hwnd, L"BUTTON", L"INI-Datei öffnen", BS_PUSHBUTTON,
                445, 420, 150, 28, S_OPEN_INI);
        control(hwnd, L"BUTTON", L"Speichern", BS_DEFPUSHBUTTON,
                475, 465, 100, 30, S_SAVE);
        control(hwnd, L"BUTTON", L"Abbrechen", BS_PUSHBUTTON,
                585, 465, 100, 30, S_CANCEL);
    }
    void save() {
        Settings next;
        { std::lock_guard<std::mutex> guard(app->mutex); next = app->settings; }
        next.piUrl = readText(url);
        next.token = readText(token);
        next.hotkey = readText(hotkey);
        next.autostart = SendMessageW(autostart, BM_GETCHECK, 0, 0) == BST_CHECKED;
        for (int i = 0; i < 4; ++i) {
            int selected = int(SendMessageW(gestures[i], CB_GETCURSEL, 0, 0));
            next.gestures[GESTURE_IDS[i]] = ACTION_IDS[selected >= 0 && selected < 7 ? selected : 0];
        }
        if (!next.configured()) { alert(hwnd, L"Bitte HTTP-Pi-Adresse und Token mit mindestens 24 Zeichen eingeben."); return; }
        if (!validHotkey(next.hotkey)) { alert(hwnd, L"Ungültige Tastenkombination. Beispiel: Ctrl+Shift+F1."); return; }
        try {
            setAutostart(next.autostart);
            saveSettings(next);
            {
                std::lock_guard<std::mutex> guard(app->mutex);
                app->settings = next; app->gestureDirty = true;
                app->needsSync = app->game.has_value(); ++app->configVersion;
            }
            DestroyWindow(hwnd);
        } catch (const std::exception& error) { alert(hwnd, errorText(error), L"Einstellungen"); }
    }
};
void App::showSettings() {
    if (settingsWindow) { SetForegroundWindow(settingsWindow); return; }
    static bool registered = false;
    if (!registered) {
        WNDCLASSW klass{}; klass.lpfnWndProc = settingsProc;
        klass.hInstance = GetModuleHandleW(nullptr); klass.lpszClassName = L"MarqueePiSettings";
        klass.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        klass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        registered = RegisterClassW(&klass) != 0;
    }
    auto* state = new SettingsWindow(this);
    HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME, L"MarqueePiSettings",
                                 L"Marquee-Pi - Einstellungen", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                 CW_USEDEFAULT, CW_USEDEFAULT, 725, 550, hwnd, nullptr,
                                 GetModuleHandleW(nullptr), state);
    if (!window) { delete state; alert(hwnd, L"Einstellungsfenster konnte nicht geöffnet werden."); return; }
    settingsWindow = window;
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
}
static LRESULT CALLBACK settingsProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    auto* state = (SettingsWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (message == WM_NCCREATE) {
        state = (SettingsWindow*)((CREATESTRUCTW*)lp)->lpCreateParams;
        state->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)state);
    }
    if (!state) return DefWindowProcW(hwnd, message, wp, lp);
    switch (message) {
    case WM_CREATE: state->create(); return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case S_SHOW_TOKEN:
            if (HIWORD(wp) == BN_CLICKED) {
                bool show = SendMessageW(state->showToken, BM_GETCHECK, 0, 0) == BST_CHECKED;
                SendMessageW(state->token, EM_SETPASSWORDCHAR, show ? 0 : L'*', 0);
                InvalidateRect(state->token, nullptr, TRUE);
            }
            return 0;
        case S_HELP:
            alert(hwnd,
                  L"1. Per SSH am Raspberry Pi anmelden.\n\n"
                  L"2. Zufälligen Token erzeugen: openssl rand -hex 32\n\n"
                  L"3. Die 64 Zeichen in /etc/arcade-pi-display/config.json als Wert von "
                  L"\"token\" eintragen und den Platzhalter ersetzen.\n\n"
                  L"4. sudo systemctl restart arcade-pi-display.service\n\n"
                  L"5. Genau denselben Token hier eintragen und speichern. Nicht veröffentlichen.",
                  L"Zugriffstoken erstellen");
            return 0;
        case S_MEDIA: state->app->showMedia(); return 0;
        case S_OPEN_INI:
            ShellExecuteW(hwnd, L"open", settingsPath().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        case S_SAVE: state->save(); return 0;
        case S_CANCEL: DestroyWindow(hwnd); return 0;
        }
        break;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_NCDESTROY:
        state->app->settingsWindow = nullptr;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        delete state;
        return 0;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}

struct MediaWindow {
    App* app = nullptr;
    HWND hwnd = nullptr, list = nullptr, activeLabel = nullptr;
    explicit MediaWindow(App* owner) : app(owner) {}
    std::wstring activeName() {
        try {
            std::wstring text = fromUtf8(readFile(dataDirectory() + L"\\active-media.txt"));
            while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n')) text.pop_back();
            return text;
        } catch (...) { return L""; }
    }
    void refresh() {
        SendMessageW(list, LB_RESETCONTENT, 0, 0);
        fs::create_directories(fs::path(mediaDirectory()));
        std::vector<std::wstring> names;
        for (auto& file : fs::directory_iterator(fs::path(mediaDirectory())))
            if (file.is_regular_file()) names.push_back(file.path().filename().wstring());
        std::sort(names.begin(), names.end());
        for (const auto& name : names) SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)name.c_str());
        std::wstring active = activeName();
        std::wstring label = L"Aktiv: " + (active.empty() ? L"kein eigenes Medium" : active);
        SetWindowTextW(activeLabel, label.c_str());
    }
    std::wstring selected() {
        int index = int(SendMessageW(list, LB_GETCURSEL, 0, 0));
        if (index == LB_ERR) return L"";
        int length = int(SendMessageW(list, LB_GETTEXTLEN, index, 0));
        if (length <= 0 || length > 300) return L"";
        std::wstring name(size_t(length) + 1, L'\0');
        SendMessageW(list, LB_GETTEXT, index, (LPARAM)name.data());
        name.resize(length);
        return name;
    }
    void create() {
        activeLabel = control(hwnd, L"STATIC", L"Aktiv:", 0, 15, 15, 540, 26);
        list = control(hwnd, L"LISTBOX", L"", LBS_NOTIFY | WS_BORDER | WS_VSCROLL,
                       15, 45, 540, 250, D_LIST);
        control(hwnd, L"BUTTON", L"Hinzufügen", BS_PUSHBUTTON, 15, 315, 110, 30, D_ADD);
        control(hwnd, L"BUTTON", L"Vorschau", BS_PUSHBUTTON, 135, 315, 100, 30, D_PREVIEW);
        control(hwnd, L"BUTTON", L"Auf Pi aktivieren", BS_PUSHBUTTON, 245, 315, 155, 30, D_ACTIVATE);
        control(hwnd, L"BUTTON", L"Entfernen", BS_PUSHBUTTON, 410, 315, 100, 30, D_REMOVE);
        refresh();
    }
    void add() {
        wchar_t chosen[32768]{};
        wchar_t filter[] = L"Unterstützte Medien\0*.jpg;*.jpeg;*.png;*.gif;*.webp;*.mp4\0Alle Dateien\0*.*\0\0";
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = hwnd; dialog.lpstrFilter = filter;
        dialog.lpstrFile = chosen; dialog.nMaxFile = 32768;
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (!GetOpenFileNameW(&dialog)) return;
        try {
            fs::path source(chosen);
            auto length = fs::file_size(source);
            if (length > 20 * 1024 * 1024) throw std::runtime_error("File exceeds 20 MB upload limit");
            std::wstring ext = source.extension().wstring();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
            if (ext != L".jpg" && ext != L".jpeg" && ext != L".png" &&
                ext != L".gif" && ext != L".webp" && ext != L".mp4")
                throw std::runtime_error("Unsupported file type");
            fs::path target = fs::path(mediaDirectory()) / source.filename();
            if (fs::exists(target)) {
                std::wstring suffix = L"-" + std::to_wstring(GetTickCount64());
                target = fs::path(mediaDirectory()) / (source.stem().wstring() + suffix + ext);
            }
            fs::copy_file(source, target);
            refresh();
            SendMessageW(list, LB_SELECTSTRING, (WPARAM)-1, (LPARAM)target.filename().c_str());
        } catch (const std::exception& error) { alert(hwnd, errorText(error)); }
    }
    void preview() {
        std::wstring name = selected();
        if (name.empty()) return;
        std::wstring path = (fs::path(mediaDirectory()) / name).wstring();
        ShellExecuteW(hwnd, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    void activate() {
        std::wstring name = selected();
        if (name.empty()) return;
        try {
            Settings settings;
            { std::lock_guard<std::mutex> guard(app->mutex); settings = app->settings; }
            std::wstring path = (fs::path(mediaDirectory()) / name).wstring();
            std::string body = readFile(path);
            if (body.size() > 20 * 1024 * 1024) throw std::runtime_error("File exceeds 20 MB upload limit");
            std::wstring ext = fs::path(name).extension().wstring();
            piRequest(settings, L"POST", L"/v1/default-media", body,
                      L"application/octet-stream", L"X-File-Name: upload" + ext + L"\r\n");
            writeFile(dataDirectory() + L"\\active-media.txt", toUtf8(name));
            refresh();
            alert(hwnd, L"Das Medium ist auf dem Pi aktiv.");
        } catch (const std::exception& error) { alert(hwnd, errorText(error), L"Upload fehlgeschlagen"); }
    }
    void remove() {
        std::wstring name = selected();
        if (name.empty()) return;
        if (name == activeName()) { alert(hwnd, L"Bitte zuerst ein anderes Medium aktivieren."); return; }
        std::wstring path = (fs::path(mediaDirectory()) / name).wstring();
        if (!DeleteFileW(path.c_str())) { alert(hwnd, L"Datei konnte nicht entfernt werden."); return; }
        refresh();
    }
};
void App::showMedia() {
    if (mediaWindow) { SetForegroundWindow(mediaWindow); return; }
    static bool registered = false;
    if (!registered) {
        WNDCLASSW klass{}; klass.lpfnWndProc = mediaProc;
        klass.hInstance = GetModuleHandleW(nullptr); klass.lpszClassName = L"MarqueePiMedia";
        klass.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        klass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        registered = RegisterClassW(&klass) != 0;
    }
    auto* state = new MediaWindow(this);
    HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME, L"MarqueePiMedia",
                                 L"Marquee-Pi - Standardmedien", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                 CW_USEDEFAULT, CW_USEDEFAULT, 590, 400, hwnd, nullptr,
                                 GetModuleHandleW(nullptr), state);
    if (!window) { delete state; alert(hwnd, L"Medienfenster konnte nicht geöffnet werden."); return; }
    mediaWindow = window;
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
}
static LRESULT CALLBACK mediaProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    auto* state = (MediaWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (message == WM_NCCREATE) {
        state = (MediaWindow*)((CREATESTRUCTW*)lp)->lpCreateParams;
        state->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)state);
    }
    if (!state) return DefWindowProcW(hwnd, message, wp, lp);
    switch (message) {
    case WM_CREATE:
        try { state->create(); } catch (const std::exception& error) { alert(hwnd, errorText(error)); }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case D_ADD: state->add(); return 0;
        case D_PREVIEW: state->preview(); return 0;
        case D_ACTIVATE: state->activate(); return 0;
        case D_REMOVE: state->remove(); return 0;
        }
        break;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_NCDESTROY:
        state->app->mediaWindow = nullptr;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        delete state;
        return 0;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}
static LRESULT CALLBACK mainProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    auto* app = (App*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (message == WM_NCCREATE) {
        app = (App*)((CREATESTRUCTW*)lp)->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)app);
    }
    if (!app) return DefWindowProcW(hwnd, message, wp, lp);
    if (message == app->taskbarCreated && app->taskbarCreated) { app->addIcon(true); return 0; }
    switch (message) {
    case WM_COMMAND: app->command(LOWORD(wp)); return 0;
    case WM_TRAY:
        if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU) app->menu();
        else if (LOWORD(lp) == WM_LBUTTONDBLCLK) app->showMedia();
        return 0;
    case WM_STATUS: {
        auto* status = (StatusUpdate*)lp;
        app->setStatus(*status); delete status; return 0;
    }
    case WM_GAME: {
        auto* game = (GameMessage*)lp;
        app->onGame(std::move(*game)); delete game; return 0;
    }
    case WM_APP_HOTKEY: {
        Settings settings;
        bool inGame;
        { std::lock_guard<std::mutex> guard(app->mutex); settings = app->settings; inGame = app->game.has_value(); }
        if (inGame) sendRetroArchHotkey(settings.hotkey);
        return 0;
    }
    case WM_ERROR: {
        auto* error = (std::wstring*)lp;
        alert(hwnd, *error); delete error; return 0;
    }
    case WM_ENDSESSION:
        if (wp && !(lp & ENDSESSION_CLOSEAPP) && !(lp & ENDSESSION_LOGOFF)) app->onShutdown();
        return 0;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY: {
        NOTIFYICONDATAW data{}; data.cbSize = sizeof(data); data.hWnd = hwnd; data.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &data);
        PostQuitMessage(0); return 0;
    }
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int) {
    try {
        Settings settings = loadSettings();
        std::wstring command = commandLine ? commandLine : L"";
        if (command.find(L"--pi-shutdown") != std::wstring::npos) {
            try { piRequest(settings, L"POST", L"/v1/shutdown", {}, L"", L"", 3000); return 0; }
            catch (...) { return 1; }
        }
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&controls);
        App app(std::move(settings));
        currentApp = &app;
        if (!app.start(instance)) {
            alert(nullptr, L"Marquee-Pi konnte nicht gestartet werden.");
            return 1;
        }
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        app.close();
        currentApp = nullptr;
        return int(message.wParam);
    } catch (const std::exception& error) {
        alert(nullptr, errorText(error), L"Marquee-Pi Startfehler");
        return 1;
    }
}
