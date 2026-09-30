#include "ui_kit.hpp"
#include <windowsx.h>
#include <dwmapi.h>
#include <map>
#include <memory>
#include <vector>

namespace ui {
namespace {

Palette darkPalette() {
    return {true, Color(255, 0x0d, 0x0a, 0x1a), Color(255, 0x15, 0x10, 0x28), Color(255, 0x1d, 0x16, 0x36),
            Color(51, 180, 140, 255), Color(255, 0xef, 0xea, 0xff), Color(255, 0xb3, 0xa7, 0xd3),
            Color(255, 0xff, 0x3f, 0xd8), Color(255, 0x2e, 0xf2, 0xff), Color(255, 0xff, 0xe4, 0x5c),
            Color(255, 0x45, 0xff, 0x9a), Color(255, 0xff, 0x5a, 0x78), Color(255, 0x1a, 0x00, 0x16)};
}
Palette lightPalette() {
    return {false, Color(255, 0xfb, 0xf8, 0xff), Color(255, 0xff, 0xff, 0xff), Color(255, 0xf2, 0xec, 0xfc),
            Color(41, 70, 30, 130), Color(255, 0x1b, 0x12, 0x30), Color(255, 0x54, 0x46, 0x6f),
            Color(255, 0xb8, 0x00, 0x8f), Color(255, 0x00, 0x6f, 0x86), Color(255, 0x7d, 0x5f, 0x00),
            Color(255, 0x07, 0x78, 0x4a), Color(255, 0xc0, 0x12, 0x3a), Color(255, 0xff, 0xff, 0xff)};
}
Palette current = darkPalette();

ULONG_PTR gdiplusToken = 0;
std::unique_ptr<Gdiplus::PrivateFontCollection> collection;
std::vector<std::wstring> gdiFontFiles;
struct FaceSpec { const wchar_t* family; int style; const wchar_t* fallback; };
FaceSpec spec(Face f) {
    switch (f) {
    case Face::Body: return {L"Chakra Petch", Gdiplus::FontStyleRegular, L"Segoe UI"};
    case Face::BodySemi: return {L"Chakra Petch SemiBold", Gdiplus::FontStyleRegular, L"Segoe UI Semibold"};
    case Face::BodyBold: return {L"Chakra Petch", Gdiplus::FontStyleBold, L"Segoe UI"};
    case Face::Display: return {L"Silkscreen", Gdiplus::FontStyleRegular, L"Consolas"};
    case Face::DisplayBold: return {L"Silkscreen", Gdiplus::FontStyleBold, L"Consolas"};
    case Face::Mono: return {L"IBM Plex Mono", Gdiplus::FontStyleRegular, L"Consolas"};
    case Face::MonoMedium: return {L"IBM Plex Mono Medium", Gdiplus::FontStyleRegular, L"Consolas"};
    }
    return {L"Segoe UI", Gdiplus::FontStyleRegular, L"Segoe UI"};
}

// Resource ids of the bundled TTFs (see resources.rc).
const int FONT_RESOURCES[] = {201, 202, 203, 204, 205, 206, 207, 208};

bool appsUseDarkTheme() {
    DWORD value = 0, size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS) return true;
    return value == 0;
}

std::map<std::pair<int, int>, std::unique_ptr<Gdiplus::Font>>& fontCache() {
    static std::map<std::pair<int, int>, std::unique_ptr<Gdiplus::Font>> cache;
    return cache;
}
std::map<std::pair<int, int>, HFONT>& gdiCache() {
    static std::map<std::pair<int, int>, HFONT> cache;
    return cache;
}

}  // namespace

const Palette& pal() { return current; }
bool isDark() { return current.dark; }
void setTheme(const std::string& setting) {
    const bool dark = setting == "light" ? false : setting == "auto" ? appsUseDarkTheme() : true;
    current = dark ? darkPalette() : lightPalette();
}
Color withAlpha(Color c, int alpha) { return Color(BYTE(std::clamp(alpha, 0, 255)), c.GetR(), c.GetG(), c.GetB()); }

