#include "core.hpp"
#include "strings.hpp"
#include "thumbnail.hpp"
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
#include <set>
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
constexpr wchar_t PIPE_NAME[] = L"\\\\.\\pipe\\MarqueePiGameEvents";
constexpr int ICON_CONNECTED = 101, ICON_DISCONNECTED = 102;
enum MenuId {
    M_MEDIA = 1001, M_DEFAULT, M_RELOAD, M_REBOOT, M_SHUTDOWN,
    M_SETTINGS, M_OPEN_INI, M_LOAD_INI, M_EXIT
};
enum SettingId {
    S_URL = 2001, S_TOKEN, S_SHOW_TOKEN, S_HELP, S_HOTKEY, S_RETRO_MODE, S_RETRO_PORT,
    S_STARTUP, S_OPEN_INI, S_SAVE, S_CANCEL,
    S_GESTURE0 = 2020,
    S_TABS = 2040,
    S_LANGUAGE = 2041
};
enum MediaId { D_LIST = 3001, D_ADD, D_PREVIEW, D_ACTIVATE, D_BOOT, D_SHUTDOWN, D_REMOVE };
struct StatusUpdate { bool connected; std::wstring title; };
struct SettingsWindow;
class App;
static App* currentApp = nullptr;
static const char* ACTION_IDS[] = {
    "none", "marquee", "box_art", "logo", "controls", "default", "retroarch_menu", "touch_menu"
};
static constexpr int ACTION_COUNT = int(sizeof(ACTION_IDS) / sizeof(ACTION_IDS[0]));
static_assert(int(Str::ActionTouchMenu) - int(Str::ActionNone) + 1 == ACTION_COUNT, "action labels out of sync");
static const char* GESTURE_IDS[] = { "long-press", "swipe-down", "swipe-up", "swipe-right", "swipe-left" };
static constexpr int GESTURE_COUNT = int(sizeof(GESTURE_IDS) / sizeof(GESTURE_IDS[0]));
static_assert(int(Str::GestureLeft) - int(Str::GestureLongPress) + 1 == GESTURE_COUNT, "gesture labels out of sync");
enum SettingsPage { PAGE_CONNECTION, PAGE_GESTURES, PAGE_MEDIA, PAGE_RETROARCH, PAGE_GENERAL, SETTINGS_PAGES };
static constexpr int THUMB_W = 112, THUMB_H = 70, MEDIA_ITEM_HEIGHT = 78;
constexpr UINT WM_SELECT_PAGE = WM_APP + 30, WM_THUMB = WM_APP + 31;

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
static void logLine(const wchar_t* filename, const std::wstring& line) {
    try {
        std::wstring path = dataDirectory() + L"\\" + filename;
        HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;
        std::string data = toUtf8(timeStamp() + L" " + line + L"\r\n");
        DWORD written = 0; WriteFile(h, data.data(), DWORD(data.size()), &written, nullptr);
        CloseHandle(h);
    } catch (...) {}
}
static void logShutdown(const std::wstring& line) { logLine(L"shutdown.log", line); }
static void logArtworkWarning(const std::string& line) {
    try { logLine(L"artwork-warnings.log", fromUtf8(line)); } catch (...) {}
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
            if (isUser32ShutdownEvent(xml)) {
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
class App {
public:
    HWND hwnd = nullptr, settingsWindow = nullptr;
    HICON onlineIcon = nullptr, offlineIcon = nullptr;
    Settings settings;
    std::mutex mutex;
    std::optional<GameMessage> game;
    uint64_t gameVersion = 0, configVersion = 0;
    bool needsSync = false, pendingDefault = false, gestureDirty = true;
    std::atomic<bool> stopping{false};
    std::mutex pipeHandleMutex;
    HANDLE activePipe = INVALID_HANDLE_VALUE;
    std::thread polling, pipe;
    UINT taskbarCreated = 0;
    bool connected = false;
    FILETIME startedUtc{};
    std::wstring status;

    explicit App(Settings initial) : settings(std::move(initial)) { status = tr(Str::StatusChecking); }
    bool start(HINSTANCE instance);
    void close();
    void addIcon(bool add);
    void setStatus(const StatusUpdate& update);
    void menu();
    void command(int id);
    void showSettings(int page = PAGE_CONNECTION);
    void showMedia() { showSettings(PAGE_MEDIA); }
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
    {
        std::lock_guard<std::mutex> guard(pipeHandleMutex);
        if (activePipe != INVALID_HANDLE_VALUE) CancelIoEx(activePipe, nullptr);
    }
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
    wcscpy_s(data.szTip, connected ? tr(Str::TipConnected) : tr(Str::TipDisconnected));
    Shell_NotifyIconW(add ? NIM_ADD : NIM_MODIFY, &data);
}
void App::setStatus(const StatusUpdate& update) {
    connected = update.connected;
    status = connected ? std::wstring(tr(Str::StatusConnected)) + update.title
                       : std::wstring(tr(Str::StatusUnreachable));
    addIcon(false);
}
void App::menu() {
    HMENU popup = CreatePopupMenu();
    AppendMenuW(popup, MF_STRING | MF_GRAYED, 0, status.c_str());
    AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(popup, MF_STRING, M_MEDIA, tr(Str::MenuMedia));
    AppendMenuW(popup, MF_STRING, M_DEFAULT, tr(Str::MenuDefault));
    AppendMenuW(popup, MF_STRING, M_RELOAD, tr(Str::MenuReload));
    AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(popup, MF_STRING, M_REBOOT, tr(Str::MenuReboot));
    AppendMenuW(popup, MF_STRING, M_SHUTDOWN, tr(Str::MenuShutdown));
    AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(popup, MF_STRING, M_SETTINGS, tr(Str::MenuSettings));
    AppendMenuW(popup, MF_STRING, M_OPEN_INI, tr(Str::MenuOpenIni));
    AppendMenuW(popup, MF_STRING, M_LOAD_INI, tr(Str::MenuReloadIni));
    AppendMenuW(popup, MF_STRING, M_EXIT, tr(Str::MenuExit));
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
        const wchar_t* prompt = id == M_REBOOT ? tr(Str::ConfirmReboot) : tr(Str::ConfirmShutdown);
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
        setUiLanguage(resolveLanguage(loaded.language));
        {
            std::lock_guard<std::mutex> guard(mutex);
            settings = loaded; gestureDirty = true; needsSync = game.has_value(); ++configVersion;
        }
    } catch (const std::exception& error) { alert(hwnd, errorText(error), tr(Str::TitleReloadIni)); }
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
    bool cursorReady = false;
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
                std::wstring title = fromUtf8(json.get("game_title").value(""));
                if (title.empty()) title = tr(Str::DefaultMediaTitle);
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
                    try {  // a Pi without language support answers 404; the gestures above still count
                        piRequest(copy, L"POST", L"/v1/language", languagePayload(copy),
                                  L"application/json; charset=utf-8");
                    } catch (const HttpError& error) {
                        if (error.status != 404) throw;
                    }
                    std::lock_guard<std::mutex> guard(mutex);
                    if (configVersion == 0 || (settings.gestures == copy.gestures &&
                                               settings.language == copy.language)) gestureDirty = false;
                }
                if (reset) {
                    piRequest(copy, L"POST", L"/v1/default");
                    std::lock_guard<std::mutex> guard(mutex);
                    if (gameVersion == version) pendingDefault = false;
                } else if (sync && current) {
                    try {
                        const std::string payload = gamePayload(*current);
                        for (const auto& problem : takeArtworkErrors())
                            logArtworkWarning("not sent: " + problem);
                        auto response = piRequest(copy, L"POST", L"/v1/game", payload,
                                                  L"application/json; charset=utf-8");
                        for (const auto& warning : gameWarnings(response.body))
                            logArtworkWarning(warning);
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
                auto response = piRequest(copy, L"GET", L"/v1/gesture-events?after=" + std::to_wstring(cursor));
                auto events = mini::parse(response.body);
                std::string nextInstance = events.get("instance_id").value();
                if (nextInstance != instance) {
                    instance = nextInstance; cursor = 0; cursorReady = false;
                    std::lock_guard<std::mutex> guard(mutex);
                    gestureDirty = true;
                    if (game) needsSync = true;
                } else {
                    const auto& received = events.get("events").items;
                    for (const auto& event : events.get("events").items) {
                        int64_t id = event.get("id").integer();
                        if (id <= cursor) continue;
                        cursor = id;
                        if (cursorReady && current && event.get("action").value() == "retroarch_menu")
                            PostMessageW(hwnd, WM_APP_HOTKEY, 0, 0);
                    }
                    if (!cursorReady && received.size() < 20) cursorReady = true;
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
                                         PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
                                             PIPE_REJECT_REMOTE_CLIENTS,
                                         PIPE_UNLIMITED_INSTANCES, 131072, 131072, 0, nullptr);
        if (handle == INVALID_HANDLE_VALUE) { std::this_thread::sleep_for(std::chrono::seconds(1)); continue; }
        {
            std::lock_guard<std::mutex> guard(pipeHandleMutex);
            activePipe = handle;
        }
        bool connectedPipe = ConnectNamedPipe(handle, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
        std::string line;
        if (connectedPipe) {
            char block[4096];
            DWORD read = 0;
            while (line.size() < 100000 && ReadFile(handle, block, sizeof(block), &read, nullptr) && read) {
                line.append(block, read);
                if (line.find('\n') != std::string::npos) break;
            }
        }
        DisconnectNamedPipe(handle);
        {
            std::lock_guard<std::mutex> guard(pipeHandleMutex);
            if (activePipe == handle) activePipe = INVALID_HANDLE_VALUE;
            CloseHandle(handle);
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
}
void App::onShutdown() {
    std::wstring type = recentShutdownType(startedUtc);
    if (!isPowerOffType(type)) {
        logShutdown(L"Pi stays on; Windows type: " + (type.empty() ? L"unknown" : type));
        return;
    }
    try {
        Settings copy;
        { std::lock_guard<std::mutex> guard(mutex); copy = settings; }
        piRequest(copy, L"POST", L"/v1/shutdown", {}, L"", L"", 3000);
        logShutdown(L"Pi shutdown command sent; Windows type: " + type);
    } catch (const std::exception& error) {
        logShutdown(L"Pi shutdown failed: " + errorText(error));
    }
}

struct ThumbResult { std::wstring key; HBITMAP bitmap; };

struct SettingsWindow {
    struct MediaEntry { std::wstring name, key; uintmax_t size = 0; };

    App* app = nullptr;
    int initialPage = PAGE_CONNECTION;
    HWND hwnd = nullptr, tabs = nullptr, url = nullptr, token = nullptr, hotkey = nullptr;
    HWND retroMode = nullptr, retroPort = nullptr, autostart = nullptr, showToken = nullptr, languageBox = nullptr;
    HWND gestures[GESTURE_COUNT]{}, mediaList = nullptr;
    std::vector<HWND> pages[SETTINGS_PAGES];

    // Media tab state. Thumbnails are rendered on worker threads and cached by
    // name, size and modification time; only the UI thread touches these members.
    std::vector<MediaEntry> entries;
    std::wstring standardName, bootName, shutdownName;
    std::map<std::wstring, HBITMAP> thumbnails;
    std::set<std::wstring> thumbnailFailed, thumbnailPending;
    std::vector<std::thread> workers;
    std::atomic<bool> stopping{false};
    HFONT boldFont = nullptr;

    SettingsWindow(App* owner, int page) : app(owner), initialPage(page) {}
    ~SettingsWindow() {
        stopping = true;
        for (auto& worker : workers) if (worker.joinable()) worker.join();
        for (auto& pair : thumbnails) DeleteObject(pair.second);
        if (boldFont) DeleteObject(boldFont);
    }

    // Adds a control to one tab page; pages other than the visible one are hidden.
    HWND add(int page, const wchar_t* type, const wchar_t* title, DWORD style,
             int x, int y, int width, int height, int id = 0) {
        HWND h = control(hwnd, type, title, style, x, y, width, height, id);
        pages[page].push_back(h);
        return h;
    }
    void selectPage(int page) {
        for (int p = 0; p < SETTINGS_PAGES; ++p)
            for (HWND h : pages[p]) ShowWindow(h, p == page ? SW_SHOW : SW_HIDE);
    }
    void showPage(int page) {
        TabCtrl_SetCurSel(tabs, page);
        selectPage(page);
    }
    void create() {
        Settings current;
        { std::lock_guard<std::mutex> guard(app->mutex); current = app->settings; }
        LOGFONTW font{};
        GetObjectW(GetStockObject(DEFAULT_GUI_FONT), sizeof(font), &font);
        font.lfWeight = FW_BOLD;
        boldFont = CreateFontIndirectW(&font);
        tabs = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP,
                               10, 10, 685, 410, hwnd, (HMENU)(INT_PTR)S_TABS,
                               GetModuleHandleW(nullptr), nullptr);
        SendMessageW(tabs, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
                for (int i = 0; i < SETTINGS_PAGES; ++i) {
            TCITEMW item{};
            item.mask = TCIF_TEXT;
            item.pszText = const_cast<wchar_t*>(trAt(Str::TabConnection, i));
            SendMessageW(tabs, TCM_INSERTITEMW, i, (LPARAM)&item);
        }

        // Connection
        add(PAGE_CONNECTION, L"STATIC", tr(Str::PiAddress), 0, 25, 63, 160, 22);
        url = add(PAGE_CONNECTION, L"EDIT", current.piUrl.c_str(), WS_BORDER | ES_AUTOHSCROLL, 190, 60, 490, 25, S_URL);
        add(PAGE_CONNECTION, L"STATIC", tr(Str::AccessToken), 0, 25, 98, 160, 22);
        token = add(PAGE_CONNECTION, L"EDIT", current.token.c_str(), WS_BORDER | ES_AUTOHSCROLL | ES_PASSWORD,
                    190, 95, 490, 25, S_TOKEN);
        showToken = add(PAGE_CONNECTION, L"BUTTON", tr(Str::ShowToken), BS_AUTOCHECKBOX, 190, 127, 135, 26, S_SHOW_TOKEN);
        add(PAGE_CONNECTION, L"BUTTON", tr(Str::HelpToken), BS_PUSHBUTTON, 340, 127, 180, 27, S_HELP);

        // Gestures
        add(PAGE_GESTURES, L"STATIC", tr(Str::GesturesIntro), 0, 25, 55, 650, 38);
        for (int i = 0; i < GESTURE_COUNT; ++i) {
            int y = 102 + i * 35;
            add(PAGE_GESTURES, L"STATIC", trAt(Str::GestureLongPress, i), 0, 25, y + 3, 160, 22);
            HWND combo = add(PAGE_GESTURES, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
                             190, y, 360, 250, S_GESTURE0 + i);
            gestures[i] = combo;
            for (int a = 0; a < ACTION_COUNT; ++a)
                SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)trAt(Str::ActionNone, a));
            std::string selected = i == 0 ? "touch_menu" : "none";
            auto it = current.gestures.find(GESTURE_IDS[i]);
            if (it != current.gestures.end()) selected = it->second;
            int index = 0;
            for (int j = 0; j < ACTION_COUNT; ++j) if (selected == ACTION_IDS[j]) index = j;
            SendMessageW(combo, CB_SETCURSEL, index, 0);
        }
        add(PAGE_GESTURES, L"STATIC", tr(Str::GesturesWarning), 0, 25, 285, 650, 22);

        // Media
        mediaList = add(PAGE_MEDIA, L"LISTBOX", L"",
                        LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | WS_BORDER | WS_VSCROLL,
                        25, 50, 650, MEDIA_ITEM_HEIGHT * 4 + 2, D_LIST);
        const int by = 50 + MEDIA_ITEM_HEIGHT * 4 + 12;
        add(PAGE_MEDIA, L"BUTTON", tr(Str::MediaAdd), BS_PUSHBUTTON, 25, by, 75, 30, D_ADD);
        add(PAGE_MEDIA, L"BUTTON", tr(Str::MediaPreview), BS_PUSHBUTTON, 108, by, 75, 30, D_PREVIEW);
        add(PAGE_MEDIA, L"BUTTON", tr(Str::MediaRemove), BS_PUSHBUTTON, 191, by, 75, 30, D_REMOVE);
        add(PAGE_MEDIA, L"BUTTON", tr(Str::MediaUseDefault), BS_PUSHBUTTON, 279, by, 105, 30, D_ACTIVATE);
        add(PAGE_MEDIA, L"BUTTON", tr(Str::MediaUseBoot), BS_PUSHBUTTON, 392, by, 125, 30, D_BOOT);
        add(PAGE_MEDIA, L"BUTTON", tr(Str::MediaUseShutdown), BS_PUSHBUTTON, 525, by, 150, 30, D_SHUTDOWN);

        // RetroArch
        add(PAGE_RETROARCH, L"STATIC", tr(Str::Hotkey), 0, 25, 65, 160, 22);
        hotkey = add(PAGE_RETROARCH, L"EDIT", current.hotkey.c_str(), WS_BORDER | ES_AUTOHSCROLL,
                     190, 62, 160, 25, S_HOTKEY);
        add(PAGE_RETROARCH, L"STATIC", tr(Str::HotkeyExamples), 0, 190, 92, 480, 32);
        add(PAGE_RETROARCH, L"STATIC", tr(Str::ControlMethod), 0, 25, 137, 160, 22);
        retroMode = add(PAGE_RETROARCH, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 190, 134, 360, 120, S_RETRO_MODE);
        SendMessageW(retroMode, CB_ADDSTRING, 0, (LPARAM)tr(Str::ModeKeyboard));
        SendMessageW(retroMode, CB_ADDSTRING, 0, (LPARAM)tr(Str::ModeNetwork));
        SendMessageW(retroMode, CB_SETCURSEL, current.retroArchNetworkControl ? 1 : 0, 0);
        add(PAGE_RETROARCH, L"STATIC", tr(Str::NetworkPort), 0, 25, 175, 160, 22);
        retroPort = add(PAGE_RETROARCH, L"EDIT", std::to_wstring(current.retroArchNetworkPort).c_str(),
                        WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER, 190, 172, 100, 25, S_RETRO_PORT);
        add(PAGE_RETROARCH, L"STATIC", tr(Str::NetworkHint), 0, 190, 202, 480, 40);

        // General
        autostart = add(PAGE_GENERAL, L"BUTTON", tr(Str::Autostart), BS_AUTOCHECKBOX, 25, 62, 220, 26, S_STARTUP);
        SendMessageW(autostart, BM_SETCHECK, current.autostart ? BST_CHECKED : BST_UNCHECKED, 0);
        add(PAGE_GENERAL, L"BUTTON", tr(Str::OpenIni), BS_PUSHBUTTON, 25, 105, 210, 28, S_OPEN_INI);
        add(PAGE_GENERAL, L"STATIC", tr(Str::IniHint), 0, 250, 111, 420, 36);
        add(PAGE_GENERAL, L"STATIC", tr(Str::Language), 0, 25, 165, 160, 22);
        languageBox = add(PAGE_GENERAL, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 190, 162, 300, 120, S_LANGUAGE);
        for (Str label : {Str::LanguageAuto, Str::LanguageEnglish, Str::LanguageGerman})
            SendMessageW(languageBox, CB_ADDSTRING, 0, (LPARAM)tr(label));
        SendMessageW(languageBox, CB_SETCURSEL, current.language == "en" ? 1 : current.language == "de" ? 2 : 0, 0);
        add(PAGE_GENERAL, L"STATIC", tr(Str::LanguageHint), 0, 190, 192, 480, 22);

        control(hwnd, L"BUTTON", tr(Str::Save), BS_DEFPUSHBUTTON, 485, 432, 100, 30, S_SAVE);
        control(hwnd, L"BUTTON", tr(Str::Cancel), BS_PUSHBUTTON, 595, 432, 100, 30, S_CANCEL);
        refreshMedia();
        showPage(initialPage);
    }
    void save() {
        Settings next;
        { std::lock_guard<std::mutex> guard(app->mutex); next = app->settings; }
        next.piUrl = readText(url);
        next.token = readText(token);
        next.hotkey = readText(hotkey);
        next.retroArchNetworkControl = SendMessageW(retroMode, CB_GETCURSEL, 0, 0) == 1;
        try { next.retroArchNetworkPort = std::stoi(readText(retroPort)); }
        catch (...) { showPage(PAGE_RETROARCH); alert(hwnd, tr(Str::InvalidPort)); return; }
        if (next.retroArchNetworkPort < 1 || next.retroArchNetworkPort > 65535) {
            showPage(PAGE_RETROARCH);
            alert(hwnd, tr(Str::PortRange)); return;
        }
        next.autostart = SendMessageW(autostart, BM_GETCHECK, 0, 0) == BST_CHECKED;
        {
            const int choice = int(SendMessageW(languageBox, CB_GETCURSEL, 0, 0));
            next.language = choice == 1 ? "en" : choice == 2 ? "de" : "auto";
        }
        bool menuReachable = false;
        for (int i = 0; i < GESTURE_COUNT; ++i) {
            int selected = int(SendMessageW(gestures[i], CB_GETCURSEL, 0, 0));
            next.gestures[GESTURE_IDS[i]] = ACTION_IDS[selected >= 0 && selected < ACTION_COUNT ? selected : 0];
            menuReachable = menuReachable || next.gestures[GESTURE_IDS[i]] == "touch_menu";
        }
        if (!next.configured()) {
            showPage(PAGE_CONNECTION);
            alert(hwnd, tr(Str::NotConfigured)); return;
        }
        if (!validHotkey(next.hotkey)) {
            showPage(PAGE_RETROARCH); alert(hwnd, tr(Str::InvalidHotkey)); return;
        }
        if (!menuReachable) {
            showPage(PAGE_GESTURES);
            if (MessageBoxW(hwnd,
                            tr(Str::TouchMenuUnassigned),
                            L"Marquee-Pi", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;
        }
        try {
            setAutostart(next.autostart);
            saveSettings(next);
            setUiLanguage(resolveLanguage(next.language));
            {
                std::lock_guard<std::mutex> guard(app->mutex);
                app->settings = next; app->gestureDirty = true;
                app->needsSync = app->game.has_value(); ++app->configVersion;
            }
            DestroyWindow(hwnd);
        } catch (const std::exception& error) { alert(hwnd, errorText(error), tr(Str::TitleSettings)); }
    }

    // ---- Medien -----------------------------------------------------------------
    std::wstring activeName(const wchar_t* marker) {
        try {
            std::wstring text = fromUtf8(readFile(dataDirectory() + L"\\" + marker));
            while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n')) text.pop_back();
            return text;
        } catch (...) { return L""; }
    }
    std::wstring selectedName() {
        int index = int(SendMessageW(mediaList, LB_GETCURSEL, 0, 0));
        if (index == LB_ERR || index < 0 || size_t(index) >= entries.size()) return L"";
        return entries[size_t(index)].name;
    }
    void refreshMedia(const std::wstring& select = L"") {
        std::wstring keep = select.empty() ? selectedName() : select;
        entries.clear();
        SendMessageW(mediaList, LB_RESETCONTENT, 0, 0);
        std::error_code ec;
        fs::create_directories(fs::path(mediaDirectory()), ec);
        for (fs::directory_iterator it(fs::path(mediaDirectory()), ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code fileError;
            if (!it->is_regular_file(fileError) || fileError) continue;
            MediaEntry entry;
            entry.name = it->path().filename().wstring();
            entry.size = it->file_size(fileError);
            auto stamp = it->last_write_time(fileError).time_since_epoch().count();
            entry.key = entry.name + L"|" + std::to_wstring(entry.size) + L"|" + std::to_wstring(stamp);
            entries.push_back(std::move(entry));
        }
        std::sort(entries.begin(), entries.end(),
                  [](const MediaEntry& a, const MediaEntry& b) { return a.name < b.name; });
        std::vector<std::pair<std::wstring, std::wstring>> jobs;
        for (const auto& entry : entries) {
            SendMessageW(mediaList, LB_ADDSTRING, 0, (LPARAM)entry.name.c_str());
            if (thumbnails.count(entry.key) || thumbnailFailed.count(entry.key) ||
                thumbnailPending.count(entry.key)) continue;
            thumbnailPending.insert(entry.key);
            jobs.emplace_back(entry.key, (fs::path(mediaDirectory()) / entry.name).wstring());
        }
        standardName = activeName(L"active-media.txt");
        bootName = activeName(L"boot-media.txt");
        shutdownName = activeName(L"shutdown-media.txt");
        for (size_t i = 0; i < entries.size(); ++i)
            if (entries[i].name == keep) SendMessageW(mediaList, LB_SETCURSEL, i, 0);
        startThumbnails(std::move(jobs));
    }
    void startThumbnails(std::vector<std::pair<std::wstring, std::wstring>> jobs) {
        if (jobs.empty()) return;
        HWND target = hwnd;
        workers.emplace_back([this, target, jobs = std::move(jobs)] {
            const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
            for (const auto& job : jobs) {
                if (stopping) break;
                HBITMAP bitmap = com ? createThumbnail(job.second, THUMB_W, THUMB_H) : nullptr;
                auto* result = new ThumbResult{job.first, bitmap};
                if (!PostMessageW(target, WM_THUMB, 0, (LPARAM)result)) {
                    if (bitmap) DeleteObject(bitmap);
                    delete result;
                }
            }
            if (com) CoUninitialize();
        });
    }
    void thumbnailReady(ThumbResult* result) {
        thumbnailPending.erase(result->key);
        if (result->bitmap) {
            auto old = thumbnails.find(result->key);
            if (old != thumbnails.end()) DeleteObject(old->second);
            thumbnails[result->key] = result->bitmap;
        } else {
            thumbnailFailed.insert(result->key);
        }
        delete result;
        InvalidateRect(mediaList, nullptr, FALSE);
    }
    static std::wstring sizeText(uintmax_t bytes) {
        wchar_t text[48];
        if (bytes >= 1024 * 1024) swprintf(text, 48, L"%.1f MB", double(bytes) / (1024.0 * 1024.0));
        else swprintf(text, 48, L"%.0f KB", std::max(1.0, double(bytes) / 1024.0));
        for (wchar_t* c = text; *c; ++c) if (*c == L'.') *c = L',';
        return text;
    }
    void drawItem(const DRAWITEMSTRUCT& d) {
        if (d.itemID == UINT(-1) || d.itemID >= entries.size()) return;
        const MediaEntry& entry = entries[d.itemID];
        const bool selected = (d.itemState & ODS_SELECTED) != 0;
        HDC dc = d.hDC;
        RECT row = d.rcItem;
        FillRect(dc, &row, GetSysColorBrush(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW));
        SetBkMode(dc, TRANSPARENT);

        const int thumbX = row.left + 6, thumbY = row.top + (row.bottom - row.top - THUMB_H) / 2;
        auto bitmap = thumbnails.find(entry.key);
        if (bitmap != thumbnails.end()) {
            HDC memory = CreateCompatibleDC(dc);
            HGDIOBJ previous = SelectObject(memory, bitmap->second);
            BitBlt(dc, thumbX, thumbY, THUMB_W, THUMB_H, memory, 0, 0, SRCCOPY);
            SelectObject(memory, previous);
            DeleteDC(memory);
        } else {
            RECT box{thumbX, thumbY, thumbX + THUMB_W, thumbY + THUMB_H};
            HBRUSH gray = CreateSolidBrush(RGB(0x30, 0x30, 0x30));
            FillRect(dc, &box, gray);
            DeleteObject(gray);
            SetTextColor(dc, RGB(0xb0, 0xb0, 0xb0));
            DrawTextW(dc, thumbnailFailed.count(entry.key) ? tr(Str::MediaNoPreview) : L"…", -1, &box,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        const int textX = thumbX + THUMB_W + 14;
        std::wstring ext = fs::path(entry.name).extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        std::wstring detail = std::wstring(ext == L".mp4" ? tr(Str::MediaVideo) : tr(Str::MediaImage)) +
                              L"  ·  " + sizeText(entry.size);
        std::wstring roles;
        auto addRole = [&](const std::wstring& active, const wchar_t* label) {
            if (active == entry.name) roles += (roles.empty() ? L"" : L"  ·  ") + std::wstring(label);
        };
        addRole(standardName, tr(Str::RoleDefault));
        addRole(bootName, tr(Str::RoleBoot));
        addRole(shutdownName, tr(Str::RoleShutdown));

        HGDIOBJ previousFont = SelectObject(dc, boldFont);
        SetTextColor(dc, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
        RECT nameRect{textX, row.top + 8, row.right - 8, row.top + 28};
        DrawTextW(dc, entry.name.c_str(), -1, &nameRect, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(dc, previousFont);
        SetTextColor(dc, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_GRAYTEXT));
        RECT detailRect{textX, row.top + 30, row.right - 8, row.top + 48};
        DrawTextW(dc, detail.c_str(), -1, &detailRect, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        if (!roles.empty()) {
            SetTextColor(dc, selected ? GetSysColor(COLOR_HIGHLIGHTTEXT) : RGB(0x00, 0x5a, 0xb4));
            RECT roleRect{textX, row.top + 50, row.right - 8, row.top + 68};
            DrawTextW(dc, roles.c_str(), -1, &roleRect, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
        HPEN line = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNFACE));
        HGDIOBJ previousPen = SelectObject(dc, line);
        MoveToEx(dc, row.left, row.bottom - 1, nullptr);
        LineTo(dc, row.right, row.bottom - 1);
        SelectObject(dc, previousPen);
        DeleteObject(line);
    }
    void addMedia() {
        wchar_t chosen[32768]{};
        std::wstring filterText = tr(Str::FilterSupported);
        filterText.push_back(L'\0');
        filterText += L"*.jpg;*.jpeg;*.png;*.gif;*.webp;*.mp4";
        filterText.push_back(L'\0');
        filterText += tr(Str::FilterAll);
        filterText.push_back(L'\0');
        filterText += L"*.*";
        filterText.push_back(L'\0');
        filterText.push_back(L'\0');
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = hwnd; dialog.lpstrFilter = filterText.c_str();
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
            refreshMedia(target.filename().wstring());
        } catch (const std::exception& error) { alert(hwnd, errorText(error)); }
    }
    void previewMedia() {
        std::wstring name = selectedName();
        if (name.empty()) return;
        std::wstring path = (fs::path(mediaDirectory()) / name).wstring();
        ShellExecuteW(hwnd, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    void activateMedia(const wchar_t* endpoint = L"/v1/default-media",
                       const wchar_t* marker = L"active-media.txt",
                       const wchar_t* success = nullptr,
                       bool imageOnly = false) {
        std::wstring name = selectedName();
        if (name.empty()) return;
        try {
            Settings settings;
            { std::lock_guard<std::mutex> guard(app->mutex); settings = app->settings; }
            std::wstring path = (fs::path(mediaDirectory()) / name).wstring();
            std::string body = readFile(path);
            if (body.size() > 20 * 1024 * 1024) throw std::runtime_error("File exceeds 20 MB upload limit");
            std::wstring ext = fs::path(name).extension().wstring();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
            if (imageOnly && ext != L".png" && ext != L".jpg" && ext != L".jpeg")
                throw std::runtime_error("Boot splash must be a PNG or JPEG image");
            piRequest(settings, L"POST", endpoint, body,
                      L"application/octet-stream", L"X-File-Name: upload" + ext + L"\r\n");
            writeFile(dataDirectory() + L"\\" + marker, toUtf8(name));
            refreshMedia(name);
            alert(hwnd, success ? success : tr(Str::DefaultActive));
        } catch (const std::exception& error) { alert(hwnd, errorText(error), tr(Str::UploadFailed)); }
    }
    void removeMedia() {
        std::wstring name = selectedName();
        if (name.empty()) return;
        if (name == activeName(L"active-media.txt") || name == activeName(L"boot-media.txt") ||
            name == activeName(L"shutdown-media.txt")) {
            alert(hwnd, tr(Str::ReplaceRolesFirst));
            return;
        }
        std::wstring path = (fs::path(mediaDirectory()) / name).wstring();
        if (!DeleteFileW(path.c_str())) { alert(hwnd, tr(Str::RemoveFailed)); return; }
        refreshMedia(L"\x01");  // nothing selected afterwards
    }
};
void App::showSettings(int page) {
    if (settingsWindow) {
        SendMessageW(settingsWindow, WM_SELECT_PAGE, WPARAM(page), 0);
        SetForegroundWindow(settingsWindow);
        return;
    }
    static bool registered = false;
    if (!registered) {
        WNDCLASSW klass{}; klass.lpfnWndProc = settingsProc;
        klass.hInstance = GetModuleHandleW(nullptr); klass.lpszClassName = L"MarqueePiSettings";
        klass.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        klass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        registered = RegisterClassW(&klass) != 0;
    }
    auto* state = new SettingsWindow(this, page);
    constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    RECT frame{0, 0, 705, 475};
    AdjustWindowRectEx(&frame, style, FALSE, WS_EX_DLGMODALFRAME);
    HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME, L"MarqueePiSettings",
                                 tr(Str::WindowTitle), style,
                                 CW_USEDEFAULT, CW_USEDEFAULT, frame.right - frame.left,
                                 frame.bottom - frame.top, hwnd, nullptr,
                                 GetModuleHandleW(nullptr), state);
    if (!window) { delete state; alert(hwnd, tr(Str::SettingsOpenFailed)); return; }
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
    case WM_SELECT_PAGE:
        if (int(wp) >= 0 && int(wp) < SETTINGS_PAGES) state->showPage(int(wp));
        return 0;
    case WM_THUMB: state->thumbnailReady((ThumbResult*)lp); return 0;
    case WM_MEASUREITEM: {
        auto* item = (MEASUREITEMSTRUCT*)lp;
        if (item->CtlID != D_LIST) break;
        item->itemHeight = MEDIA_ITEM_HEIGHT;
        return TRUE;
    }
    case WM_DRAWITEM: {
        auto* item = (DRAWITEMSTRUCT*)lp;
        if (item->CtlID != D_LIST) break;
        state->drawItem(*item);
        return TRUE;
    }
    case WM_NOTIFY: {
        const NMHDR* header = (const NMHDR*)lp;
        if (header->idFrom == S_TABS && header->code == TCN_SELCHANGE)
            state->selectPage(TabCtrl_GetCurSel(state->tabs));
        return 0;
    }
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
            alert(hwnd, tr(Str::TokenHelp), tr(Str::TokenHelpTitle));
            return 0;
        case D_LIST:
            if (HIWORD(wp) == LBN_DBLCLK) state->previewMedia();
            return 0;
        case D_ADD: state->addMedia(); return 0;
        case D_PREVIEW: state->previewMedia(); return 0;
        case D_ACTIVATE: state->activateMedia(); return 0;
        case D_BOOT:
            state->activateMedia(L"/v1/boot-splash", L"boot-media.txt", tr(Str::BootSaved), true);
            return 0;
        case D_SHUTDOWN:
            state->activateMedia(L"/v1/shutdown-media", L"shutdown-media.txt", tr(Str::ShutdownSaved));
            return 0;
        case D_REMOVE: state->removeMedia(); return 0;
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
        if (inGame) {
            if (settings.retroArchNetworkControl) sendRetroArchNetworkCommand(settings.retroArchNetworkPort);
            else sendRetroArchHotkey(settings.hotkey);
        }
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
        setUiLanguage(resolveLanguage(settings.language));
        std::wstring command = commandLine ? commandLine : L"";
        if (command.find(L"--pi-shutdown") != std::wstring::npos) {
            try { piRequest(settings, L"POST", L"/v1/shutdown", {}, L"", L"", 3000); return 0; }
            catch (...) { return 1; }
        }
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_TAB_CLASSES};
        InitCommonControlsEx(&controls);
        App app(std::move(settings));
        currentApp = &app;
        if (!app.start(instance)) {
            alert(nullptr, tr(Str::StartFailed));
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
        alert(nullptr, errorText(error), tr(Str::TitleStartupError));
        return 1;
    }
}
