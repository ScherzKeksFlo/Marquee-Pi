#include "popups.hpp"
#include "strings.hpp"
#include "thumb_cache.hpp"
#include <windowsx.h>
#include <filesystem>

namespace ui {
namespace {

constexpr wchar_t POPUP_CLASS[] = L"MarqueePiPopup";

// Common window plumbing: a borderless popup that owns its Surface, optional deactivate-to-close.
class Popup : public Surface {
public:
    bool closeOnDeactivate = false;
    bool deleteOnDestroy = false;
    bool done = false;

    static LRESULT CALLBACK proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<Popup*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Popup*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            self->hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(hwnd, message, wp, lp);
        LRESULT result = 0;
        if (self->handle(message, wp, lp, result)) return result;
        switch (message) {
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE && self->closeOnDeactivate) DestroyWindow(hwnd);
            return 0;
        case WM_THUMB_READY:
            ThumbCache::instance().drain();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_DESTROY:
            ThumbCache::instance().unsubscribe(hwnd);
            return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) { self->cancel(); return 0; }
            if (wp == VK_RETURN) { self->accept(); return 0; }
            break;
        case WM_NCDESTROY: {
            self->done = true;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            if (self->deleteOnDestroy) delete self;
            return 0;
        }
        }
        return DefWindowProcW(hwnd, message, wp, lp);
    }

    virtual void cancel() { DestroyWindow(hwnd); }
    virtual void accept() {}

    // scaleHint > 0 forces the DPI scale used for the initial size; otherwise it follows the owner window.
    bool create(HWND owner, int x, int y, int widthDip, int heightDip, bool topmost, float scaleHint) {
        static bool registered = false;
        if (!registered) {
            WNDCLASSW klass{};
            klass.lpfnWndProc = proc;
            klass.hInstance = GetModuleHandleW(nullptr);
            klass.lpszClassName = POPUP_CLASS;
            klass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
            klass.style = CS_DROPSHADOW;
            registered = RegisterClassW(&klass) != 0;
        }
        scale = scaleHint > 0.f ? scaleHint : owner ? ownerDpiScale(owner) : 1.f;
        const int w = int(widthDip * scale + 0.5f), h = int(heightDip * scale + 0.5f);
        hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | (topmost ? WS_EX_TOPMOST : 0), POPUP_CLASS, L"", WS_POPUP,
                               x, y, w, h, owner, nullptr, GetModuleHandleW(nullptr), this);
        if (!hwnd) return false;
        const float used = scale;
        initScale();  // the monitor the window really landed on decides
        if (std::abs(scale - used) > 0.01f)
            SetWindowPos(hwnd, nullptr, x, y, int(widthDip * scale + 0.5f), int(heightDip * scale + 0.5f),
                         SWP_NOZORDER | SWP_NOACTIVATE);
        applyWindowChrome(hwnd, true);
        return true;
    }

    static float scaleForPoint(POINT point) {
        typedef HRESULT(WINAPI * Fn)(HMONITOR, int, UINT*, UINT*);
        static const auto getDpi = reinterpret_cast<Fn>(
            reinterpret_cast<void*>(GetProcAddress(LoadLibraryW(L"shcore.dll"), "GetDpiForMonitor")));
        UINT dpiX = 96, dpiY = 96;
        if (getDpi) getDpi(MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST), 0, &dpiX, &dpiY);  // MDT_EFFECTIVE_DPI
        return float(dpiX ? dpiX : 96) / 96.f;
    }
    // Scale for a dialog: the owner window, or the monitor under the cursor when there is none.
    static float scaleForDialog(HWND owner) {
        if (owner) return ownerDpiScale(owner);
        POINT cursor{};
        GetCursorPos(&cursor);
        return scaleForPoint(cursor);
    }

    static float ownerDpiScale(HWND owner) {
        typedef UINT(WINAPI * Fn)(HWND);
        static const auto getDpi = reinterpret_cast<Fn>(
            reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
        const UINT dpi = getDpi ? getDpi(owner) : 96;
        return float(dpi ? dpi : 96) / 96.f;
    }
};