void startup(HINSTANCE instance) {
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&gdiplusToken, &input, nullptr);
    collection = std::make_unique<Gdiplus::PrivateFontCollection>();
    for (int id : FONT_RESOURCES) {
        HRSRC found = FindResourceW(instance, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));
        if (!found) continue;
        HGLOBAL loaded = LoadResource(instance, found);
        const void* data = loaded ? LockResource(loaded) : nullptr;
        const DWORD size = SizeofResource(instance, found);
        if (!data || !size) continue;
        collection->AddMemoryFont(data, INT(size));  // the resource stays mapped for the whole process
        DWORD count = 0;
        AddFontMemResourceEx(const_cast<void*>(data), size, nullptr, &count);  // same faces for EDIT controls
    }
}
void shutdown() {
    fontCache().clear();
    for (auto& pair : gdiCache()) DeleteObject(pair.second);
    gdiCache().clear();
    collection.reset();
    if (gdiplusToken) Gdiplus::GdiplusShutdown(gdiplusToken);
    gdiplusToken = 0;
}

const Gdiplus::Font& font(Face face, float pixels) {
    const std::pair<int, int> key{int(face), int(pixels * 100)};
    auto& cache = fontCache();
    auto it = cache.find(key);
    if (it != cache.end()) return *it->second;
    const FaceSpec s = spec(face);
    std::unique_ptr<Gdiplus::Font> made;
    if (collection) {
        Gdiplus::FontFamily family(s.family, collection.get());
        if (family.IsAvailable() && family.IsStyleAvailable(s.style))
            made = std::make_unique<Gdiplus::Font>(&family, pixels, s.style, Gdiplus::UnitPixel);
    }
    if (!made || !made->IsAvailable())
        made = std::make_unique<Gdiplus::Font>(s.fallback, pixels, s.style, Gdiplus::UnitPixel);
    return *(cache[key] = std::move(made));
}
HFONT gdiFont(Face face, int pixelHeight) {
    const std::pair<int, int> key{int(face), pixelHeight};
    auto& cache = gdiCache();
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    const FaceSpec s = spec(face);
    HFONT made = CreateFontW(-pixelHeight, 0, 0, 0, s.style & Gdiplus::FontStyleBold ? FW_BOLD : FW_NORMAL, FALSE,
                             FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, s.family);
    return cache[key] = made;
}

Gdiplus::GraphicsPath* roundPath(RectF r, float radius) {
    auto* path = new Gdiplus::GraphicsPath();
    const float d = std::min({radius * 2.f, r.Width, r.Height});
    if (d <= 0.5f) { path->AddRectangle(r); return path; }
    path->AddArc(r.X, r.Y, d, d, 180, 90);
    path->AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    path->AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    path->AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    path->CloseFigure();
    return path;
}

