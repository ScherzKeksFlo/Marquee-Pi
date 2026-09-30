#include "settings_ui.hpp"
#include "event_log.hpp"
#include "popups.hpp"
#include "strings.hpp"
#include "thumb_cache.hpp"
#include "ui_kit.hpp"
#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <map>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace ui {
namespace {

constexpr wchar_t SETTINGS_CLASS[] = L"MarqueePiSettings";
constexpr UINT WM_TEST_DONE = WM_APP + 50, WM_UPLOAD_PROGRESS = WM_APP + 51, WM_UPLOAD_DONE = WM_APP + 52,
               WM_GOTO_PAGE = WM_APP + 53, WM_REFRESH = WM_APP + 54, WM_TOAST = WM_APP + 55;
constexpr UINT_PTR TIMER_TICK = 1, TIMER_TOAST = 2;
constexpr float NAV_W = 204.f, FOOT_H = 57.f, TABS_H = 44.f;

enum Id {
    ID_NAV = 100,  // +page
    ID_SAVE = 110, ID_CANCEL, ID_TEST = 120, ID_TOKEN_TOGGLE, ID_HELP, ID_SETUP, ID_GOMEDIA, ID_GOLOGS,
    ID_ACT = 130,      // +0..3 default, reload, reboot, shutdown
    ID_GESTURE = 140,  // +0..4
    ID_RETRO = 150,    // +0 keyboard, +1 network
    ID_LANG = 160,     // +0 auto, en, de
    ID_AUTOSTART = 165, ID_OPEN_INI, ID_RELOAD_INI,
    ID_VIEW_GRID = 170, ID_VIEW_LIST, ID_ADD,
    ID_ROLE = 175,     // +0..2
    ID_PREVIEW = 180, ID_REMOVE, ID_RETRY, ID_DISMISS,
    ID_LOGF = 190,     // +0..3
    ID_COPY = 195, ID_OPEN_LOG,
    ID_MEDIA = 1000,   // +entry index
    ID_EDIT_URL = 3001, ID_EDIT_TOKEN, ID_EDIT_HOTKEY, ID_EDIT_PORT
};

const char* ACTION_IDS[] = {"none", "marquee", "box_art", "logo", "controls", "default", "retroarch_menu", "touch_menu"};
constexpr int ACTION_COUNT = int(sizeof(ACTION_IDS) / sizeof(ACTION_IDS[0]));
static_assert(int(Str::ActionTouchMenu) - int(Str::ActionNone) + 1 == ACTION_COUNT, "action labels out of sync");
const char* GESTURE_IDS[] = {"long-press", "swipe-down", "swipe-up", "swipe-right", "swipe-left"};
constexpr int GESTURE_COUNT = int(sizeof(GESTURE_IDS) / sizeof(GESTURE_IDS[0]));
static_assert(int(Str::GestureLeft) - int(Str::GestureLongPress) + 1 == GESTURE_COUNT, "gesture labels out of sync");

struct RoleInfo {
    const wchar_t* marker;
    const wchar_t* endpoint;
    Str chip, button, name, saved;
};
const RoleInfo ROLES[3] = {
    {L"active-media.txt", L"/v1/default-media", Str::RoleChipDefault, Str::MediaUseDefault, Str::RoleDefault, Str::DefaultActive},
    {L"boot-media.txt", L"/v1/boot-splash", Str::RoleBoot, Str::MediaUseBoot, Str::RoleBoot, Str::BootSaved},
    {L"shutdown-media.txt", L"/v1/shutdown-media", Str::RoleChipShutdown, Str::MediaUseShutdown, Str::RoleShutdown,
     Str::ShutdownSaved}};
Color roleColor(int role) { return role == 0 ? pal().pink : role == 1 ? pal().cyan : pal().yel; }

std::wstring lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), ::towlower);
    return s;
}
std::wstring upper(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), ::towupper);
    return s;
}
std::wstring clockText(std::time_t t, bool seconds) {
    std::tm local{};
    localtime_s(&local, &t);
    wchar_t text[16];
    if (seconds) swprintf(text, 16, L"%02d:%02d:%02d", local.tm_hour, local.tm_min, local.tm_sec);
    else swprintf(text, 16, L"%02d:%02d", local.tm_hour, local.tm_min);
    return text;
}
std::wstring sizeText(uintmax_t bytes) {
    wchar_t text[48];
    if (bytes >= 1024 * 1024) swprintf(text, 48, L"%.1f MB", double(bytes) / (1024.0 * 1024.0));
    else swprintf(text, 48, L"%.0f KB", std::max(1.0, double(bytes) / 1024.0));
    if (PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_GERMAN)
        for (wchar_t* ch = text; *ch; ++ch) if (*ch == L'.') *ch = L',';
    return text;
}
std::wstring readText(HWND control) {
    const int n = GetWindowTextLengthW(control);
    std::wstring text(size_t(n) + 1, L'\0');
    GetWindowTextW(control, text.data(), n + 1);
    text.resize(size_t(n));
    const size_t a = text.find_first_not_of(L" \t\r\n"), b = text.find_last_not_of(L" \t\r\n");
    return a == std::wstring::npos ? L"" : text.substr(a, b - a + 1);
}
// "error" from a Pi JSON error body, else the exception text.
std::wstring piErrorText(const std::exception& error) {
    const std::string what = error.what();
    const size_t brace = what.find('{');
    if (brace != std::string::npos) {
        try {
            const std::string message = mini::parse(what.substr(brace)).get("error").value();
            if (!message.empty()) return fromUtf8(message);
        } catch (...) {}
    }
    return errorText(error);
}

struct TestResult { bool ok; bool rejected; int ms; std::wstring message; };
struct UploadResult { bool ok; std::wstring name, message; int role; };

class SettingsUi : public Surface {
public:
    static SettingsUi* instance;

    UiHost& host;
    int page;
    bool sidebar = true;

    HWND urlEdit = nullptr, tokenEdit = nullptr, hotkeyEdit = nullptr, portEdit = nullptr;
    HBRUSH editBrush = nullptr;
    std::map<HWND, RectF> placed;      // desired edit rectangles of this paint pass (DIP)
    std::map<HWND, RECT> applied;      // pixel rectangles last sent to the edits

    // staged values that are applied by Save
    Settings working;
    bool showToken = false;
    RectF gestureBox[GESTURE_COUNT];

    RectF viewport;
    float scrollY = 0, contentHeight = 0;
    bool blink = true;
    int noisePhase = 0;
    unsigned logRevision = 0;

    // connection test
    bool testing = false;
    bool messageShown = false;
    std::wstring messageText;
    Color messageColor;
    std::thread tester;

    // media
    struct Entry { std::wstring name, path; uintmax_t size = 0; std::wstring ext; };
    std::vector<Entry> entries;
    int selected = -1;
    bool grid = true, blocked = false;
    struct { bool active = false; std::wstring name; int role = 0, percent = 0; } upload;
    struct { bool shown = false; std::wstring name; int role = 0; std::wstring message; } uploadError;
    std::thread uploader;

    int logFilter = 0;  // 0 all, 1 info, 2 warn, 3 error

    std::wstring toastText;
    Color toastColor;
    bool toastShown = false;

    explicit SettingsUi(UiHost& h, int initialPage) : host(h), page(initialPage) {}
    ~SettingsUi() override {
        if (tester.joinable()) tester.join();
        if (uploader.joinable()) uploader.join();
        if (editBrush) DeleteObject(editBrush);
    }

    // ---- window plumbing --------------------------------------------------------------------

