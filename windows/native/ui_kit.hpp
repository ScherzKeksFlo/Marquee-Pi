#pragma once
// Drawing kit for the arcade-themed windows: palette, bundled fonts and a small GDI+
// canvas. All coordinates passed to Canvas are device independent pixels (96 dpi); the
// owning window applies the DPI scale with Canvas::scale().
#include <windows.h>
#include <algorithm>
using std::min;
using std::max;
#include <gdiplus.h>
#include <string>
#include <vector>

namespace ui {

using Gdiplus::Color;
using Gdiplus::RectF;

struct Palette {
    bool dark;
    Color wbg, pbg, ibg, line, ink, ink2, pink, cyan, yel, grn, red, onacc;
};

const Palette& pal();
// "dark", "light" or "auto" (follows the Windows app theme).
void setTheme(const std::string& setting);
bool isDark();
Color withAlpha(Color c, int alpha);
inline COLORREF rgb(Color c) { return RGB(c.GetR(), c.GetG(), c.GetB()); }

enum class Face { Body, BodySemi, BodyBold, Display, DisplayBold, Mono, MonoMedium };

// Starts GDI+ and registers the bundled fonts. Call once per process; shutdown() after
// every GDI+ object (windows, bitmaps) is gone.
void startup(HINSTANCE instance);
void shutdown();
// GDI font for stock controls (EDIT) at a pixel height.
HFONT gdiFont(Face face, int pixelHeight);
const Gdiplus::Font& font(Face face, float pixels);

enum Align { Left = 0, Center = 1, Right = 2 };

class Canvas {
public:
    explicit Canvas(Gdiplus::Graphics& graphics) : g(graphics) {}
    Gdiplus::Graphics& g;

    void scale(float factor) { g.ScaleTransform(factor, factor); }
    void fillRect(RectF r, Color c);
    void fillRound(RectF r, float radius, Color c);
    void strokeRound(RectF r, float radius, Color c, float width = 1.f);
    // Soft colored halo around a rounded rectangle (approximates a blurred box-shadow).
    void glow(RectF r, float radius, Color c, float spread, int strength);
    void circle(float cx, float cy, float radius, Color c);
    void line(float x1, float y1, float x2, float y2, Color c, float width = 1.f);
    void gradient(RectF r, float radius, Color a, Color b, float angleDegrees);
    void scanlines(RectF r, int blackAlpha, float step = 3.f);
    void image(Gdiplus::Bitmap* bitmap, RectF dst, float radius = 0.f);

    void text(const std::wstring& s, RectF r, Face face, float px, Color c, Align align = Left,
              bool vcenter = true, bool wrap = false);
    float textWidth(const std::wstring& s, Face face, float px);
    // Height needed to draw `s` wrapped to `width`.
    float textHeight(const std::wstring& s, Face face, float px, float width);
};

Gdiplus::GraphicsPath* roundPath(RectF r, float radius);

// Windows 11 frame: dark caption for the dark theme, rounded corners, themed caption and
// border colors. Attributes the running Windows does not know are ignored.
void applyWindowChrome(HWND window, bool glowBorder);

// A window that paints itself with Canvas and handles mouse input through hit regions
// registered while painting (immediate mode). Coordinates are DIPs.
enum class Button { Secondary, Primary, Outline, Danger, Link, Segment, SegmentOn, Chip, ChipOn };

struct Hit { RectF r; int id; bool enabled; };

class Surface {
public:
    virtual ~Surface() = default;
    HWND hwnd = nullptr;
    float scale = 1.f;

    // Forwards the mouse and paint messages; returns true when it handled `message`.
    bool handle(UINT message, WPARAM wp, LPARAM lp, LRESULT& result);

protected:
    virtual void paint(Canvas& canvas, float width, float height) = 0;
    virtual void click(int id) = 0;
    virtual void afterPaint() {}
    void initScale();  // reads the DPI of the window

    std::vector<Hit> hits;
    RectF clip{0, 0, 100000.f, 100000.f};
    int hover = -1, pressed = -1;

    void add(RectF r, int id, bool enabled = true);
    bool hovered(int id) const { return hover == id; }
    bool held(int id) const { return pressed == id && hover == id; }
    int hitAt(float x, float y) const;  // index into hits or -1

    // Shared look.
    void panel(Canvas& c, RectF r);
    void label(Canvas& c, const std::wstring& s, RectF r);  // Silkscreen panel label
    void heading(Canvas& c, const std::wstring& s, RectF r, float px = 22.f);
    // Draws the button and registers its hit region.
    void button(Canvas& c, RectF r, const std::wstring& text, int id, Button kind = Button::Secondary,
                bool enabled = true, Color accent = Color(0, 0, 0, 0));
    void toggle(Canvas& c, RectF r, bool on);
    void radio(Canvas& c, float cx, float cy, bool on);
    void chip(Canvas& c, float x, float y, const std::wstring& text, Color color, float* widthOut = nullptr);
};

}  // namespace ui