void Canvas::fillRect(RectF r, Color c) {
    Gdiplus::SolidBrush brush(c);
    g.FillRectangle(&brush, r);
}
void Canvas::fillRound(RectF r, float radius, Color c) {
    std::unique_ptr<Gdiplus::GraphicsPath> path(roundPath(r, radius));
    Gdiplus::SolidBrush brush(c);
    g.FillPath(&brush, path.get());
}
void Canvas::strokeRound(RectF r, float radius, Color c, float width) {
    // Inset by half the pen so the stroke stays inside the rectangle like a CSS border.
    r.X += width / 2; r.Y += width / 2; r.Width -= width; r.Height -= width;
    std::unique_ptr<Gdiplus::GraphicsPath> path(roundPath(r, std::max(0.f, radius - width / 2)));
    Gdiplus::Pen pen(c, width);
    g.DrawPath(&pen, path.get());
}
void Canvas::glow(RectF r, float radius, Color c, float spread, int strength) {
    const int steps = std::max(2, int(spread));
    for (int i = steps; i >= 1; --i) {
        const float t = float(i) / float(steps);
        const float falloff = (1.f - t) * (1.f - t);
        const int alpha = int(strength * falloff);
        if (alpha <= 0) continue;
        RectF grown(r.X - i, r.Y - i, r.Width + 2.f * i, r.Height + 2.f * i);
        std::unique_ptr<Gdiplus::GraphicsPath> path(roundPath(grown, radius + i));
        Gdiplus::Pen pen(withAlpha(c, alpha), 1.4f);
        g.DrawPath(&pen, path.get());
    }
}
void Canvas::circle(float cx, float cy, float radius, Color c) {
    Gdiplus::SolidBrush brush(c);
    g.FillEllipse(&brush, cx - radius, cy - radius, radius * 2, radius * 2);
}
void Canvas::line(float x1, float y1, float x2, float y2, Color c, float width) {
    Gdiplus::Pen pen(c, width);
    g.DrawLine(&pen, x1, y1, x2, y2);
}
void Canvas::gradient(RectF r, float radius, Color a, Color b, float angleDegrees) {
    Gdiplus::LinearGradientBrush brush(r, a, b, angleDegrees, FALSE);
    std::unique_ptr<Gdiplus::GraphicsPath> path(roundPath(r, radius));
    g.FillPath(&brush, path.get());
}
void Canvas::scanlines(RectF r, int blackAlpha, float step) {
    if (blackAlpha <= 0) return;
    Gdiplus::Pen pen(Color(BYTE(blackAlpha), 0, 0, 0), 1.f);
    const Gdiplus::SmoothingMode previous = g.GetSmoothingMode();
    g.SetSmoothingMode(Gdiplus::SmoothingModeNone);
    for (float y = r.Y; y < r.Y + r.Height; y += step) g.DrawLine(&pen, r.X, y, r.X + r.Width, y);
    g.SetSmoothingMode(previous);
}
void Canvas::image(Gdiplus::Bitmap* bitmap, RectF dst, float radius) {
    if (!bitmap) return;
    Gdiplus::GraphicsState state = g.Save();
    std::unique_ptr<Gdiplus::GraphicsPath> path(roundPath(dst, radius));
    g.SetClip(path.get(), Gdiplus::CombineModeIntersect);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.DrawImage(bitmap, dst);
    g.Restore(state);
}

void Canvas::text(const std::wstring& s, RectF r, Face face, float px, Color c, Align align, bool vcenter, bool wrap) {
    if (s.empty()) return;
    Gdiplus::StringFormat format(Gdiplus::StringFormatFlagsNoClip);
    if (!wrap) format.SetFormatFlags(format.GetFormatFlags() | Gdiplus::StringFormatFlagsNoWrap);
    format.SetAlignment(align == Center ? Gdiplus::StringAlignmentCenter
                        : align == Right ? Gdiplus::StringAlignmentFar : Gdiplus::StringAlignmentNear);
    format.SetLineAlignment(vcenter ? Gdiplus::StringAlignmentCenter : Gdiplus::StringAlignmentNear);
    format.SetTrimming(wrap ? Gdiplus::StringTrimmingWord : Gdiplus::StringTrimmingEllipsisCharacter);
    Gdiplus::SolidBrush brush(c);
    g.DrawString(s.c_str(), INT(s.size()), &font(face, px), r, &format, &brush);
}
float Canvas::textWidth(const std::wstring& s, Face face, float px) {
    if (s.empty()) return 0;
    Gdiplus::StringFormat format(Gdiplus::StringFormatFlagsMeasureTrailingSpaces | Gdiplus::StringFormatFlagsNoWrap);
    RectF bounds;
    g.MeasureString(s.c_str(), INT(s.size()), &font(face, px), RectF(0, 0, 10000.f, 1000.f), &format, &bounds);
    return bounds.Width;
}
float Canvas::textHeight(const std::wstring& s, Face face, float px, float width) {
    if (s.empty()) return 0;
    Gdiplus::StringFormat format;
    RectF bounds;
    g.MeasureString(s.c_str(), INT(s.size()), &font(face, px), RectF(0, 0, width, 10000.f), &format, &bounds);
    return bounds.Height;
}

// ---- Surface -----------------------------------------------------------------------------