void runModal(Popup& dialog, HWND owner) {
    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(dialog.hwnd, SW_SHOW);
    SetForegroundWindow(dialog.hwnd);
    SetFocus(dialog.hwnd);
    MSG message;
    while (!dialog.done && GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (owner) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
}

void centerOn(HWND owner, int widthPx, int heightPx, int& x, int& y) {
    RECT area{};
    if (owner && IsWindowVisible(owner)) GetWindowRect(owner, &area);
    else {
        MONITORINFO info{};
    info.cbSize = sizeof(info);
        GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &info);
        area = info.rcWork;
    }
    x = area.left + (area.right - area.left - widthPx) / 2;
    y = area.top + (area.bottom - area.top - heightPx) / 2;
}

// ---- dropdown ------------------------------------------------------------------------------

class Dropdown : public Popup {
public:
    HWND target = nullptr;
    int id = 0, selected = -1;
    std::vector<std::wstring> items;
    static constexpr float ROW = 32.f;

    void paint(Canvas& c, float w, float h) override {
        const Palette& p = pal();
        c.fillRect(RectF(0, 0, w, h), p.pbg);
        for (size_t i = 0; i < items.size(); ++i) {
            RectF row(4, 4 + float(i) * ROW, w - 8, ROW);
            const bool on = int(i) == selected, over = hovered(int(i));
            if (over) c.fillRound(row, 5, p.ibg);
            c.text(items[i], RectF(row.X + 10, row.Y, row.Width - 26, row.Height), on ? Face::BodyBold : Face::BodySemi,
                   13, on ? p.pink : p.ink);
            if (on) c.circle(row.X + row.Width - 12, row.Y + row.Height / 2, 3.f, p.pink);
            add(row, int(i));
        }
    }
    void click(int index) override {
        PostMessageW(target, WM_UI_DROPDOWN, WPARAM(id), LPARAM(index));
        DestroyWindow(hwnd);
    }
};

// ---- confirm dialog ---------------------------------------------------------------------------

class Confirm : public Popup {
public:
    std::wstring title, text, ok;
    bool destructive = true, accepted = false;
    float textHeight = 0;

    void paint(Canvas& c, float w, float h) override {
        const Palette& p = pal();
        const Color accent = destructive ? p.red : p.cyan;
        c.fillRect(RectF(0, 0, w, h), p.wbg);
        c.text(title, RectF(22, 22, w - 44, 22), Face::Display, 16, accent);
        c.text(text, RectF(22, 56, w - 44, textHeight), Face::Body, 14, p.ink, Left, false, true);
        const float y = h - 22 - 34;
        const float okW = std::max(96.f, c.textWidth(ok, Face::BodyBold, 13) + 36);
        const float cancelW = std::max(88.f, c.textWidth(tr(Str::Cancel), Face::BodySemi, 13) + 36);
        RectF okRect(w - 22 - okW, y, okW, 34), cancelRect(okRect.X - 8 - cancelW, y, cancelW, 34);
        button(c, cancelRect, tr(Str::Cancel), 2);
        c.fillRound(okRect, 6, hovered(1) ? withAlpha(accent, 220) : accent);
        c.text(ok, okRect, Face::BodyBold, 13, destructive ? Color(255, 255, 255, 255) : p.onacc, Center);
        add(okRect, 1);
    }
    void click(int id) override {
        accepted = id == 1;
        DestroyWindow(hwnd);
    }
    void accept() override { accepted = true; DestroyWindow(hwnd); }
};

// ---- token help --------------------------------------------------------------------------------

class TokenHelp : public Popup {
public:
    struct Step { Str text; const wchar_t* code; };
    std::vector<Step> steps{{Str::TokenStep1, nullptr},
                            {Str::TokenStep2, L"openssl rand -hex 32"},
                            {Str::TokenStep3, nullptr},
                            {Str::TokenStep4, L"sudo systemctl restart marquee-pi-api.service"},
                            {Str::TokenStep5, nullptr}};
    static constexpr float WIDTH = 460.f, TEXT_X = 22.f + 26.f;

