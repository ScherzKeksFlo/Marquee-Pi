#pragma once
#include <windows.h>
#include <string>
#include "ui_kit.hpp"

// Thumbnails for media files and game artwork, rendered on one worker thread with
// createThumbnail() and cached by path, size and modification time. get() and drain()
// belong to the UI thread; windows that want to repaint when a thumbnail arrives
// subscribe and handle WM_THUMB_READY by calling drain().
constexpr UINT WM_THUMB_READY = WM_APP + 31;

class ThumbCache {
public:
    // Thumbnails have the aspect ratio of the Pi display (5:3 until it reports another one).
    int width() const;
    int height() const;
    // height / width of the display; changing it drops the cached thumbnails.
    void setRatio(double ratio);

    static ThumbCache& instance();
    // Null until the thumbnail exists; asks the worker to make it on the first call.
    Gdiplus::Bitmap* get(const std::wstring& path);
    // True when the file was tried and cannot be decoded (e.g. WebP without the Windows extension).
    bool failed(const std::wstring& path);
    // Moves finished thumbnails into the cache. Returns true when something changed.
    bool drain();
    void subscribe(HWND window);
    void unsubscribe(HWND window);
    void shutdown();

private:
    ThumbCache();
    struct Impl;
    Impl* impl;
};