    static LRESULT CALLBACK proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<SettingsUi*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<SettingsUi*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            self->hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(hwnd, message, wp, lp);
        LRESULT result = 0;
        if (self->handle(message, wp, lp, result)) return result;
        return self->message(message, wp, lp);
    }

    LRESULT message(UINT m, WPARAM wp, LPARAM lp) {
        switch (m) {
        case WM_CREATE: onCreate(); return 0;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lp);
            RECT frame{0, 0, LONG(760 * scale), LONG(520 * scale)};
            AdjustWindowRectEx(&frame, GetWindowLongW(hwnd, GWL_STYLE), FALSE, GetWindowLongW(hwnd, GWL_EXSTYLE));
            info->ptMinTrackSize.x = frame.right - frame.left;
            info->ptMinTrackSize.y = frame.bottom - frame.top;
            return 0;
        }
        case WM_SIZE: InvalidateRect(hwnd, nullptr, FALSE); return 0;
        case WM_DPICHANGED: {
            scale = float(LOWORD(wp)) / 96.f;
            const RECT* suggested = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                         suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            applyEditFonts();
            applied.clear();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_MOUSEWHEEL:
            scrollY -= float(GET_WHEEL_DELTA_WPARAM(wp)) / 120.f * 64.f;
            clampScroll();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetTextColor(dc, rgb(pal().ink));
            SetBkColor(dc, rgb(pal().ibg));
            return reinterpret_cast<LRESULT>(editBrush);
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) { save(); return 0; }
            if (LOWORD(wp) == IDCANCEL) { DestroyWindow(hwnd); return 0; }
            if (HIWORD(wp) == EN_CHANGE) InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_TIMER:
            if (wp == TIMER_TICK) {
                blink = !blink;
                noisePhase = (noisePhase + 10) % 40;
                logRevision = eventLog().revision();
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (wp == TIMER_TOAST) {
                KillTimer(hwnd, TIMER_TOAST);
                toastShown = false;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_THUMB_READY:
            ThumbCache::instance().drain();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_UI_DROPDOWN: {
            const int index = int(wp) - ID_GESTURE;
            if (index >= 0 && index < GESTURE_COUNT && int(lp) >= 0 && int(lp) < ACTION_COUNT) {
                working.gestures[GESTURE_IDS[index]] = ACTION_IDS[int(lp)];
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_TEST_DONE: {
            std::unique_ptr<TestResult> result(reinterpret_cast<TestResult*>(lp));
            testing = false;
            messageShown = true;
            if (result->ok) { messageText = fmt(Str::ConnOk, result->ms); messageColor = pal().grn; }
            else if (result->rejected) { messageText = tr(Str::ConnTokenRejected); messageColor = pal().red; }
            else { messageText = fmt(Str::ConnUnreachable, result->message.c_str()); messageColor = pal().red; }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_UPLOAD_PROGRESS:
            upload.percent = int(wp);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_UPLOAD_DONE: {
            std::unique_ptr<UploadResult> result(reinterpret_cast<UploadResult*>(lp));
            finishUpload(*result);
            return 0;
        }
        case WM_GOTO_PAGE:
            gotoPage(int(wp));
            return 0;
        case WM_REFRESH:
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_TOAST: {
            std::unique_ptr<std::pair<std::wstring, ToastKind>> item(
                reinterpret_cast<std::pair<std::wstring, ToastKind>*>(lp));
            toast(item->first, item->second);
            return 0;
        }
        case WM_CLOSE: DestroyWindow(hwnd); return 0;
        case WM_DESTROY:
            KillTimer(hwnd, TIMER_TICK);
            KillTimer(hwnd, TIMER_TOAST);
            ThumbCache::instance().unsubscribe(hwnd);
            return 0;
        case WM_NCDESTROY:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            instance = nullptr;
            delete this;
            return 0;
        }
        return DefWindowProcW(hwnd, m, wp, lp);
    }

    void onCreate() {
        initScale();
        applyWindowChrome(hwnd, true);
        editBrush = CreateSolidBrush(rgb(pal().ibg));
        auto makeEdit = [&](int id, DWORD extra) {
            return CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL | extra, 0, 0, 10, 10, hwnd,
                                   reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
        };
        working = host.sync().settings();
        sidebar = working.navigation != "tabs";
        urlEdit = makeEdit(ID_EDIT_URL, 0);
        tokenEdit = makeEdit(ID_EDIT_TOKEN, ES_PASSWORD);
        hotkeyEdit = makeEdit(ID_EDIT_HOTKEY, 0);
        portEdit = makeEdit(ID_EDIT_PORT, ES_NUMBER);
        SetWindowTextW(urlEdit, working.piUrl.c_str());
        SetWindowTextW(tokenEdit, working.token.c_str());
        SetWindowTextW(hotkeyEdit, working.hotkey.c_str());
        SetWindowTextW(portEdit, std::to_wstring(working.retroArchNetworkPort).c_str());
        for (HWND e : {urlEdit, tokenEdit, hotkeyEdit, portEdit}) SendMessageW(e, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
        SendMessageW(tokenEdit, EM_SETPASSWORDCHAR, 0x2022, 0);
        applyEditFonts();
        refreshEntries(L"");
        ThumbCache::instance().subscribe(hwnd);
        SetTimer(hwnd, TIMER_TICK, 500, nullptr);
        logRevision = eventLog().revision();

        // size: 1100x720 DIP, limited to the work area
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        GetMonitorInfoW(MonitorFromWindow(host.window(), MONITOR_DEFAULTTOPRIMARY), &info);
        RECT frame{0, 0, LONG(1100 * scale), LONG(720 * scale)};
        AdjustWindowRectEx(&frame, GetWindowLongW(hwnd, GWL_STYLE), FALSE, GetWindowLongW(hwnd, GWL_EXSTYLE));
        const int workW = info.rcWork.right - info.rcWork.left, workH = info.rcWork.bottom - info.rcWork.top;
        const int w = std::min<int>(frame.right - frame.left, int(workW * 0.92)),
                  h = std::min<int>(frame.bottom - frame.top, int(workH * 0.92));
        SetWindowPos(hwnd, nullptr, info.rcWork.left + (workW - w) / 2, info.rcWork.top + (workH - h) / 2, w, h,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void applyEditFonts() {
        HFONT f = gdiFont(Face::Mono, int(13 * scale + 0.5f));
        for (HWND e : {urlEdit, tokenEdit, hotkeyEdit, portEdit}) SendMessageW(e, WM_SETFONT, reinterpret_cast<WPARAM>(f), TRUE);
    }

    void clampScroll() {
        const float room = std::max(0.f, contentHeight - viewport.Height);
        scrollY = std::clamp(scrollY, 0.f, room);
    }
    void gotoPage(int target) {
        if (target < 0 || target >= SETTINGS_PAGES) return;
        page = target;
        scrollY = 0;
        blocked = false;
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    void toast(const std::wstring& text, ToastKind kind) {
        const Palette& p = pal();
        toastText = text;
        toastColor = kind == ToastKind::Ok ? p.grn : kind == ToastKind::Info ? p.cyan : kind == ToastKind::Warn ? p.yel
                     : kind == ToastKind::Error ? p.red : p.ink2;
        toastShown = true;
        SetTimer(hwnd, TIMER_TOAST, 2600, nullptr);
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    // ---- status -------------------------------------------------------------------------------

    struct Look { std::wstring label, brief, sub; Color color; bool configured, connected; };
    Look look() {
        const Palette& p = pal();
        const Settings s = host.sync().settings();
        const PiStatus& st = host.status();
        if (!s.configured())
            return {tr(Str::StatusNotConfigured), tr(Str::StatusNotConfiguredShort), tr(Str::StatusSubNotConfigured), p.yel, false, false};
        if (!st.known) return {tr(Str::StatusChecking), L"", L"", p.yel, true, false};
        if (st.connected)
            return {tr(Str::StatusPiConnected), piHost(s) + L" · " + std::to_wstring(st.latencyMs) + L" ms",
                    fmt(Str::StatusSubConnected, s.piUrl.c_str(), st.latencyMs), p.grn, true, true};
        return {tr(Str::StatusUnreachable), tr(Str::StatusRetrying),
                st.lastContact ? fmt(Str::StatusSubUnreachable, clockText(st.lastContact, false).c_str())
                               : std::wstring(tr(Str::StatusSubNeverContacted)),
                p.red, true, false};
    }
    void statusDot(Canvas& c, float cx, float cy, float r, Color color) {
        c.glow(RectF(cx - r, cy - r, r * 2, r * 2), r, color, 10, 130);
        c.circle(cx, cy, r, color);
    }

    // ---- painting -----------------------------------------------------------------------------

    void placeEdit(HWND edit, RectF field) { placed[edit] = RectF(field.X + 12, field.Y + (field.Height - 18) / 2, field.Width - 24, 18); }
    // Input box with the pink bottom edge; the EDIT control sits inside it.
    void field(Canvas& c, RectF r, HWND edit) {
        const Palette& p = pal();
        c.fillRound(r, 6, p.ibg);
        c.strokeRound(r, 6, p.line);
        c.fillRect(RectF(r.X + 1, r.Y + r.Height - 2, r.Width - 2, 2), p.pink);
        placeEdit(edit, r);
    }

    void paint(Canvas& c, float W, float H) override {
        const Palette& p = pal();
        placed.clear();
        c.fillRect(RectF(0, 0, W, H), p.wbg);
        const float navW = sidebar ? NAV_W : 0.f, tabsH = sidebar ? 0.f : TABS_H;
        viewport = RectF(navW, tabsH, W - navW, H - FOOT_H - tabsH);

        clip = viewport;
        c.g.SetClip(viewport);
        const float bottom = drawPage(c);
        contentHeight = bottom + scrollY - viewport.Y + 28.f;
        clampScroll();
        c.g.ResetClip();
        clip = RectF(0, 0, 100000.f, 100000.f);
        c.scanlines(viewport, p.dark ? 18 : 4);
        if (contentHeight > viewport.Height + 1) {  // slim scroll indicator
            const float track = viewport.Height - 8.f, thumb = std::max(28.f, track * viewport.Height / contentHeight);
            const float top = viewport.Y + 4.f + (track - thumb) * (scrollY / std::max(1.f, contentHeight - viewport.Height));
            c.fillRound(RectF(viewport.X + viewport.Width - 7.f, top, 4.f, thumb), 2, withAlpha(p.ink2, 90));
        }
        drawFooter(c, W, H);
        if (sidebar) drawSidebar(c, H); else drawTabs(c, W);
        drawToast(c, W, H);
    }

    void afterPaint() override {
        for (HWND e : {urlEdit, tokenEdit, hotkeyEdit, portEdit}) {
            auto it = placed.find(e);
            bool visible = false;
            RECT pixels{};
            if (it != placed.end()) {
                const RectF r = it->second;
                visible = r.Y >= viewport.Y && r.Y + r.Height <= viewport.Y + viewport.Height;
                pixels = {LONG(r.X * scale), LONG(r.Y * scale), LONG((r.X + r.Width) * scale), LONG((r.Y + r.Height) * scale)};
            }
            if (!visible) {
                if (IsWindowVisible(e)) ShowWindow(e, SW_HIDE);
                applied.erase(e);
                continue;
            }
            auto known = applied.find(e);
            if (known == applied.end() || !EqualRect(&known->second, &pixels) || !IsWindowVisible(e)) {
                SetWindowPos(e, nullptr, pixels.left, pixels.top, pixels.right - pixels.left, pixels.bottom - pixels.top,
                             SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
                applied[e] = pixels;
            }
        }
    }

    float drawPage(Canvas& c) {
        switch (page) {
        case PAGE_DASHBOARD: return pageDashboard(c);
        case PAGE_CONNECTION: return pageConnection(c);
        case PAGE_MEDIA: return pageMedia(c);
        case PAGE_GESTURES: return pageGestures(c);
        case PAGE_RETROARCH: return pageRetroArch(c);
        case PAGE_GENERAL: return pageGeneral(c);
        default: return pageLogs(c);
        }
    }

    void drawSidebar(Canvas& c, float H) {
        const Palette& p = pal();
        RectF bar(0, 0, NAV_W, H);
        c.fillRect(bar, p.pbg);
        c.line(NAV_W - 0.5f, 0, NAV_W - 0.5f, H, p.line);
        const Look l = look();
        statusDot(c, 24, 36, 4.5f, l.color);
        c.text(l.label, RectF(40, 22, NAV_W - 52, 18), Face::BodySemi, 13, p.ink);
        c.text(l.brief, RectF(40, 40, NAV_W - 52, 16), Face::Body, 11, p.ink2);
        float y = 16 + 4 + 34 + 16;
        for (int i = 0; i < SETTINGS_PAGES; ++i) {
            RectF row(10, y, NAV_W - 20, 36);
            const bool on = page == i, over = hovered(ID_NAV + i);
            if (on) { c.glow(row, 6, p.pink, 9, 100); c.fillRound(row, 6, p.ibg); }
            else if (over) c.fillRound(row, 6, p.ibg);
            wchar_t index[8];
            swprintf(index, 8, L"%02d", i + 1);
            c.text(index, RectF(row.X + 10, row.Y, 24, row.Height), Face::Display, 10, on ? p.pink : p.ink2);
            c.text(trAt(Str::TabDashboard, i), RectF(row.X + 38, row.Y, row.Width - 46, row.Height), Face::BodyBold, 14,
                   on ? p.ink : p.ink2);
            add(row, ID_NAV + i);
            y += 39;
        }
        const std::wstring footer = fmt(Str::SidebarFooter, L"1.0");
        c.text(footer, RectF(20, H - 34, NAV_W - 30, 16), Face::Display, 10, p.ink2);
        if (blink) {
            const float w = c.textWidth(footer, Face::Display, 10);
            c.text(L"_", RectF(20 + w, H - 34, 12, 16), Face::Display, 10, p.ink2);
        }
    }
    void drawTabs(Canvas& c, float W) {
        const Palette& p = pal();
        c.fillRect(RectF(0, 0, W, TABS_H), p.pbg);
        c.line(0, TABS_H - 0.5f, W, TABS_H - 0.5f, p.line);
        float x = 16;
        for (int i = 0; i < SETTINGS_PAGES; ++i) {
            const std::wstring text = trAt(Str::TabDashboard, i);
            const float w = c.textWidth(text, Face::BodyBold, 13) + 28;
            RectF tab(x, 4, w, TABS_H - 4);
            const bool on = page == i;
            c.text(text, tab, Face::BodyBold, 13, on ? p.ink : p.ink2, Center);
            if (on) c.fillRect(RectF(x, TABS_H - 3, w, 2), p.pink);
            add(tab, ID_NAV + i);
            x += w + 4;
        }
    }
    void drawFooter(Canvas& c, float W, float H) {
        const Palette& p = pal();
        const float top = H - FOOT_H, left = sidebar ? NAV_W : 0.f;
        c.fillRect(RectF(left, top, W - left, FOOT_H), p.pbg);
        c.line(left, top + 0.5f, W, top + 0.5f, p.line);
        const Look l = look();
        std::wstring note = upload.active ? fmt(Str::UploadingTo, upload.percent)
                            : (l.configured && !l.connected) ? std::wstring(tr(Str::FootOffline)) : std::wstring(tr(Str::FootSaved));
        const float saveW = c.textWidth(tr(Str::Save), Face::BodyBold, 13) + 44,
                    cancelW = c.textWidth(tr(Str::Cancel), Face::BodySemi, 13) + 36;
        RectF save(W - 20 - saveW, top + 12, saveW, 34), cancel(save.X - 8 - cancelW, top + 12, cancelW, 34);
        c.text(note, RectF(left + 20, top, cancel.X - left - 32, FOOT_H), Face::Body, 12, p.ink2);
        button(c, cancel, tr(Str::Cancel), ID_CANCEL);
        button(c, save, tr(Str::Save), ID_SAVE, Button::Primary);
    }
    void drawToast(Canvas& c, float W, float H) {
        if (!toastShown) return;
        const Palette& p = pal();
        const float left = sidebar ? NAV_W : 0.f;
        const float tw = c.textWidth(toastText, Face::Body, 13), w = tw + 16 + 8 + 10 + 16;
        RectF r(left + (W - left - w) / 2, H - FOOT_H - 11 - 38, w, 38);
        c.glow(r, 6, toastColor, 8, 60);
        c.fillRound(r, 6, p.pbg);
        c.strokeRound(r, 6, toastColor);
        statusDot(c, r.X + 20, r.Y + 19, 4, toastColor);
        c.text(toastText, RectF(r.X + 34, r.Y, tw + 4, r.Height), Face::Body, 13, p.ink);
    }

    // ---- pages --------------------------------------------------------------------------------

    float top() const { return viewport.Y + 24.f - scrollY; }
    float left() const { return viewport.X + 28.f; }
    float width() const { return viewport.Width - 56.f; }

    float pageTitle(Canvas& c, const std::wstring& title, float x, float y, float w) {
        heading(c, title, RectF(x, y, w, 28));
        return y + 28;
    }
    void actionButton(Canvas& c, RectF r, const std::wstring& text, int id, bool danger, bool enabled) {
        const Palette& p = pal();
        const bool over = enabled && hovered(id);
        const int fade = enabled ? 255 : 115;
        if (danger) {
            if (over) c.fillRound(r, 6, withAlpha(p.red, 28));
            c.strokeRound(r, 6, withAlpha(p.red, fade));
        } else {
            if (over) c.glow(r, 6, p.cyan, 8, 70);
            c.fillRound(r, 6, withAlpha(p.ibg, fade));
            c.strokeRound(r, 6, p.line);
        }
        const float size = c.textWidth(text, Face::BodySemi, 13) > r.Width - 20 ? 12.f : 13.f;
        c.text(text, RectF(r.X + 10, r.Y, r.Width - 20, r.Height), Face::BodySemi, size, withAlpha(danger ? p.red : p.ink, fade));
        add(r, id, enabled);
    }
    Gdiplus::Bitmap* thumbFor(const std::wstring& name) {
        if (name.empty()) return nullptr;
        return ThumbCache::instance().get((fs::path(mediaDirectory()) / name).wstring());
    }
    void thumbBox(Canvas& c, RectF r, Gdiplus::Bitmap* bitmap, float radius, Color outline) {
        c.fillRound(r, radius, pal().ibg);
        c.image(bitmap, r, radius);
        c.strokeRound(r, radius, outline);
    }

    float pageDashboard(Canvas& c) {
        const Palette& p = pal();
        const Look l = look();
        const PiStatus& st = host.status();
        const Settings settings = host.sync().settings();
        const float x = left(), w = width();
        float y = top();
        heading(c, tr(Str::TabDashboard), RectF(x, y, w, 28));
        c.text(tr(Str::DashSubtitle), RectF(x + w - 320, y + 4, 320, 20), Face::Body, 12, p.ink2, Right);
        y += 28 + 18;

        if (!l.configured) {
            const float textW = w - 40 - 70 - 18 - 170 - 18;
            const float bodyH = c.textHeight(tr(Str::FirstRunBody), Face::Body, 13, textW);
            const float h = std::max(58.f, 20 + bodyH + 4) + 36;
            RectF r(x, y, w, h);
            c.fillRound(r, 8, p.pbg);
            c.strokeRound(r, 8, p.yel);
            c.text(L"P1", RectF(x + 20, y, 60, h), Face::Display, 26, p.yel);
            c.text(tr(Str::FirstRunTitle), RectF(x + 90, y + 18, textW, 20), Face::BodyBold, 15, p.ink);
            c.text(tr(Str::FirstRunBody), RectF(x + 90, y + 42, textW, bodyH + 4), Face::Body, 13, p.ink2, Left, false, true);
            RectF go(x + w - 20 - 170, y + (h - 36) / 2, 170, 36);
            c.fillRound(go, 6, hovered(ID_SETUP) ? withAlpha(p.yel, 225) : p.yel);
            c.text(tr(Str::FirstRunButton), go, Face::BodyBold, 13, Color(255, 0x1a, 0x12, 0x00), Center);
            add(go, ID_SETUP);
            y += h + 18;
        }

        const float colA = (w - 16) * 1.35f / 2.35f, colB = w - 16 - colA;
        // row 1: Now showing | Connection + Current game
        {
            const float innerA = colA - 32;
            const float previewH = innerA * 0.6f;
            std::wstring caption;
            if (!l.configured) caption = tr(Str::CaptionConnectFirst);
            else if (!l.connected) caption = tr(Str::CaptionOffline);
            else if (st.gameActive) caption = fmt(Str::CaptionMarquee, st.gameTitle.c_str());
            else caption = fmt(Str::CaptionDefaultMedia, roleFile(ROLES[0].marker).empty() ? L"—" : roleFile(ROLES[0].marker).c_str());
            const float captionH = c.textHeight(caption, Face::Body, 12, innerA);
            const float leftH = 16 + 18 + 10 + previewH + 10 + captionH + 16;

            const float connH = 16 + 16 + 10 + 24 + 10 + (3 * 19 + 2 * 6) + 16;
            const std::wstring gameTitle = st.gameActive ? st.gameTitle : std::wstring(tr(Str::NoGameRunning));
            const std::wstring gameSub = !st.gameActive ? std::wstring(tr(Str::WaitingForGame))
                                         : !l.connected ? std::wstring(tr(Str::GameQueued))
                                                        : fmt(Str::GameStartedVia, clockText(st.gameStarted, false).c_str());
            const float innerB = colB - 32;
            const float titleH = c.textHeight(gameTitle, Face::BodyBold, 16, innerB) + 4;
            const float subH = c.textHeight(gameSub, Face::Body, 13, innerB);
            const float gameH = 16 + 16 + 8 + titleH + 8 + subH + 16;
            const float rowH = std::max(leftH, connH + 16 + gameH);

            RectF a(x, y, colA, rowH);
            panel(c, a);
            label(c, tr(Str::PanelNowShowing), RectF(a.X + 16, a.Y + 16, 200, 18));
            const Color tagColor = l.configured ? l.color : p.ink2;
            c.text(!l.configured ? L"—" : l.connected ? tr(Str::TagLive) : tr(Str::TagOffline),
                   RectF(a.X + a.Width - 16 - 120, a.Y + 16, 120, 18), Face::BodySemi, 11, tagColor, Right);
            drawPreview(c, RectF(a.X + 16, a.Y + 16 + 18 + 10, innerA, previewH), l, st);
            c.text(caption, RectF(a.X + 16, a.Y + 16 + 18 + 10 + previewH + 10, innerA, captionH + 2), Face::Body, 12, p.ink2,
                   Left, false, true);

            RectF b(x + colA + 16, y, colB, connH);
            panel(c, b);
            label(c, tr(Str::PanelConnection), RectF(b.X + 16, b.Y + 16, 200, 18));
            statusDot(c, b.X + 21, b.Y + 16 + 16 + 10 + 12, 5, l.color);
            c.text(l.label, RectF(b.X + 38, b.Y + 16 + 16 + 10, innerB - 22, 24), Face::BodyBold, 18, p.ink);
            float ky = b.Y + 16 + 16 + 10 + 24 + 10;
            const std::pair<const wchar_t*, std::wstring> rows[3] = {
                {tr(Str::LabelAddress), settings.piUrl.empty() ? L"—" : settings.piUrl},
                {tr(Str::LabelLatency), l.connected ? std::to_wstring(st.latencyMs) + L" ms" : L"—"},
                {tr(Str::LabelPluginPipe), st.pipeListening ? tr(Str::PipeListening) : tr(Str::PipeStopped)}};
            float keyW = 0;
            for (const auto& row : rows) keyW = std::max(keyW, c.textWidth(row.first, Face::Body, 13));
            for (const auto& row : rows) {
                c.text(row.first, RectF(b.X + 16, ky, keyW + 4, 19), Face::Body, 13, p.ink2);
                const float valueW = innerB - keyW - 14;
                float size = 12.f;
                while (size > 9.f && c.textWidth(row.second, Face::Mono, size) > valueW) size -= 0.5f;
                c.text(row.second, RectF(b.X + 16 + keyW + 14, ky, valueW, 19), Face::Mono, size, p.ink);
                ky += 25;
            }

            RectF g(b.X, b.Y + connH + 16, colB, gameH);
            panel(c, g);
            label(c, tr(Str::PanelCurrentGame), RectF(g.X + 16, g.Y + 16, 200, 18));
            c.text(gameTitle, RectF(g.X + 16, g.Y + 16 + 16 + 8, innerB, titleH), Face::BodyBold, 16, p.ink, Left, false, true);
            c.text(gameSub, RectF(g.X + 16, g.Y + 16 + 16 + 8 + titleH + 8, innerB, subH + 2), Face::Body, 13, p.ink2, Left,
                   false, true);
            y += rowH + 16;
        }
        // row 2: Active roles | Quick actions
        {
            const float innerA = colA - 32;
            const float cardW = (innerA - 20) / 3.f, thumbH = cardW * 0.6f;
            const float rolesH = 16 + 18 + 12 + thumbH + 6 + 15 + 6 + 17 + 16;
            const float actionsH = 16 + 16 + 10 + 38 * 2 + 8 + 16;
            const float rowH = std::max(rolesH, actionsH);
            RectF a(x, y, colA, rowH);
            panel(c, a);
            label(c, tr(Str::PanelActiveRoles), RectF(a.X + 16, a.Y + 16, 240, 18));
            const float linkW = c.textWidth(tr(Str::MenuMedia), Face::BodySemi, 12) + 4;
            button(c, RectF(a.X + a.Width - 16 - linkW, a.Y + 14, linkW, 20), tr(Str::MenuMedia), ID_GOMEDIA, Button::Link);
            for (int r = 0; r < 3; ++r) {
                const float cx = a.X + 16 + r * (cardW + 10), cy = a.Y + 16 + 18 + 12;
                const std::wstring name = roleFile(ROLES[r].marker);
                thumbBox(c, RectF(cx, cy, cardW, thumbH), thumbFor(name), 5, roleColor(r));
                c.text(tr(ROLES[r].chip), RectF(cx, cy + thumbH + 6, cardW, 15), Face::BodyBold, 11, roleColor(r));
                c.text(name.empty() ? L"—" : name, RectF(cx, cy + thumbH + 6 + 15 + 6, cardW, 17), Face::Body, 12, p.ink);
            }
            RectF b(x + colA + 16, y, colB, rowH);
            panel(c, b);
            label(c, tr(Str::PanelQuickActions), RectF(b.X + 16, b.Y + 16, 240, 18));
            const float bw = (colB - 32 - 8) / 2.f;
            const Str labels[4] = {Str::MenuDefault, Str::MenuReload, Str::MenuReboot, Str::MenuShutdown};
            for (int i = 0; i < 4; ++i)
                actionButton(c, RectF(b.X + 16 + (i % 2) * (bw + 8), b.Y + 16 + 16 + 10 + (i / 2) * 46, bw, 38),
                             tr(labels[i]), ID_ACT + i, i == 3, l.connected);
            y += rowH + 16;
        }
        // recent events
        {
            const auto entries = eventLog().snapshot();
            const int shown = int(std::min<size_t>(4, entries.size()));
            const float h = 16 + 18 + 8 + std::max(1, shown) * 22 + 16;
            RectF a(x, y, w, h);
            panel(c, a);
            label(c, tr(Str::PanelRecentEvents), RectF(a.X + 16, a.Y + 16, 240, 18));
            const float linkW = c.textWidth(tr(Str::LinkAllLogs), Face::BodySemi, 12) + 4;
            button(c, RectF(a.X + a.Width - 16 - linkW, a.Y + 14, linkW, 20), tr(Str::LinkAllLogs), ID_GOLOGS, Button::Link);
            float ly = a.Y + 16 + 18 + 8;
            if (shown == 0) c.text(tr(Str::LogEmpty), RectF(a.X + 16, ly, w - 32, 20), Face::Body, 12, p.ink2);
            for (int i = 0; i < shown; ++i, ly += 22) logRow(c, entries[size_t(i)], a.X + 16, ly, w - 32, false);
            y += h;
        }
        return y;
    }

    Color levelColor(LogLevel level) { return level == LogLevel::Error ? pal().red : level == LogLevel::Warn ? pal().yel : pal().cyan; }
    const wchar_t* levelName(LogLevel level) { return level == LogLevel::Error ? L"ERROR" : level == LogLevel::Warn ? L"WARN" : L"INFO"; }
    void logRow(Canvas& c, const LogEntry& e, float x, float y, float w, bool withSource) {
        const Palette& p = pal();
        c.text(clockText(e.time, true), RectF(x, y, 64, 20), Face::Mono, 12, p.ink2);
        c.text(levelName(e.level), RectF(x + 74, y, 56, 20), Face::MonoMedium, 12, levelColor(e.level));
        float mx = x + 140;
        if (withSource) {
            c.text(fromUtf8(e.source), RectF(mx, y, 76, 20), Face::Mono, 12, p.ink2);
            mx += 86;
        }
        c.text(e.message, RectF(mx, y, x + w - mx, 20), Face::Mono, 12, p.ink);
    }

    void drawPreview(Canvas& c, RectF r, const Look& l, const PiStatus& st) {
        const Palette& p = pal();
        c.fillRound(r, 6, Color(255, 0, 0, 0));
        if (!l.configured) {
            c.fillRound(r, 6, Color(255, 0x0a, 0x08, 0x14));
            c.text(tr(Str::StatusNotConfigured), r, Face::Display, 16, Color(255, 0x8a, 0x7f, 0xb0), Center);
        } else if (!l.connected) {
            Gdiplus::GraphicsState state = c.g.Save();
            std::unique_ptr<Gdiplus::GraphicsPath> path(roundPath(r, 6));
            c.g.SetClip(path.get(), Gdiplus::CombineModeIntersect);
            for (float yy = r.Y - 5 + float(noisePhase % 5); yy < r.Y + r.Height; yy += 5) {
                c.fillRect(RectF(r.X, yy, r.Width, 2), Color(255, 0x14, 0x14, 0x14));
                c.fillRect(RectF(r.X, yy + 2, r.Width, 1), Color(255, 0x26, 0x26, 0x26));
            }
            c.g.Restore(state);
            const float size = std::clamp(r.Width / 14.f, 16.f, 28.f);
            c.text(tr(Str::NoSignal), RectF(r.X + 1, r.Y + 1, r.Width, r.Height), Face::Display, size, withAlpha(p.red, 90), Center);
            c.text(tr(Str::NoSignal), r, Face::Display, size, p.red, Center);
        } else {
            Gdiplus::Bitmap* image = nullptr;
            if (st.gameActive && !st.gameMarquee.empty()) image = ThumbCache::instance().get(st.gameMarquee);
            if (!image) image = thumbFor(roleFile(ROLES[0].marker));
            if (image) c.image(image, r, 6);
            else {
                c.gradient(r, 6, Color(255, 0x2a, 0x0a, 0x55), Color(255, 0xff, 0x7a, 0x1a), 45);
                const std::wstring title = st.gameActive ? st.gameTitle : std::wstring(tr(Str::DefaultMediaTitle));
                c.text(title, RectF(r.X + 10, r.Y, r.Width - 20, r.Height), Face::DisplayBold, 22, Color(255, 0xff, 0xe4, 0x5c), Center,
                       true, true);
            }
        }
        c.scanlines(r, 64);
        c.strokeRound(r, 6, Color(255, 0x22, 0x1a, 0x3a), 2);
    }

    float pageConnection(Canvas& c) {
        const Palette& p = pal();
        const Look l = look();
        const float x = left(), w = std::min(width(), 620.f);
        float y = top();
        y = pageTitle(c, tr(Str::TabConnection), x, y, w) + 18;

        RectF card(x, y, w, 64);
        panel(c, card);
        statusDot(c, x + 27, y + 32, 5, l.color);
        c.text(l.label, RectF(x + 48, y + 12, w - 200, 20), Face::BodyBold, 14, p.ink);
        c.text(l.sub, RectF(x + 48, y + 32, w - 200, 20), Face::Body, 12, p.ink2);
        const std::wstring testLabel = testing ? tr(Str::Testing) : tr(Str::TestConnection);
        const float tw = std::max(120.f, c.textWidth(testLabel, Face::BodySemi, 13) + 32);
        button(c, RectF(x + w - 16 - tw, y + 15, tw, 34), testLabel, ID_TEST, Button::Outline, !testing);
        y += 64 + 18;

        if (messageShown) {
            const float h = c.textHeight(messageText, Face::Body, 13, w - 28) + 20;
            RectF r(x, y, w, h);
            c.fillRound(r, 6, p.ibg);
            c.strokeRound(r, 6, messageColor);
            c.text(messageText, RectF(x + 14, y + 10, w - 28, h - 20), Face::Body, 13, messageColor, Left, false, true);
            y += h + 18;
        }

        c.text(tr(Str::PiAddress), RectF(x, y, w, 18), Face::BodySemi, 13, p.ink);
        y += 24;
        field(c, RectF(x, y, w, 38), urlEdit);
        y += 38 + 18;

        c.text(tr(Str::AccessToken), RectF(x, y, w, 18), Face::BodySemi, 13, p.ink);
        y += 24;
        const std::wstring toggleText = showToken ? tr(Str::HideToken) : tr(Str::ShowToken);
        const float bw = c.textWidth(toggleText, Face::Body, 13) + 28;
        field(c, RectF(x, y, w - bw - 8, 38), tokenEdit);
        button(c, RectF(x + w - bw, y, bw, 38), toggleText, ID_TOKEN_TOGGLE);
        y += 38 + 6;
        const int chars = GetWindowTextLengthW(tokenEdit);
        c.text(chars == 0 ? std::wstring(tr(Str::TokenNone)) : chars < 24 ? fmt(Str::TokenTooShort, chars) : fmt(Str::TokenCount, chars),
               RectF(x, y, w, 18), Face::Body, 12, p.ink2);
        y += 18 + 18;

        const float hw = c.textWidth(tr(Str::HelpToken), Face::BodySemi, 13) + 2;
        const bool over = hovered(ID_HELP);
        c.text(tr(Str::HelpToken), RectF(x, y, hw + 4, 20), Face::BodySemi, 13, over ? p.pink : p.cyan);
        c.line(x, y + 19, x + hw, y + 19, over ? p.pink : p.cyan);
        add(RectF(x, y, hw + 4, 22), ID_HELP);
        y += 22 + 18;

        const float bodyH = c.textHeight(tr(Str::SecurityBody), Face::Body, 12, w - 28);
        RectF note(x, y, w, 12 + 18 + 4 + bodyH + 12);
        c.fillRound(note, 6, p.ibg);
        c.text(tr(Str::SecurityTitle), RectF(x + 14, y + 12, w - 28, 18), Face::BodyBold, 12, p.yel);
        c.text(tr(Str::SecurityBody), RectF(x + 14, y + 34, w - 28, bodyH + 2), Face::Body, 12, p.ink2, Left, false, true);
        return y + note.Height;
    }

    // ---- media page ------------------------------------------------------------------------------

    void refreshEntries(const std::wstring& select) {
        const std::wstring keep = select.empty() && selected >= 0 && size_t(selected) < entries.size() ? entries[size_t(selected)].name : select;
        entries.clear();
        std::error_code ec;
        fs::create_directories(fs::path(mediaDirectory()), ec);
        for (fs::directory_iterator it(fs::path(mediaDirectory()), ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code fileError;
            if (!it->is_regular_file(fileError) || fileError) continue;
            Entry e;
            e.name = it->path().filename().wstring();
            e.path = it->path().wstring();
            e.size = it->file_size(fileError);
            e.ext = lower(it->path().extension().wstring());
            entries.push_back(std::move(e));
        }
        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.name < b.name; });
        selected = entries.empty() ? -1 : 0;
        for (size_t i = 0; i < entries.size(); ++i) if (entries[i].name == keep) selected = int(i);
        if (keep == L"\x01") selected = entries.empty() ? -1 : 0;
    }
    int roleOf(const Entry& e, int role) { return roleFile(ROLES[role].marker) == e.name; }
    bool isVideo(const Entry& e) { return e.ext == L".mp4"; }
    bool isStaticImage(const Entry& e) { return e.ext == L".png" || e.ext == L".jpg" || e.ext == L".jpeg"; }

    void mediaThumb(Canvas& c, const Entry& e, RectF r, float radius, bool badge) {
        const Palette& p = pal();
        Gdiplus::Bitmap* image = ThumbCache::instance().get(e.path);
        c.fillRound(r, radius, p.ibg);
        if (image) c.image(image, r, radius);
        else if (ThumbCache::instance().failed(e.path)) {
            Gdiplus::GraphicsState state = c.g.Save();
            std::unique_ptr<Gdiplus::GraphicsPath> path(roundPath(r, radius));
            c.g.SetClip(path.get(), Gdiplus::CombineModeIntersect);
            for (float o = -r.Height; o < r.Width; o += 12) c.line(r.X + o, r.Y + r.Height, r.X + o + r.Height, r.Y, p.line, 4);
            c.g.Restore(state);
            c.text(tr(Str::MediaNoPreview), r, Face::Display, 10, p.ink2, Center);
        } else {
            c.text(L"…", r, Face::Display, 12, p.ink2, Center);
        }
        if (badge && r.Width > 100) {
            std::wstring text = upper(e.ext.size() > 1 ? e.ext.substr(1) : e.ext);
            const float bw = c.textWidth(text, Face::Mono, 10) + 10;
            RectF b(r.X + r.Width - 5 - bw, r.Y + r.Height - 5 - 16, bw, 16);
            c.fillRound(b, 3, Color(153, 0, 0, 0));
            c.text(text, b, Face::Mono, 10, Color(255, 255, 255, 255), Center);
        }
        if (upload.active && upload.name == e.name) {
            c.fillRound(r, radius, Color(184, 8, 4, 20));
            c.text(fmt(Str::UploadingTo, upload.percent), RectF(r.X + 8, r.Y + r.Height - 34, r.Width - 16, 16), Face::BodySemi, 11,
                   Color(255, 255, 255, 255));
            RectF bar(r.X + 8, r.Y + r.Height - 14, r.Width - 16, 4);
            c.fillRound(bar, 2, Color(51, 255, 255, 255));
            c.glow(RectF(bar.X, bar.Y, bar.Width * float(upload.percent) / 100.f, 4), 2, p.cyan, 6, 120);
            c.fillRound(RectF(bar.X, bar.Y, bar.Width * float(upload.percent) / 100.f, 4), 2, p.cyan);
        }
    }

    float pageMedia(Canvas& c) {
        const Palette& p = pal();
        const Look l = look();
        const float x = left(), w = width();
        float y = top();
        // header row
        const std::wstring addText = L"+ " + std::wstring(tr(Str::MediaAdd));
        const float addW = c.textWidth(addText, Face::BodyBold, 13) + 32;
        const float segW1 = c.textWidth(tr(Str::MediaGrid), Face::BodySemi, 12) + 24,
                    segW2 = c.textWidth(tr(Str::MediaListView), Face::BodySemi, 12) + 24;
        heading(c, tr(Str::TabMedia), RectF(x, y, w - addW - segW1 - segW2 - 6 - 20, 28));
        RectF add_(x + w - addW, y - 1, addW, 34);
        button(c, add_, addText, ID_ADD, Button::Primary);
        RectF seg(add_.X - 10 - (segW1 + segW2 + 6), y - 1, segW1 + segW2 + 6, 34);
        c.fillRound(seg, 6, p.ibg);
        c.strokeRound(seg, 6, p.line);
        button(c, RectF(seg.X + 3, seg.Y + 3, segW1, 28), tr(Str::MediaGrid), ID_VIEW_GRID, grid ? Button::SegmentOn : Button::Segment);
        button(c, RectF(seg.X + 3 + segW1, seg.Y + 3, segW2, 28), tr(Str::MediaListView), ID_VIEW_LIST, grid ? Button::Segment : Button::SegmentOn);
        y += 34 + 8;
        c.text(tr(Str::MediaSubline), RectF(x, y, w, 18), Face::Body, 12, p.ink2);
        y += 18 + 14;

        if (uploadError.shown) {
            const std::wstring body = fmt(Str::UploadErrorBody, uploadError.name.c_str(), uploadError.message.c_str(),
                                          lower(tr(ROLES[uploadError.role].name)).c_str());
            const float retryW = c.textWidth(tr(Str::Retry), Face::BodySemi, 12) + 24;
            const float labelW = c.textWidth(tr(Str::LogFilterError), Face::Display, 13) + 16;
            const float textW = w - 28 - labelW - retryW - 12 - 24 - 12;
            const float bodyH = c.textHeight(body, Face::Body, 13, textW);
            const float h = std::max(48.f, 12 + 18 + 3 + bodyH + 12);
            RectF r(x, y, w, h);
            c.glow(r, 6, p.red, 10, 70);
            c.fillRound(r, 6, p.pbg);
            c.strokeRound(r, 6, p.red);
            c.text(tr(Str::LogFilterError), RectF(x + 14, y + 12, labelW, 18), Face::Display, 13, p.red);
            c.text(tr(Str::UploadFailed), RectF(x + 14 + labelW, y + 12, textW, 18), Face::BodyBold, 13, p.ink);
            c.text(body, RectF(x + 14 + labelW, y + 33, textW, bodyH + 2), Face::Body, 13, p.ink2, Left, false, true);
            button(c, RectF(x + w - 14 - 24 - 12 - retryW, y + 10, retryW, 30), tr(Str::Retry), ID_RETRY, Button::Danger);
            c.text(L"x", RectF(x + w - 14 - 22, y + 10, 22, 30), Face::BodySemi, 14, hovered(ID_DISMISS) ? p.ink : p.ink2, Center);
            add(RectF(x + w - 14 - 26, y + 10, 26, 30), ID_DISMISS);
            y += h + 14;
        }

        const float detailW = 230, listW = w - detailW - 16;
        const float startY = y;
        float listBottom = startY;
        if (entries.empty()) {
            c.text(tr(Str::NoMedia), RectF(x, y + 8, listW, 22), Face::Body, 13, p.ink2);
            listBottom = y + 40;
        } else if (grid) {
            const int cols = std::max(1, int((listW + 12) / (150 + 12)));
            const float cardW = (listW - float(cols - 1) * 12) / float(cols);
            const float thumbH = (cardW - 16) * 0.6f, cardH = 8 + thumbH + 7 + 18 + 7 + 18 + 8;
            for (size_t i = 0; i < entries.size(); ++i) {
                const Entry& e = entries[i];
                RectF card(x + float(int(i) % cols) * (cardW + 12), startY + float(int(i) / cols) * (cardH + 12), cardW, cardH);
                const bool on = int(i) == selected;
                if (on) c.glow(card, 8, p.pink, 9, 100);
                c.fillRound(card, 8, p.pbg);
                c.strokeRound(card, 8, on ? p.pink : p.line);
                mediaThumb(c, e, RectF(card.X + 8, card.Y + 8, cardW - 16, thumbH), 5, true);
                c.text(e.name, RectF(card.X + 8, card.Y + 8 + thumbH + 7, cardW - 16, 18), Face::BodySemi, 13, p.ink);
                float cx = card.X + 8;
                for (int r = 0; r < 3; ++r) {
                    if (!roleOf(e, r)) continue;
                    float cw = 0;
                    chip(c, cx, card.Y + 8 + thumbH + 7 + 18 + 7, tr(ROLES[r].chip), roleColor(r), &cw);
                    cx += cw + 4;
                }
                add(card, ID_MEDIA + int(i));
                listBottom = std::max(listBottom, card.Y + cardH);
            }
        } else {
            const float rowH = 48, headH = 30;
            RectF table(x, startY, listW, headH + rowH * float(entries.size()));
            c.fillRound(table, 8, p.pbg);
            const float avail = listW - 24 - 56 - 60 - 70 - 12 * 4;
            const float nameW = avail / 2.1f, roleW = avail * 1.1f / 2.1f;
            const float cx0 = x + 12, cx1 = cx0 + 56 + 12, cx2 = cx1 + nameW + 12, cx3 = cx2 + 60 + 12, cx4 = cx3 + 70 + 12;
            c.text(tr(Str::ColName), RectF(cx1, startY, nameW, headH), Face::BodyBold, 11, p.ink2);
            c.text(tr(Str::ColType), RectF(cx2, startY, 60, headH), Face::BodyBold, 11, p.ink2);
            c.text(tr(Str::ColSize), RectF(cx3, startY, 70, headH), Face::BodyBold, 11, p.ink2);
            c.text(tr(Str::ColRoles), RectF(cx4, startY, roleW, headH), Face::BodyBold, 11, p.ink2);
            c.line(x, startY + headH - 0.5f, x + listW, startY + headH - 0.5f, p.line);
            for (size_t i = 0; i < entries.size(); ++i) {
                const Entry& e = entries[i];
                const float ry = startY + headH + float(i) * rowH;
                RectF row(x, ry, listW, rowH);
                if (int(i) == selected) c.fillRect(RectF(x + 1, ry, listW - 2, rowH), p.ibg);
                else if (hovered(ID_MEDIA + int(i))) c.fillRect(RectF(x + 1, ry, listW - 2, rowH), withAlpha(p.ibg, 120));
                mediaThumb(c, e, RectF(cx0, ry + (rowH - 33.6f) / 2, 56, 33.6f), 3, false);
                c.text(e.name, RectF(cx1, ry, nameW, rowH), Face::BodySemi, 13, p.ink);
                c.text(isVideo(e) ? tr(Str::MediaVideo) : tr(Str::MediaImage), RectF(cx2, ry, 60, rowH), Face::Body, 13, p.ink2);
                c.text(sizeText(e.size), RectF(cx3, ry, 70, rowH), Face::Mono, 12, p.ink2);
                float cx = cx4;
                for (int r = 0; r < 3; ++r) {
                    if (!roleOf(e, r)) continue;
                    float cw = 0;
                    chip(c, cx, ry + (rowH - 18) / 2, tr(ROLES[r].chip), roleColor(r), &cw);
                    cx += cw + 4;
                }
                if (i + 1 < entries.size()) c.line(x, ry + rowH - 0.5f, x + listW, ry + rowH - 0.5f, p.line);
                add(row, ID_MEDIA + int(i));
            }
            c.strokeRound(table, 8, p.line);
            listBottom = table.Y + table.Height;
        }

        // detail panel
        float detailBottom = startY;
        {
            RectF d(x + listW + 16, startY, detailW, 100);
            const Entry* e = selected >= 0 && size_t(selected) < entries.size() ? &entries[size_t(selected)] : nullptr;
            const float innerW = detailW - 28;
            const float thumbH = innerW * 0.6f;
            float dy = startY + 14;
            std::wstring meta = e ? std::wstring(isVideo(*e) ? tr(Str::MediaVideo) : tr(Str::MediaImage)) + L" · " + sizeText(e->size) : L"";
            const std::wstring hint = e && isStaticImage(*e) ? tr(Str::MediaHintStatic) : tr(Str::MediaHintVideo);
            const float nameH = e ? c.textHeight(e->name, Face::BodyBold, 14, innerW) + 2 : 0;
            const float hintH = c.textHeight(hint, Face::Body, 11, innerW);
            const float blockedH = blocked ? c.textHeight(tr(Str::ReplaceRolesFirst), Face::Body, 12, innerW) + 2 : 0;
            d.Height = 14 + thumbH + 12 + nameH + 18 + 12 + 3 * 34 + 2 * 6 + 12 + hintH + (blocked ? 12 + blockedH : 0) + 12 + 32 + 14;
            panel(c, d);
            if (e) {
                mediaThumb(c, *e, RectF(d.X + 14, dy, innerW, thumbH), 5, false);
                dy += thumbH + 12;
                c.text(e->name, RectF(d.X + 14, dy, innerW, nameH), Face::BodyBold, 14, p.ink, Left, false, true);
                dy += nameH;
                c.text(meta, RectF(d.X + 14, dy, innerW, 18), Face::Body, 12, p.ink2);
                dy += 18 + 12;
                for (int r = 0; r < 3; ++r) {
                    RectF b(d.X + 14, dy, innerW, 34);
                    const bool has = roleOf(*e, r) != 0;
                    const bool disabled = (r == 1 && !isStaticImage(*e)) || upload.active;
                    const bool over = !disabled && !has && hovered(ID_ROLE + r);
                    if (over) c.fillRound(b, 6, withAlpha(p.ibg, 200));
                    if (has) c.fillRound(b, 6, p.ibg);
                    c.strokeRound(b, 6, has ? roleColor(r) : p.line);
                    c.fillRound(RectF(b.X + 10, b.Y + 13, 8, 8), 2, roleColor(r));
                    const std::wstring text = has ? std::wstring(tr(ROLES[r].chip)) + L" ✓" : std::wstring(tr(ROLES[r].button));
                    c.text(text, RectF(b.X + 26, b.Y, b.Width - 34, b.Height), Face::BodySemi, 12,
                           withAlpha(disabled ? p.ink2 : p.ink, 255));
                    add(b, ID_ROLE + r, !disabled && !has);
                    dy += 34 + 6;
                }
                dy += 6;
                c.text(hint, RectF(d.X + 14, dy, innerW, hintH + 2), Face::Body, 11, p.ink2, Left, false, true);
                dy += hintH + 12;
                if (blocked) {
                    c.text(tr(Str::ReplaceRolesFirst), RectF(d.X + 14, dy, innerW, blockedH), Face::Body, 12, p.red, Left, false, true);
                    dy += blockedH + 12;
                }
                const float half = (innerW - 6) / 2;
                button(c, RectF(d.X + 14, dy, half, 32), tr(Str::MediaPreview), ID_PREVIEW);
                button(c, RectF(d.X + 14 + half + 6, dy, half, 32), tr(Str::MediaRemove), ID_REMOVE);
            } else {
                c.text(tr(Str::NoMedia), RectF(d.X + 14, dy, innerW, 60), Face::Body, 12, p.ink2, Left, false, true);
            }
            detailBottom = d.Y + d.Height;
        }
        (void)l;
        return std::max(listBottom, detailBottom);
    }

    // ---- gestures, RetroArch, General ------------------------------------------------------------

    void gestureIcon(Canvas& c, int kind, float cx, float cy, Color color) {
        if (kind == 0) { c.circle(cx, cy, 4.5f, color); return; }
        const float a = 6.f;
        float dx = 0, dy = 0;
        if (kind == 1) dy = 1; else if (kind == 2) dy = -1; else if (kind == 3) dx = 1; else dx = -1;
        c.line(cx - dx * a, cy - dy * a, cx + dx * a, cy + dy * a, color, 2.f);
        c.line(cx + dx * a, cy + dy * a, cx + dx * a - (dx ? dx * 4 : 4), cy + dy * a - (dy ? dy * 4 : 4), color, 2.f);
        c.line(cx + dx * a, cy + dy * a, cx + dx * a - (dx ? dx * 4 : -4), cy + dy * a - (dy ? dy * 4 : -4), color, 2.f);
    }

    float pageGestures(Canvas& c) {
        const Palette& p = pal();
        const float x = left(), w = std::min(width(), 620.f);
        float y = top();
        y = pageTitle(c, tr(Str::TabGestures), x, y, w) + 16;
        const float introH = c.textHeight(tr(Str::GesturesIntro), Face::Body, 13, w);
        c.text(tr(Str::GesturesIntro), RectF(x, y, w, introH + 2), Face::Body, 13, p.ink2, Left, false, true);
        y += introH + 16;
        RectF box(x, y, w, 55.f * GESTURE_COUNT);
        c.fillRound(box, 8, p.pbg);
        bool menuReachable = false;
        for (int i = 0; i < GESTURE_COUNT; ++i) {
            const float ry = y + 55.f * float(i);
            std::string current = i == 0 ? "touch_menu" : "none";
            auto it = working.gestures.find(GESTURE_IDS[i]);
            if (it != working.gestures.end()) current = it->second;
            int index = 0;
            for (int j = 0; j < ACTION_COUNT; ++j) if (current == ACTION_IDS[j]) index = j;
            menuReachable = menuReachable || current == "touch_menu";
            gestureIcon(c, i, x + 25, ry + 27.5f, p.pink);
            c.text(trAt(Str::GestureLongPress, i), RectF(x + 50, ry, w - 330, 55), Face::BodySemi, 14, p.ink);
            RectF sel(x + w - 14 - 240, ry + 10.5f, 240, 34);
            gestureBox[i] = sel;
            const bool over = hovered(ID_GESTURE + i);
            if (over) c.glow(sel, 6, p.cyan, 8, 70);
            c.fillRound(sel, 6, p.ibg);
            c.strokeRound(sel, 6, p.line);
            c.text(trAt(Str::ActionNone, index), RectF(sel.X + 10, sel.Y, sel.Width - 34, sel.Height), Face::Body, 13, p.ink);
            c.line(sel.X + sel.Width - 22, sel.Y + 14, sel.X + sel.Width - 17, sel.Y + 19, p.ink2, 1.6f);
            c.line(sel.X + sel.Width - 17, sel.Y + 19, sel.X + sel.Width - 12, sel.Y + 14, p.ink2, 1.6f);
            add(sel, ID_GESTURE + i);
            if (i + 1 < GESTURE_COUNT) c.line(x, ry + 54.5f, x + w, ry + 54.5f, p.line);
        }
        c.strokeRound(box, 8, p.line);
        y += box.Height;
        if (!menuReachable) {
            y += 16;
            const float h = c.textHeight(tr(Str::GesturesWarning), Face::Body, 13, w);
            c.text(tr(Str::GesturesWarning), RectF(x, y, w, h + 2), Face::Body, 13, p.yel, Left, false, true);
            y += h;
        }
        return y;
    }

    void radioCard(Canvas& c, RectF r, const std::wstring& title, const std::wstring& sub, bool on, int id) {
        const Palette& p = pal();
        if (on) c.glow(r, 6, p.pink, 9, 100);
        c.fillRound(r, 6, p.pbg);
        c.strokeRound(r, 6, on ? p.pink : (hovered(id) ? p.ink2 : p.line));
        radio(c, r.X + 21, r.Y + r.Height / 2, on);
        if (sub.empty()) {
            c.text(title, RectF(r.X + 42, r.Y, r.Width - 54, r.Height), Face::Body, 14, p.ink);
        } else {
            c.text(title, RectF(r.X + 42, r.Y + 9, r.Width - 54, 20), Face::BodySemi, 14, p.ink);
            c.text(sub, RectF(r.X + 42, r.Y + 30, r.Width - 54, 18), Face::Body, 12, p.ink2);
        }
        add(r, id);
    }

    float pageRetroArch(Canvas& c) {
        const Palette& p = pal();
        const float x = left(), w = std::min(width(), 620.f);
        float y = top();
        y = pageTitle(c, tr(Str::TabRetroArch), x, y, w) + 16;
        c.text(tr(Str::ControlMethod), RectF(x, y, w, 18), Face::BodySemi, 13, p.ink);
        y += 26;
        radioCard(c, RectF(x, y, w, 46), tr(Str::ModeKeyboard), L"", !working.retroArchNetworkControl, ID_RETRO);
        y += 54;
        radioCard(c, RectF(x, y, w, 46), tr(Str::ModeNetwork), L"", working.retroArchNetworkControl, ID_RETRO + 1);
        y += 46 + 16;
        const float colW = (w - 14) / 2;
        const float hint1 = c.textHeight(tr(Str::HotkeyExamples), Face::Body, 12, colW),
                    hint2 = c.textHeight(tr(Str::NetworkHint), Face::Body, 12, colW);
        c.text(tr(Str::Hotkey), RectF(x, y, colW, 18), Face::BodySemi, 13, p.ink);
        c.text(tr(Str::NetworkPort), RectF(x + colW + 14, y, colW, 18), Face::BodySemi, 13, p.ink);
        y += 24;
        field(c, RectF(x, y, colW, 38), hotkeyEdit);
        field(c, RectF(x + colW + 14, y, colW, 38), portEdit);
        y += 38 + 6;
        c.text(tr(Str::HotkeyExamples), RectF(x, y, colW, hint1 + 2), Face::Body, 12, p.ink2, Left, false, true);
        c.text(tr(Str::NetworkHint), RectF(x + colW + 14, y, colW, hint2 + 2), Face::Body, 12, p.ink2, Left, false, true);
        return y + std::max(hint1, hint2);
    }

    std::string resolvedLanguage() const { return resolveLanguage(working.language); }

    float pageGeneral(Canvas& c) {
        const Palette& p = pal();
        const float x = left(), w = std::min(width(), 680.f);
        float y = top();
        y = pageTitle(c, tr(Str::TabGeneral), x, y, w) + 18;
        c.text(tr(Str::Language), RectF(x, y, w, 18), Face::BodySemi, 13, p.ink);
        y += 28;
        const float previewW = 240, cardsW = w - previewW - 16;
        const Str names[3] = {Str::LanguageAuto, Str::LanguageEnglish, Str::LanguageGerman};
        const char* codes[3] = {"auto", "en", "de"};
        float cy = y;
        for (int i = 0; i < 3; ++i) {
            std::wstring sub = i == 0 ? fmt(Str::LanguageCurrently, tr(resolveLanguage("auto") == "de" ? Str::LanguageGerman : Str::LanguageEnglish))
                                      : std::wstring(trLang(Str::LanguageSub, i == 2));
            radioCard(c, RectF(x, cy, cardsW, 50), tr(names[i]), sub, working.language == codes[i], ID_LANG + i);
            cy += 58;
        }
        const float hintH = c.textHeight(tr(Str::LanguageHint), Face::Body, 12, cardsW);
        c.text(tr(Str::LanguageHint), RectF(x, cy, cardsW, hintH + 2), Face::Body, 12, p.ink2, Left, false, true);
        cy += hintH;
        // Pi touch menu preview in the language the Pi will use
        {
            const bool german = resolvedLanguage() == "de";
            RectF box(x + cardsW + 16, y, previewW, previewW * 0.6f);
            c.fillRound(box, 6, Color(255, 0x0a, 0x07, 0x16));
            c.strokeRound(box, 6, Color(255, 0x2a, 0x20, 0x48), 2);
            const Str tiles[4] = {Str::TouchView, Str::TouchBrightness, Str::TouchStatus, Str::TouchRestart};
            const float tw = (box.Width - 20 - 6) / 2, th = (box.Height - 20 - 6) / 2;
            for (int i = 0; i < 4; ++i) {
                RectF t(box.X + 10 + float(i % 2) * (tw + 6), box.Y + 10 + float(i / 2) * (th + 6), tw, th);
                c.fillRound(t, 4, Color(255, 0x1c, 0x15, 0x34));
                c.strokeRound(t, 4, Color(115, 0x2e, 0xf2, 0xff));
                c.text(trLang(tiles[i], german), t, Face::BodySemi, 11, Color(255, 0xe9, 0xe3, 0xff), Center);
            }
            c.text(tr(Str::TouchMenuPreview), RectF(box.X, box.Y + box.Height + 6, box.Width, 16), Face::Body, 11, p.ink2);
            cy = std::max(cy, box.Y + box.Height + 22);
        }
        y = std::max(cy, y + 3 * 58) + 16;
        c.line(x, y + 0.5f, x + w, y + 0.5f, p.line);
        y += 18;
        // autostart row
        {
            const float rowH = 44;
            RectF row(x, y, w, rowH);
            c.text(tr(Str::Autostart), RectF(x, y, w - 70, 22), Face::BodySemi, 14, p.ink);
            c.text(tr(Str::AutostartHint), RectF(x, y + 23, w - 70, 18), Face::Body, 12, p.ink2);
            toggle(c, RectF(x + w - 40, y + 12, 40, 20), working.autostart);
            add(row, ID_AUTOSTART);
            y += rowH + 18;
        }
        c.line(x, y + 0.5f, x + w, y + 0.5f, p.line);
        y += 18;
        c.text(tr(Str::SettingsFile), RectF(x, y, w, 20), Face::BodySemi, 14, p.ink);
        y += 28;
        c.text(settingsPath(), RectF(x, y, w, 18), Face::Mono, 12, p.ink2);
        y += 26;
        const float iniH = c.textHeight(tr(Str::IniHint), Face::Body, 12, w);
        c.text(tr(Str::IniHint), RectF(x, y, w, iniH + 2), Face::Body, 12, p.ink2, Left, false, true);
        y += iniH + 10;
        const float b1 = c.textWidth(tr(Str::OpenIni), Face::BodySemi, 13) + 32, b2 = c.textWidth(tr(Str::MenuReloadIni), Face::BodySemi, 13) + 32;
        button(c, RectF(x, y, b1, 34), tr(Str::OpenIni), ID_OPEN_INI);
        button(c, RectF(x + b1 + 8, y, b2, 34), tr(Str::MenuReloadIni), ID_RELOAD_INI);
        return y + 34;
    }

    // ---- logs ---------------------------------------------------------------------------------------

    std::vector<LogEntry> filteredLogs(int* counts) {
        const auto all = eventLog().snapshot();
        std::vector<LogEntry> out;
        counts[0] = int(all.size());
        counts[1] = counts[2] = counts[3] = 0;
        for (const auto& e : all) {
            const int level = e.level == LogLevel::Info ? 1 : e.level == LogLevel::Warn ? 2 : 3;
            ++counts[level];
            if (logFilter == 0 || logFilter == level) out.push_back(e);
        }
        return out;
    }

    float pageLogs(Canvas& c) {
        const Palette& p = pal();
        const float x = left(), w = width();
        float y = top();
        int counts[4];
        const auto rows = filteredLogs(counts);
        const Str names[4] = {Str::LogFilterAll, Str::LogFilterInfo, Str::LogFilterWarning, Str::LogFilterError};
        const float copyW = c.textWidth(tr(Str::LogCopy), Face::BodySemi, 12) + 26,
                    openW = c.textWidth(tr(Str::LogOpenShutdown), Face::BodySemi, 12) + 26;
        float chipsW = 0;
        std::wstring labels[4];
        float widths[4];
        for (int i = 0; i < 4; ++i) {
            labels[i] = std::wstring(tr(names[i])) + L" " + std::to_wstring(counts[i]);
            widths[i] = c.textWidth(labels[i], Face::BodySemi, 12) + 26;
            chipsW += widths[i] + 6;
        }
        const float titleW = c.textWidth(tr(Str::TabLogs), Face::Display, 22) + 24;
        const bool oneRow = titleW + chipsW + copyW + openW + 30 <= w;
        heading(c, tr(Str::TabLogs), RectF(x, y, titleW, 28));
        float bx = oneRow ? x + w - openW : x + w - openW;
        float by = oneRow ? y : y + 40;
        button(c, RectF(bx, by, openW, 30), tr(Str::LogOpenShutdown), ID_OPEN_LOG);
        bx -= 8 + copyW;
        button(c, RectF(bx, by, copyW, 30), tr(Str::LogCopy), ID_COPY);
        float cx = oneRow ? bx - 16 - chipsW + 6 : x;
        for (int i = 0; i < 4; ++i) {
            button(c, RectF(cx, by, widths[i], 30), labels[i], ID_LOGF + i, logFilter == i ? Button::ChipOn : Button::Chip);
            cx += widths[i] + 6;
        }
        y += oneRow ? 30 + 14 : 40 + 30 + 14;
        if (rows.empty()) {
            c.text(tr(Str::LogEmpty), RectF(x, y + 6, w, 22), Face::Body, 13, p.ink2);
            return y + 34;
        }
        const float msgW = w - 28 - 140 - 86;
        float total = 0;
        std::vector<float> heights;
        for (const auto& e : rows) {
            const float h = std::max(20.f, c.textHeight(e.message, Face::Mono, 12, msgW)) + 16;
            heights.push_back(h);
            total += h;
        }
        RectF table(x, y, w, total);
        c.fillRound(table, 8, p.pbg);
        float ry = y;
        for (size_t i = 0; i < rows.size(); ++i) {
            const LogEntry& e = rows[i];
            const float h = heights[i];
            c.text(clockText(e.time, true), RectF(x + 14, ry + 8, 72, 20), Face::Mono, 12, p.ink2);
            c.text(levelName(e.level), RectF(x + 14 + 82, ry + 8, 58, 20), Face::MonoMedium, 12, levelColor(e.level));
            c.text(fromUtf8(e.source), RectF(x + 14 + 150, ry + 8, 76, 20), Face::Mono, 12, p.ink2);
            c.text(e.message, RectF(x + 14 + 236, ry + 8, msgW + 0, h - 12), Face::Mono, 12, p.ink, Left, false, true);
            if (i + 1 < rows.size()) c.line(x, ry + h - 0.5f, x + w, ry + h - 0.5f, p.line);
            ry += h;
        }
        c.strokeRound(table, 8, p.line);
        return y + total;
    }

    // ---- actions --------------------------------------------------------------------------------------

    void click(int id) override {
        if (id >= ID_NAV && id < int(ID_NAV) + int(SETTINGS_PAGES)) { gotoPage(id - ID_NAV); return; }
        if (id >= ID_MEDIA && id < ID_MEDIA + 1000) {
            selected = id - ID_MEDIA;
            blocked = false;
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        if (id >= ID_GESTURE && id < ID_GESTURE + GESTURE_COUNT) { openGestureMenu(id - ID_GESTURE); return; }
        if (id >= ID_ACT && id < ID_ACT + 4) {
            static const int commands[4] = {M_DEFAULT, M_RELOAD, M_REBOOT, M_SHUTDOWN};
            host.command(commands[id - ID_ACT]);
            return;
        }
        if (id >= ID_ROLE && id < ID_ROLE + 3) { assignRole(id - ID_ROLE); return; }
        if (id >= ID_LOGF && id < ID_LOGF + 4) { logFilter = id - ID_LOGF; InvalidateRect(hwnd, nullptr, FALSE); return; }
        switch (id) {
        case ID_SAVE: save(); break;
        case ID_CANCEL: DestroyWindow(hwnd); break;
        case ID_TEST: testConnection(); break;
        case ID_TOKEN_TOGGLE:
            showToken = !showToken;
            SendMessageW(tokenEdit, EM_SETPASSWORDCHAR, showToken ? 0 : 0x2022, 0);
            InvalidateRect(tokenEdit, nullptr, TRUE);
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        case ID_HELP: tokenHelpDialog(hwnd); break;
        case ID_SETUP: gotoPage(PAGE_CONNECTION); break;
        case ID_GOMEDIA: gotoPage(PAGE_MEDIA); break;
        case ID_GOLOGS: gotoPage(PAGE_LOGS); break;
        case ID_RETRO: working.retroArchNetworkControl = false; InvalidateRect(hwnd, nullptr, FALSE); break;
        case ID_RETRO + 1: working.retroArchNetworkControl = true; InvalidateRect(hwnd, nullptr, FALSE); break;
        case ID_LANG: applyLanguage("auto"); break;
        case ID_LANG + 1: applyLanguage("en"); break;
        case ID_LANG + 2: applyLanguage("de"); break;
        case ID_AUTOSTART: working.autostart = !working.autostart; InvalidateRect(hwnd, nullptr, FALSE); break;
        case ID_OPEN_INI: host.command(M_OPEN_INI); break;
        case ID_RELOAD_INI: host.command(M_LOAD_INI); break;
        case ID_VIEW_GRID: grid = true; InvalidateRect(hwnd, nullptr, FALSE); break;
        case ID_VIEW_LIST: grid = false; InvalidateRect(hwnd, nullptr, FALSE); break;
        case ID_ADD: addMedia(); break;
        case ID_PREVIEW: previewMedia(); break;
        case ID_REMOVE: removeMedia(); break;
        case ID_RETRY:
            if (uploadError.shown) {
                const std::wstring name = uploadError.name;
                for (size_t i = 0; i < entries.size(); ++i)
                    if (entries[i].name == name) startUpload(int(i), uploadError.role);
            }
            break;
        case ID_DISMISS: uploadError.shown = false; InvalidateRect(hwnd, nullptr, FALSE); break;
        case ID_COPY: copyLogs(); break;
        case ID_OPEN_LOG: {
            const std::wstring path = dataDirectory() + L"\\shutdown.log";
            ShellExecuteW(hwnd, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        }
        }
    }

    void openGestureMenu(int index) {
        const RectF r = gestureBox[index];
        POINT tl{LONG(r.X * scale), LONG((r.Y + r.Height) * scale)}, br{LONG((r.X + r.Width) * scale), LONG((r.Y + r.Height) * scale)};
        ClientToScreen(hwnd, &tl);
        ClientToScreen(hwnd, &br);
        std::vector<std::wstring> items;
        for (int a = 0; a < ACTION_COUNT; ++a) items.push_back(trAt(Str::ActionNone, a));
        std::string current = index == 0 ? "touch_menu" : "none";
        auto it = working.gestures.find(GESTURE_IDS[index]);
        if (it != working.gestures.end()) current = it->second;
        int selectedAction = 0;
        for (int j = 0; j < ACTION_COUNT; ++j) if (current == ACTION_IDS[j]) selectedAction = j;
        showDropdown(hwnd, RECT{tl.x, tl.y, br.x, br.y}, items, selectedAction, ID_GESTURE + index);
    }

    void applyLanguage(const std::string& choice) {
        working.language = choice;
        try {
            Settings now = host.sync().settings();
            now.language = choice;
            saveSettings(now);
            setUiLanguage(resolveLanguage(choice));
            host.sync().settingsChanged(now);
            host.languageChanged();
            SetWindowTextW(hwnd, tr(Str::WindowTitle));
            const PiStatus& st = host.status();
            toast(st.connected ? tr(Str::ToastLanguageApplied) : tr(Str::ToastLanguageOffline), st.connected ? ToastKind::Ok : ToastKind::Warn);
        } catch (const std::exception& error) {
            toast(errorText(error), ToastKind::Error);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    void testConnection() {
        if (testing) return;
        Settings probe = host.sync().settings();
        probe.piUrl = readText(urlEdit);
        probe.token = readText(tokenEdit);
        messageShown = true;
        if (!probe.configured()) {
            messageText = tr(Str::NotConfigured);
            messageColor = pal().yel;
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        messageShown = false;
        testing = true;
        if (tester.joinable()) tester.join();
        HWND target = hwnd;
        tester = std::thread([probe, target] {
            auto* result = new TestResult{false, false, 0, L""};
            try {
                const auto started = std::chrono::steady_clock::now();
                piRequest(probe, L"GET", L"/v1/status");
                result->ok = true;
                result->ms = int(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());
            } catch (const HttpError& error) {
                result->rejected = error.status == 401 || error.status == 403;
                result->message = errorText(error);
            } catch (const std::exception& error) {
                result->message = errorText(error);
            }
            if (!PostMessageW(target, WM_TEST_DONE, 0, reinterpret_cast<LPARAM>(result))) delete result;
        });
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    void save() {
        Settings next = host.sync().settings();
        next.piUrl = readText(urlEdit);
        next.token = readText(tokenEdit);
        next.hotkey = readText(hotkeyEdit);
        next.retroArchNetworkControl = working.retroArchNetworkControl;
        next.autostart = working.autostart;
        next.language = working.language;
        try { next.retroArchNetworkPort = std::stoi(readText(portEdit)); }
        catch (...) { gotoPage(PAGE_RETROARCH); toast(tr(Str::InvalidPort), ToastKind::Error); return; }
        if (next.retroArchNetworkPort < 1 || next.retroArchNetworkPort > 65535) {
            gotoPage(PAGE_RETROARCH);
            toast(tr(Str::PortRange), ToastKind::Error);
            return;
        }
        bool menuReachable = false;
        for (int i = 0; i < GESTURE_COUNT; ++i) {
            std::string action = i == 0 ? "touch_menu" : "none";
            auto it = working.gestures.find(GESTURE_IDS[i]);
            if (it != working.gestures.end()) action = it->second;
            next.gestures[GESTURE_IDS[i]] = action;
            menuReachable = menuReachable || action == "touch_menu";
        }
        if (!next.configured()) { gotoPage(PAGE_CONNECTION); toast(tr(Str::NotConfigured), ToastKind::Warn); return; }
        if (!validHotkey(next.hotkey)) { gotoPage(PAGE_RETROARCH); toast(tr(Str::InvalidHotkey), ToastKind::Error); return; }
        if (!menuReachable) {
            gotoPage(PAGE_GESTURES);
            if (!confirmDialog(hwnd, tr(Str::TabGestures), tr(Str::TouchMenuUnassigned), tr(Str::Save), false)) return;
        }
        try {
            setAutostart(next.autostart);
            saveSettings(next);
            setUiLanguage(resolveLanguage(next.language));
            host.sync().settingsChanged(next);
            host.languageChanged();
            working = next;
            logEvent(LogLevel::Info, "conn", tr(Str::LogSettingsSaved));
            toast(tr(Str::ToastSettingsSaved), ToastKind::Ok);
        } catch (const std::exception& error) {
            toast(errorText(error), ToastKind::Error);
        }
    }

    // media actions
    void assignRole(int role) {
        if (selected < 0 || size_t(selected) >= entries.size() || upload.active) return;
        const Look l = look();
        if (!l.configured) { toast(tr(Str::ToastSetupFirst), ToastKind::Error); return; }
        if (!l.connected) { toast(tr(Str::ToastPiUnreachable), ToastKind::Error); return; }
        startUpload(selected, role);
    }

    void startUpload(int index, int role) {
        if (index < 0 || size_t(index) >= entries.size() || upload.active) return;
        const Entry e = entries[size_t(index)];
        const Settings settings = host.sync().settings();
        upload = {true, e.name, role, 0};
        uploadError.shown = false;
        if (uploader.joinable()) uploader.join();
        HWND target = hwnd;
        uploader = std::thread([=] {
            auto* result = new UploadResult{false, e.name, L"", role};
            try {
                if (e.size > 20 * 1024 * 1024) throw std::runtime_error("File exceeds 20 MB upload limit");
                if (role == 1 && !(e.ext == L".png" || e.ext == L".jpg" || e.ext == L".jpeg"))
                    throw std::runtime_error("Boot splash must be a PNG or JPEG image");
                const std::string body = readFile(e.path);
                piRequest(settings, L"POST", ROLES[role].endpoint, body, L"application/octet-stream",
                          L"X-File-Name: upload" + e.ext + L"\r\n", 8000, [target](size_t sent, size_t total) {
                              PostMessageW(target, WM_UPLOAD_PROGRESS, WPARAM(total ? sent * 100 / total : 0), 0);
                          });
                result->ok = true;
            } catch (const std::exception& error) {
                result->message = piErrorText(error);
            }
            if (!PostMessageW(target, WM_UPLOAD_DONE, 0, reinterpret_cast<LPARAM>(result))) delete result;
        });
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    void finishUpload(const UploadResult& result) {
        upload.active = false;
        if (uploader.joinable()) uploader.join();
        if (result.ok) {
            try {
                writeFile(dataDirectory() + L"\\" + ROLES[result.role].marker, toUtf8(result.name));
            } catch (const std::exception& error) {
                toast(errorText(error), ToastKind::Error);
            }
            logEvent(LogLevel::Info, "media", fmt(Str::LogMediaStored, result.name.c_str(), tr(ROLES[result.role].name)));
            refreshEntries(result.name);
            toast(tr(ROLES[result.role].saved), ToastKind::Ok);
        } else {
            uploadError = {true, result.name, result.role, result.message};
            logEvent(LogLevel::Error, "media", fmt(Str::LogMediaFailed, result.name.c_str(), result.message.c_str()));
        }
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    void addMedia() {
        wchar_t chosen[32768]{};
        std::wstring filter = tr(Str::FilterSupported);
        filter.push_back(L'\0');
        filter += L"*.jpg;*.jpeg;*.png;*.gif;*.webp;*.mp4";
        filter.push_back(L'\0');
        filter += tr(Str::FilterAll);
        filter.push_back(L'\0');
        filter += L"*.*";
        filter.push_back(L'\0');
        filter.push_back(L'\0');
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = hwnd;
        dialog.lpstrFilter = filter.c_str();
        dialog.lpstrFile = chosen;
        dialog.nMaxFile = 32768;
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (!GetOpenFileNameW(&dialog)) return;
        try {
            fs::path source(chosen);
            const auto length = fs::file_size(source);
            if (length > 20 * 1024 * 1024) throw std::runtime_error("File exceeds 20 MB upload limit");
            const std::wstring ext = lower(source.extension().wstring());
            if (ext != L".jpg" && ext != L".jpeg" && ext != L".png" && ext != L".gif" && ext != L".webp" && ext != L".mp4")
                throw std::runtime_error("Unsupported file type");
            fs::path target = fs::path(mediaDirectory()) / source.filename();
            if (fs::exists(target)) target = fs::path(mediaDirectory()) / (source.stem().wstring() + L"-" + std::to_wstring(GetTickCount64()) + ext);
            fs::copy_file(source, target);
            refreshEntries(target.filename().wstring());
            logEvent(LogLevel::Info, "media", fmt(Str::LogMediaImported, target.filename().c_str()));
            toast(fmt(Str::ToastImported, target.filename().c_str()), ToastKind::Info);
        } catch (const std::exception& error) {
            toast(errorText(error), ToastKind::Error);
        }
    }
    void previewMedia() {
        if (selected < 0 || size_t(selected) >= entries.size()) return;
        ShellExecuteW(hwnd, L"open", entries[size_t(selected)].path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    void removeMedia() {
        if (selected < 0 || size_t(selected) >= entries.size()) return;
        const Entry e = entries[size_t(selected)];
        for (int r = 0; r < 3; ++r)
            if (roleOf(e, r)) { blocked = true; InvalidateRect(hwnd, nullptr, FALSE); return; }
        if (!DeleteFileW(e.path.c_str())) { toast(tr(Str::RemoveFailed), ToastKind::Error); return; }
        logEvent(LogLevel::Info, "media", fmt(Str::LogMediaRemoved, e.name.c_str()));
        refreshEntries(L"\x01");
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    void copyLogs() {
        int counts[4];
        const auto rows = filteredLogs(counts);
        std::wstring text;
        for (const auto& e : rows)
            text += clockText(e.time, true) + L" " + levelName(e.level) + L" " + fromUtf8(e.source) + L" " + e.message + L"\r\n";
        if (OpenClipboard(hwnd)) {
            EmptyClipboard();
            const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
            if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
                memcpy(GlobalLock(memory), text.c_str(), bytes);
                GlobalUnlock(memory);
                SetClipboardData(CF_UNICODETEXT, memory);
            }
            CloseClipboard();
            toast(tr(Str::ToastLogCopied), ToastKind::Info);
        }
    }
};

SettingsUi* SettingsUi::instance = nullptr;

}  // namespace

void openSettings(UiHost& host, int page) {
    if (SettingsUi::instance) {
        SendMessageW(SettingsUi::instance->hwnd, WM_GOTO_PAGE, WPARAM(page), 0);
        if (IsIconic(SettingsUi::instance->hwnd)) ShowWindow(SettingsUi::instance->hwnd, SW_RESTORE);
        SetForegroundWindow(SettingsUi::instance->hwnd);
        return;
    }
    static bool registered = false;
    if (!registered) {
        WNDCLASSW klass{};
        klass.lpfnWndProc = SettingsUi::proc;
        klass.hInstance = GetModuleHandleW(nullptr);
        klass.lpszClassName = SETTINGS_CLASS;
        klass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        klass.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101));
        registered = RegisterClassW(&klass) != 0;
    }
    auto* window = new SettingsUi(host, std::clamp(page, 0, SETTINGS_PAGES - 1));
    SettingsUi::instance = window;
    HWND created = CreateWindowExW(WS_EX_CONTROLPARENT, SETTINGS_CLASS, tr(Str::WindowTitle),
                                   WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 400, 300,
                                   nullptr, nullptr, GetModuleHandleW(nullptr), window);
    if (!created) {
        SettingsUi::instance = nullptr;
        delete window;
        return;
    }
    ShowWindow(created, SW_SHOW);
    SetForegroundWindow(created);
}

HWND settingsWindow() { return SettingsUi::instance ? SettingsUi::instance->hwnd : nullptr; }

void settingsToast(const std::wstring& text, ToastKind kind) {
    if (!SettingsUi::instance) return;
    auto* item = new std::pair<std::wstring, ToastKind>(text, kind);
    if (!PostMessageW(SettingsUi::instance->hwnd, WM_TOAST, 0, reinterpret_cast<LPARAM>(item))) delete item;
}

void settingsRefresh() {
    if (SettingsUi::instance) PostMessageW(SettingsUi::instance->hwnd, WM_REFRESH, 0, 0);
}

}  // namespace ui