    // Shared by the size calculation and painting: returns the total height.
    float layout(Canvas* c, float w) {
        const Palette& p = pal();
        const float textW = w - TEXT_X - 22.f;
        float y = 22;
        if (c) c->text(tr(Str::TokenHelpTitle), RectF(22, y, w - 44, 22), Face::Display, 16, p.cyan);
        y += 22 + 14;
        Gdiplus::Bitmap probe(1, 1);
        Gdiplus::Graphics probeGraphics(&probe);
        Canvas measure(probeGraphics);
        Canvas& m = c ? *c : measure;
        int n = 1;
        for (const Step& step : steps) {
            const std::wstring text = tr(step.text);
            const float th = m.textHeight(text, Face::Body, 13, textW) + 2;
            if (c) {
                c->text(std::to_wstring(n), RectF(22, y, 22, 18), Face::Display, 13, p.pink, Left, false);
                c->text(text, RectF(TEXT_X, y, textW, th), Face::Body, 13, p.ink, Left, false, true);
            }
            y += th;
            if (step.code) {
                y += 6;
                if (c) {
                    RectF box(TEXT_X, y, textW, 30);
                    c->fillRound(box, 5, p.ibg);
                    c->text(step.code, RectF(box.X + 10, box.Y, box.Width - 20, box.Height), Face::Mono, 12, p.grn);
                }
                y += 30;
            }
            y += 14;
            ++n;
        }
        return y + 34 + 22 - 14;
    }
    void paint(Canvas& c, float w, float h) override {
        c.fillRect(RectF(0, 0, w, h), pal().wbg);
        layout(&c, w);
        RectF ok(w - 22 - 96, h - 22 - 34, 96, 34);
        button(c, ok, tr(Str::DialogOk), 1, Button::Primary);
    }
    void click(int) override { DestroyWindow(hwnd); }
    void accept() override { DestroyWindow(hwnd); }
};

// ---- tray flyout -------------------------------------------------------------------------------

class Flyout : public Popup {
public:
    UiHost* host = nullptr;
    static constexpr float WIDTH = 300.f, ITEM = 33.f;

    struct Item { int id; Str label; bool piAction; bool red; };
    static const std::vector<Item>& items() {
        static const std::vector<Item> list{
            {M_MEDIA, Str::MenuMedia, false, false},      {M_DEFAULT, Str::MenuDefault, true, false},
            {M_RELOAD, Str::MenuReload, true, false},     {M_REBOOT, Str::MenuReboot, true, false},
            {M_SHUTDOWN, Str::MenuShutdown, true, true},  {0, Str::Count, false, false},
            {M_SETTINGS, Str::MenuSettings, false, false}, {M_OPEN_INI, Str::MenuOpenIni, false, false},
            {M_LOAD_INI, Str::MenuReloadIni, false, false}, {0, Str::Count, false, false},
            {M_EXIT, Str::MenuExit, false, false}};
        return list;
    }
    static float height() {
        float h = 94 + 12;
        for (const Item& item : items()) h += item.id ? ITEM : 11;
        return h;
    }

    void paint(Canvas& c, float w, float h) override {
        const Palette& p = pal();
        const PiStatus& status = host->status();
        const Settings settings = host->sync().settings();
        const bool configured = settings.configured();
        c.fillRect(RectF(0, 0, w, h), p.pbg);

        const Color dot = !configured ? p.yel : status.connected ? p.grn : p.red;
        std::wstring text = !configured ? tr(Str::StatusTipNotConfigured)
                            : status.connected ? std::wstring(tr(Str::StatusConnected)) + piHost(settings)
                                               : std::wstring(tr(Str::StatusUnreachable));
        c.glow(RectF(14, 15, 9, 9), 4.5f, dot, 8, 120);
        c.circle(18.5f, 19.5f, 4.5f, dot);
        c.text(text, RectF(32, 10, w - 46, 20), Face::BodyBold, 13, p.ink);

        const std::wstring name = roleFile(L"active-media.txt");
        const float ratio = float(status.display.ratio());
        const float thumbH = std::min(38.4f, 64.f * ratio), thumbW = thumbH / ratio;
        const RectF thumb(14, 40 + (38.4f - thumbH) / 2, thumbW, thumbH);
        c.fillRound(thumb, 4, p.ibg);
        if (!name.empty()) {
            const std::wstring path = std::filesystem::path(mediaDirectory()) / name;
            c.image(ThumbCache::instance().get(path), thumb, 4);
        }
        c.text(tr(Str::DefaultMediaTitle), RectF(88, 40, w - 102, 16), Face::Body, 11, p.ink2);
        c.text(name.empty() ? L"—" : name, RectF(88, 57, w - 102, 20), Face::BodySemi, 13, p.ink);
        c.line(0, 93.5f, w, 93.5f, p.line);

        float y = 100;
        for (const Item& item : items()) {
            if (!item.id) {
                c.line(14, y + 5.5f, w - 14, y + 5.5f, p.line);
                y += 11;
                continue;
            }
            RectF row(6, y, w - 12, ITEM);
            const bool enabled = !item.piAction || status.connected;
            const bool over = enabled && hovered(item.id);
            if (over) c.fillRound(row, 5, p.ibg);
            const Color color = item.red ? p.red : p.ink;
            c.text(tr(item.label), RectF(row.X + 10, row.Y, row.Width - 20, row.Height),
                   item.id == M_MEDIA ? Face::BodySemi : Face::Body, 13, withAlpha(color, enabled ? 255 : 115));
            add(row, item.id, enabled);
            y += ITEM;
        }
    }
    void click(int id) override {
        UiHost* target = host;
        DestroyWindow(hwnd);  // this object is deleted here
        target->command(id);
    }
};

}  // namespace