void applyWindowChrome(HWND window, bool glowBorder) {
    const Palette& p = pal();
    const BOOL dark = p.dark ? TRUE : FALSE;
    const int rounded = 2;  // DWMWCP_ROUND
    const COLORREF caption = rgb(p.pbg), text = rgb(p.ink), border = rgb(glowBorder ? p.pink : p.line);
    DwmSetWindowAttribute(window, 20, &dark, sizeof(dark));      // DWMWA_USE_IMMERSIVE_DARK_MODE
    DwmSetWindowAttribute(window, 33, &rounded, sizeof(rounded));  // DWMWA_WINDOW_CORNER_PREFERENCE
    DwmSetWindowAttribute(window, 34, &border, sizeof(border));  // DWMWA_BORDER_COLOR
    DwmSetWindowAttribute(window, 35, &caption, sizeof(caption));  // DWMWA_CAPTION_COLOR
    DwmSetWindowAttribute(window, 36, &text, sizeof(text));      // DWMWA_TEXT_COLOR
}

void Surface::initScale() {
    typedef UINT(WINAPI * GetDpiForWindowFn)(HWND);
    static const auto getDpi = reinterpret_cast<GetDpiForWindowFn>(reinterpret_cast<void*>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
    const UINT dpi = getDpi && hwnd ? getDpi(hwnd) : 96;
    scale = float(dpi ? dpi : 96) / 96.f;
}

void Surface::add(RectF r, int id, bool enabled) {
    RectF visible;
    if (!RectF::Intersect(visible, r, clip)) return;
    hits.push_back({visible, id, enabled});
}
int Surface::hitAt(float x, float y) const {
    for (int i = int(hits.size()) - 1; i >= 0; --i)
        if (hits[size_t(i)].r.Contains(x, y)) return i;
    return -1;
}

bool Surface::handle(UINT message, WPARAM, LPARAM lp, LRESULT& result) {
    switch (message) {
    case WM_ERASEBKGND:
        result = 1;
        return true;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        const int w = std::max<int>(1, rc.right), h = std::max<int>(1, rc.bottom);
        {
            Gdiplus::Bitmap buffer(w, h, PixelFormat32bppPARGB);
            Gdiplus::Graphics g(&buffer);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
            Canvas canvas(g);
            canvas.scale(scale);
            hits.clear();
            clip = RectF(0, 0, 100000.f, 100000.f);
            paint(canvas, float(w) / scale, float(h) / scale);
            Gdiplus::Graphics screen(dc);
            screen.DrawImage(&buffer, Gdiplus::Rect(0, 0, w, h), 0, 0, w, h, Gdiplus::UnitPixel);
        }
        EndPaint(hwnd, &ps);
        afterPaint();
        result = 0;
        return true;
    }
    case WM_MOUSEMOVE: {
        const int index = hitAt(float(GET_X_LPARAM(lp)) / scale, float(GET_Y_LPARAM(lp)) / scale);
        const int id = index >= 0 ? hits[size_t(index)].id : -1;
        if (id != hover) {
            hover = id;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&track);
        result = 0;
        return true;
    }
    case WM_MOUSELEAVE:
        if (hover != -1) { hover = -1; InvalidateRect(hwnd, nullptr, FALSE); }
        result = 0;
        return true;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            bool enabled = false;
            for (const auto& hit : hits) if (hit.id == hover && hit.enabled) enabled = true;
            SetCursor(LoadCursorW(nullptr, enabled ? MAKEINTRESOURCEW(32649) : MAKEINTRESOURCEW(32512)));
            result = TRUE;
            return true;
        }
        return false;
    case WM_LBUTTONDOWN:
        SetFocus(hwnd);
        SetCapture(hwnd);
        pressed = hover;
        InvalidateRect(hwnd, nullptr, FALSE);
        result = 0;
        return true;
    case WM_LBUTTONUP: {
        if (GetCapture() == hwnd) ReleaseCapture();
        const int index = hitAt(float(GET_X_LPARAM(lp)) / scale, float(GET_Y_LPARAM(lp)) / scale);
        const int id = index >= 0 && hits[size_t(index)].enabled ? hits[size_t(index)].id : -1;
        const bool fire = id >= 0 && id == pressed;
        pressed = -1;
        InvalidateRect(hwnd, nullptr, FALSE);
        if (fire) click(id);
        result = 0;
        return true;
    }
    }
    return false;
}

// ---- shared widgets ------------------------------------------------------------------------

