#include "core.hpp"
#include "event_log.hpp"
#include "pi_sync.hpp"
#include "popups.hpp"
#include "settings_ui.hpp"
#include "strings.hpp"
#include "thumb_cache.hpp"
#include "ui_host.hpp"
#include "ui_kit.hpp"
#include <windows.h>
#include <shellapi.h>
#include <winevt.h>
#include <atomic>
#include <chrono>
#include <ctime>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT WM_STATUS = WM_APP + 2;
constexpr UINT WM_GAME = WM_APP + 3;
constexpr UINT WM_APP_HOTKEY = WM_APP + 4;
constexpr UINT WM_ERROR = WM_APP + 5;
constexpr wchar_t PIPE_NAME[] = L"\\\\.\\pipe\\MarqueePiGameEvents";
constexpr int ICON_CONNECTED = 101, ICON_DISCONNECTED = 102;
struct StatusUpdate { bool connected; std::wstring title; int latencyMs; };
class App;
static App* currentApp = nullptr;

static void alert(HWND owner, const std::wstring& message, const wchar_t* title = L"Marquee-Pi") {
    MessageBoxW(owner, message.c_str(), title, MB_OK | MB_ICONINFORMATION);
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
static void logShutdown(const std::wstring& line) {
    logLine(L"shutdown.log", line);
    logEvent(LogLevel::Info, "shutdown", line);
}
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
class App : public UiHost {
public:
    HWND hwnd = nullptr;
    HICON onlineIcon = nullptr, offlineIcon = nullptr;
    WinHttpTransport transport;
    PiSync syncer;
    std::atomic<bool> stopping{false};
    std::mutex pipeHandleMutex;
    HANDLE activePipe = INVALID_HANDLE_VALUE;
    std::atomic<bool> pipeUp{false};
    std::thread polling, pipe;
    UINT taskbarCreated = 0;
    FILETIME startedUtc{};
    mutable PiStatus st;

    explicit App(Settings initial) : syncer(std::move(initial), transport, syncEvents()) {}
    PiSyncEvents syncEvents();

    // UiHost
    HWND window() override { return hwnd; }
    PiSync& sync() override { return syncer; }
    const PiStatus& status() const override { st.pipeListening = pipeUp; return st; }
    void command(int id) override;
    void languageChanged() override { addIcon(false); }

    bool start(HINSTANCE instance);
    void close();
    void addIcon(bool add);
    void setStatus(const StatusUpdate& update);
    void reloadSettings();
    void onGame(GameMessage message);
    void pollLoop();
    void pipeLoop();
    void onShutdown();
    void report(const std::wstring& text, ui::ToastKind kind) {
        if (ui::settingsWindow()) ui::settingsToast(text, kind);
    }
    void reportError(const std::wstring& text) {
        if (ui::settingsWindow()) ui::settingsToast(text, ui::ToastKind::Error);
        else alert(hwnd, text);
    }
    void postError(const std::wstring& message) {
        auto* text = new std::wstring(message);
        if (!PostMessageW(hwnd, WM_ERROR, 0, (LPARAM)text)) delete text;
    }
};

static LRESULT CALLBACK mainProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp);

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
        const bool autostart = syncer.settings().autostart;
        if (autostart || autostartEnabled() != autostart) setAutostart(autostart);
    } catch (const std::exception& e) { alert(hwnd, errorText(e), L"Autostart"); }
    if (!syncer.settings().configured()) logEvent(LogLevel::Warn, "conn", tr(Str::LogNotConfigured));
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
    data.hIcon = st.connected ? onlineIcon : offlineIcon;
    wcscpy_s(data.szTip, st.connected ? tr(Str::TipConnected) : tr(Str::TipDisconnected));
    Shell_NotifyIconW(add ? NIM_ADD : NIM_MODIFY, &data);
}
void App::setStatus(const StatusUpdate& update) {
    const bool wasKnown = st.known, was = st.connected;
    st.known = true;
    st.connected = update.connected;
    st.title = update.connected ? update.title : L"";
    st.latencyMs = update.latencyMs;
    if (update.connected) st.lastContact = std::time(nullptr);
    if (!wasKnown || was != update.connected) {
        const Settings settings = syncer.settings();
        if (settings.configured()) {
            if (update.connected) logEvent(LogLevel::Info, "conn", fmt(Str::LogConnected, settings.piUrl.c_str()));
            else logEvent(LogLevel::Error, "conn", fmt(Str::LogUnreachable, settings.piUrl.c_str()));
        }
    }
    addIcon(false);
    ui::settingsRefresh();
}
void App::command(int id) {
    switch (id) {
    case M_SETTINGS: ui::openSettings(*this, PAGE_DASHBOARD); return;
    case M_MEDIA: ui::openSettings(*this, PAGE_MEDIA); return;
    case M_OPEN_INI:
        ShellExecuteW(hwnd, L"open", settingsPath().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    case M_LOAD_INI: reloadSettings(); return;
    case M_EXIT: DestroyWindow(hwnd); return;
    }
    const bool reboot = id == M_REBOOT;
    const Str label = id == M_DEFAULT ? Str::MenuDefault : id == M_RELOAD ? Str::MenuReload
                      : reboot ? Str::MenuReboot : Str::MenuShutdown;
    if (reboot || id == M_SHUTDOWN) {
        if (!ui::confirmDialog(ui::settingsWindow(), tr(label), tr(reboot ? Str::ConfirmReboot : Str::ConfirmShutdown),
                               tr(reboot ? Str::ConfirmRestartButton : Str::ConfirmShutdownButton), true))
            return;
    }
    try {
        if (id == M_DEFAULT) { syncer.showDefault(); st.gameActive = false; }
        else if (id == M_RELOAD) syncer.reloadDisplay();
        else if (reboot) syncer.rebootPi();
        else syncer.shutdownPi();
        logEvent(LogLevel::Info, "api", fmt(Str::LogCommandSent, tr(label)));
        report(tr(id == M_DEFAULT ? Str::ToastDefaultShown : id == M_RELOAD ? Str::ToastReloaded
                  : reboot ? Str::ToastRestartSent : Str::ToastShutdownSent),
               reboot ? ui::ToastKind::Info : id == M_SHUTDOWN ? ui::ToastKind::Warn : ui::ToastKind::Ok);
    } catch (const std::exception& error) {
        logEvent(LogLevel::Error, "api", fmt(Str::LogCommandFailed, tr(label), errorText(error).c_str()));
        reportError(errorText(error));
    }
    ui::settingsRefresh();
}
void App::reloadSettings() {
    try {
        Settings loaded = loadSettings();
        if (!loaded.configured() || !validHotkey(loaded.hotkey))
            throw std::runtime_error("INI has invalid Pi URL, token or hotkey");
        setAutostart(loaded.autostart);
        setUiLanguage(resolveLanguage(loaded.language));
        syncer.settingsChanged(std::move(loaded));
        addIcon(false);
        logEvent(LogLevel::Info, "conn", tr(Str::LogSettingsReloaded));
        report(tr(Str::ToastIniReloaded), ui::ToastKind::Ok);
    } catch (const std::exception& error) {
        if (ui::settingsWindow()) ui::settingsToast(errorText(error), ui::ToastKind::Error);
        else alert(hwnd, errorText(error), tr(Str::TitleReloadIni));
    }
    ui::settingsRefresh();
}
void App::onGame(GameMessage message) {
    if (message.action == "game") {
        st.gameActive = true;
        st.gameTitle = message.title;
        st.gameMarquee = message.marquee;
        st.gameStarted = std::time(nullptr);
        logEvent(LogLevel::Info, "pipe", fmt(Str::LogGameStarted, message.title.c_str()));
        syncer.gameStarted(std::move(message));
    } else if (message.action == "exit") {
        st.gameActive = false;
        logEvent(LogLevel::Info, "pipe", tr(Str::LogGameEnded));
        syncer.gameExited();
    }
    ui::settingsRefresh();
}
PiSyncEvents App::syncEvents() {
    PiSyncEvents events;
    events.status = [this](bool isConnected, const std::string& title, int latencyMs) {
        std::wstring shown = fromUtf8(title);
        if (isConnected && shown.empty()) shown = tr(Str::DefaultMediaTitle);
        auto* update = new StatusUpdate{isConnected, isConnected ? shown : std::wstring(), latencyMs};
        if (!PostMessageW(hwnd, WM_STATUS, 0, (LPARAM)update)) delete update;
    };
    events.retroArchMenu = [this] { PostMessageW(hwnd, WM_APP_HOTKEY, 0, 0); };
    events.gameSent = [](size_t bytes, int ms) {
        logEvent(LogLevel::Info, "api", fmt(Str::LogGameSent, int((bytes + 1023) / 1024), ms));
    };
    events.artworkSkipped = [](const std::string& message) {
        logArtworkWarning("not sent: " + message);
        logEvent(LogLevel::Warn, "api", fmt(Str::LogArtworkSkipped, fromUtf8(message).c_str()));
    };
    events.piWarning = [](const std::string& message) {
        logArtworkWarning(message);
        logEvent(LogLevel::Warn, "api", fmt(Str::LogPiWarning, fromUtf8(message).c_str()));
    };
    return events;
}
void App::pollLoop() {
    while (!stopping) {
        syncer.tick();
        for (int i = 0; i < 15 && !stopping; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}
void App::pipeLoop() {
    while (!stopping) {
        HANDLE handle = CreateNamedPipeW(PIPE_NAME, PIPE_ACCESS_INBOUND,
                                         PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
                                             PIPE_REJECT_REMOTE_CLIENTS,
                                         PIPE_UNLIMITED_INSTANCES, 131072, 131072, 0, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            pipeUp = false;
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        pipeUp = true;
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
    pipeUp = false;
}
void App::onShutdown() {
    std::wstring type = recentShutdownType(startedUtc);
    if (!isPowerOffType(type)) {
        logShutdown(L"Pi stays on; Windows type: " + (type.empty() ? L"unknown" : type));
        return;
    }
    try {
        syncer.shutdownPi(3000);
        logShutdown(L"Pi shutdown command sent; Windows type: " + type);
    } catch (const std::exception& error) {
        logShutdown(L"Pi shutdown failed: " + errorText(error));
    }
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
    case WM_TRAY:
        if (LOWORD(lp) == WM_LBUTTONUP || LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU) {
            POINT cursor{};
            GetCursorPos(&cursor);
            ui::showFlyout(*app, cursor);
        } else if (LOWORD(lp) == WM_LBUTTONDBLCLK) app->command(M_MEDIA);
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
        Settings settings = app->syncer.settings();
        if (app->syncer.inGame()) {
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

static void enableDpiAwareness() {
    typedef BOOL(WINAPI * SetContextFn)(HANDLE);
    const auto setContext = reinterpret_cast<SetContextFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext")));
    if (setContext) setContext(reinterpret_cast<HANDLE>(-4));  // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int) {
    try {
        Settings settings = loadSettings();
        setUiLanguage(resolveLanguage(settings.language));
        std::wstring command = commandLine ? commandLine : L"";
        if (command.find(L"--pi-shutdown") != std::wstring::npos) {
            try {
                WinHttpTransport transport;
                PiSync(settings, transport).shutdownPi(3000);
                return 0;
            }
            catch (...) { return 1; }
        }
        enableDpiAwareness();
        ui::startup(instance);
        ui::setTheme(settings.theme);
        int exitCode = 0;
        {
            App app(std::move(settings));
            currentApp = &app;
            if (!app.start(instance)) {
                alert(nullptr, tr(Str::StartFailed));
                exitCode = 1;
            } else {
                MSG message{};
                while (GetMessageW(&message, nullptr, 0, 0) > 0) {
                    HWND dialog = ui::settingsWindow();
                    if (dialog && IsDialogMessageW(dialog, &message)) continue;
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
                exitCode = int(message.wParam);
            }
            app.close();
            currentApp = nullptr;
        }
        ThumbCache::instance().shutdown();
        ui::shutdown();
        return exitCode;
    } catch (const std::exception& error) {
        alert(nullptr, errorText(error), tr(Str::TitleStartupError));
        return 1;
    }
}