void showDropdown(HWND owner, RECT anchor, const std::vector<std::wstring>& items, int selected, int id) {
    auto* menu = new Dropdown;
    menu->deleteOnDestroy = true;
    menu->closeOnDeactivate = true;
    menu->target = owner;
    menu->id = id;
    menu->items = items;
    menu->selected = selected;
    const float s = Popup::ownerDpiScale(owner);
    const int widthDip = int((anchor.right - anchor.left) / s);
    const float heightDip = 8 + float(items.size()) * Dropdown::ROW;
    if (!menu->create(owner, anchor.left, anchor.bottom + 4, widthDip, int(heightDip), true, 0.f)) {
        delete menu;
        return;
    }
    ShowWindow(menu->hwnd, SW_SHOW);
    SetForegroundWindow(menu->hwnd);
}

bool confirmDialog(HWND owner, const std::wstring& title, const std::wstring& text,
                   const std::wstring& okLabel, bool destructive) {
    Confirm dialog;
    dialog.title = title;
    dialog.text = text;
    dialog.ok = okLabel;
    dialog.destructive = destructive;
    {
        Gdiplus::Bitmap probe(1, 1);
        Gdiplus::Graphics g(&probe);
        Canvas measure(g);
        dialog.textHeight = measure.textHeight(text, Face::Body, 14, 400 - 44) + 4;
    }
    const float heightDip = 56 + dialog.textHeight + 24 + 34 + 22;
    const float s = Popup::scaleForDialog(owner);
    int x = 0, y = 0;
    centerOn(owner, int(400 * s), int(heightDip * s), x, y);
    if (!dialog.create(owner, x, y, 400, int(heightDip), false, s)) return false;
    runModal(dialog, owner);
    return dialog.accepted;
}

void tokenHelpDialog(HWND owner) {
    TokenHelp dialog;
    const float heightDip = dialog.layout(nullptr, TokenHelp::WIDTH);
    const float s = Popup::scaleForDialog(owner);
    int x = 0, y = 0;
    centerOn(owner, int(TokenHelp::WIDTH * s), int(heightDip * s), x, y);
    if (!dialog.create(owner, x, y, int(TokenHelp::WIDTH), int(heightDip), false, s)) return;
    runModal(dialog, owner);
}

void showFlyout(UiHost& host, POINT anchor) {
    auto* flyout = new Flyout;
    flyout->deleteOnDestroy = true;
    flyout->closeOnDeactivate = true;
    flyout->host = &host;
    const float s = Popup::scaleForPoint(anchor);
    const int w = int(Flyout::WIDTH * s), h = int(Flyout::height() * s);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST), &info);
    int x = std::min<int>(anchor.x - w / 2, info.rcWork.right - w - 8);
    x = std::max<int>(x, info.rcWork.left + 8);
    int y = anchor.y - h - 10;
    if (y < info.rcWork.top + 8) y = std::min<int>(anchor.y + 10, info.rcWork.bottom - h - 8);
    if (!flyout->create(nullptr, x, y, int(Flyout::WIDTH), int(Flyout::height()), true, s)) {
        delete flyout;
        return;
    }
    ThumbCache::instance().subscribe(flyout->hwnd);
    ShowWindow(flyout->hwnd, SW_SHOW);
    SetForegroundWindow(flyout->hwnd);
}

}  // namespace ui