void Surface::panel(Canvas& c, RectF r) {
    c.fillRound(r, 8, pal().pbg);
    c.strokeRound(r, 8, pal().line);
}
void Surface::label(Canvas& c, const std::wstring& s, RectF r) { c.text(s, r, Face::Display, 12, pal().ink2); }
void Surface::heading(Canvas& c, const std::wstring& s, RectF r, float px) {
    if (pal().dark) {
        // Soft cyan halo behind the heading text.
        for (float dx : {-1.5f, 0.f, 1.5f})
            for (float dy : {-1.5f, 0.f, 1.5f})
                if (dx != 0.f || dy != 0.f)
                    c.text(s, RectF(r.X + dx, r.Y + dy, r.Width, r.Height), Face::Display, px, withAlpha(pal().cyan, 38));
    }
    c.text(s, r, Face::Display, px, pal().cyan);
}

void Surface::button(Canvas& c, RectF r, const std::wstring& text, int id, Button kind, bool enabled, Color accent) {
    const Palette& p = pal();
    const bool over = enabled && hovered(id), down = enabled && held(id);
    const int fade = enabled ? 255 : 115;
    switch (kind) {
    case Button::Primary:
        if (enabled) c.glow(r, 6, p.pink, 9, over ? 150 : 100);
        c.fillRound(r, 6, withAlpha(down ? Color(255, 214, 40, 184) : p.pink, fade));
        c.text(text, r, Face::BodyBold, 13, withAlpha(p.onacc, fade), Center);
        break;
    case Button::Outline:
    case Button::Danger: {
        const Color color = kind == Button::Danger ? p.red : (accent.GetAlpha() ? accent : p.cyan);
        if (over) c.fillRound(r, 6, withAlpha(color, 28));
        c.strokeRound(r, 6, withAlpha(color, fade));
        c.text(text, r, Face::BodySemi, 13, withAlpha(color, fade), Center);
        break;
    }
    case Button::Link:
        c.text(text, r, Face::BodySemi, 12, over ? p.pink : p.cyan, Left);
        break;
    case Button::Segment:
    case Button::SegmentOn:
        c.fillRound(r, 4, kind == Button::SegmentOn ? p.pink : Color(0, 0, 0, 0));
        c.text(text, r, Face::BodySemi, 12, kind == Button::SegmentOn ? p.onacc : (over ? p.ink : p.ink2), Center);
        break;
    case Button::Chip:
    case Button::ChipOn:
        c.fillRound(r, 14, kind == Button::ChipOn ? p.pink : Color(0, 0, 0, 0));
        c.strokeRound(r, 14, kind == Button::ChipOn ? p.pink : (over ? p.ink2 : p.line));
        c.text(text, r, Face::BodySemi, 12, kind == Button::ChipOn ? p.onacc : p.ink2, Center);
        break;
    default:  // Secondary
        if (over) c.glow(r, 6, p.cyan, 8, 70);
        c.fillRound(r, 6, withAlpha(down ? p.line : p.ibg, fade));
        c.strokeRound(r, 6, p.line);
        c.text(text, r, Face::BodySemi, 13, withAlpha(p.ink, fade), Center);
        break;
    }
    add(r, id, enabled);
}

void Surface::toggle(Canvas& c, RectF r, bool on) {
    const Palette& p = pal();
    if (on) {
        c.glow(r, 10, p.pink, 9, 110);
        c.fillRound(r, 10, p.pink);
    } else {
        c.strokeRound(r, 10, p.ink2);
    }
    const float knob = on ? r.X + 22.f : r.X + 3.f;
    c.circle(knob + 7.f, r.Y + r.Height / 2, 7.f, on ? p.onacc : p.ink2);
}
void Surface::radio(Canvas& c, float cx, float cy, bool on) {
    const Palette& p = pal();
    c.strokeRound(RectF(cx - 7, cy - 7, 14, 14), 7, on ? p.pink : p.ink2, 2.f);
    if (on) c.circle(cx, cy, 3.f, p.pink);
}
void Surface::chip(Canvas& c, float x, float y, const std::wstring& text, Color color, float* widthOut) {
    const float w = c.textWidth(text, Face::BodyBold, 10) + 12.f;
    RectF r(x, y, w, 18);
    c.strokeRound(r, 3, color);
    c.text(text, r, Face::BodyBold, 10, color, Center);
    if (widthOut) *widthOut = w;
}

}  // namespace ui
