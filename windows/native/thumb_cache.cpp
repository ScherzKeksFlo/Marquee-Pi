#include "thumb_cache.hpp"
#include "thumbnail.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

struct ThumbCache::Impl {
    struct Finished { std::wstring key; std::unique_ptr<Gdiplus::Bitmap> bitmap; };
    struct Job { std::wstring key, path; int width, height; };

    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Job> jobs;
    std::vector<Finished> finished;
    std::vector<HWND> subscribers;
    bool stopping = false;
    std::thread worker;

    std::atomic<int> width{320}, height{192};  // changed by setRatio on the UI thread, read by the worker

    // UI thread only.
    std::map<std::wstring, std::unique_ptr<Gdiplus::Bitmap>> cache;
    std::set<std::wstring> failedKeys, pending;

    std::wstring keyFor(const std::wstring& path) const {
        std::error_code ec;
        const auto stamp = std::filesystem::last_write_time(path, ec).time_since_epoch().count();
        const auto size = std::filesystem::file_size(path, ec);
        return path + L"|" + std::to_wstring(size) + L"|" + std::to_wstring(stamp) + L"|" +
               std::to_wstring(width.load()) + L"x" + std::to_wstring(height.load());
    }

    // Copies the DIB into a GDI+ bitmap with opaque alpha (the DIB's alpha byte is unused).
    static std::unique_ptr<Gdiplus::Bitmap> toBitmap(HBITMAP source) {
        BITMAP info{};
        if (!GetObjectW(source, sizeof(info), &info) || info.bmBitsPixel != 32 || !info.bmBits) return nullptr;
        auto bitmap = std::make_unique<Gdiplus::Bitmap>(info.bmWidth, info.bmHeight, PixelFormat32bppARGB);
        Gdiplus::BitmapData data;
        Gdiplus::Rect all(0, 0, info.bmWidth, info.bmHeight);
        if (bitmap->LockBits(&all, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &data) != Gdiplus::Ok) return nullptr;
        for (int y = 0; y < info.bmHeight; ++y) {
            const auto* from = static_cast<const BYTE*>(info.bmBits) + size_t(y) * info.bmWidthBytes;
            auto* to = static_cast<uint32_t*>(data.Scan0) + size_t(y) * (data.Stride / 4);
            for (int x = 0; x < info.bmWidth; ++x)
                to[x] = 0xFF000000u | (reinterpret_cast<const uint32_t*>(from)[x] & 0x00FFFFFFu);
        }
        bitmap->UnlockBits(&data);
        return bitmap;
    }

    void run() {
        const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        while (true) {
            Job job;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock, [&] { return stopping || !jobs.empty(); });
                if (stopping) break;
                job = std::move(jobs.front());
                jobs.pop_front();
            }
            std::unique_ptr<Gdiplus::Bitmap> bitmap;
            if (com) {
                if (HBITMAP raw = createThumbnail(job.path, job.width, job.height)) {
                    bitmap = toBitmap(raw);
                    DeleteObject(raw);
                }
            }
            std::vector<HWND> targets;
            {
                std::lock_guard<std::mutex> lock(mutex);
                finished.push_back({job.key, std::move(bitmap)});
                targets = subscribers;
            }
            for (HWND target : targets) PostMessageW(target, WM_THUMB_READY, 0, 0);
        }
        if (com) CoUninitialize();
    }
};

ThumbCache::ThumbCache() : impl(new Impl) { impl->worker = std::thread([this] { impl->run(); }); }

ThumbCache& ThumbCache::instance() {
    static ThumbCache cache;
    return cache;
}

Gdiplus::Bitmap* ThumbCache::get(const std::wstring& path) {
    if (path.empty()) return nullptr;
    const std::wstring key = impl->keyFor(path);
    auto it = impl->cache.find(key);
    if (it != impl->cache.end()) return it->second.get();
    if (impl->failedKeys.count(key) || impl->pending.count(key)) return nullptr;
    impl->pending.insert(key);
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->jobs.push_back({key, path, impl->width.load(), impl->height.load()});
    }
    impl->wake.notify_one();
    return nullptr;
}
bool ThumbCache::failed(const std::wstring& path) {
    return !path.empty() && impl->failedKeys.count(impl->keyFor(path)) != 0;
}
int ThumbCache::width() const { return impl->width; }
int ThumbCache::height() const { return impl->height; }
void ThumbCache::setRatio(double ratio) {
    if (!(ratio > 0.05 && ratio < 20)) return;
    const int base = 320;
    const int w = ratio <= 1 ? base : std::max(16, int(base / ratio + 0.5));
    const int h = ratio <= 1 ? std::max(16, int(base * ratio + 0.5)) : base;
    if (w == impl->width && h == impl->height) return;
    impl->width = w;
    impl->height = h;
    impl->cache.clear();  // the old thumbnails have the wrong shape; new requests use the new size
    impl->failedKeys.clear();
    impl->pending.clear();
}
bool ThumbCache::drain() {
    std::vector<Impl::Finished> done;
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        done.swap(impl->finished);
    }
    for (auto& item : done) {
        impl->pending.erase(item.key);
        if (item.bitmap) impl->cache[item.key] = std::move(item.bitmap);
        else impl->failedKeys.insert(item.key);
    }
    return !done.empty();
}
void ThumbCache::subscribe(HWND window) {
    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->subscribers.push_back(window);
}
void ThumbCache::unsubscribe(HWND window) {
    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->subscribers.erase(std::remove(impl->subscribers.begin(), impl->subscribers.end(), window),
                            impl->subscribers.end());
}
void ThumbCache::shutdown() {
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->stopping = true;
    }
    impl->wake.notify_all();
    if (impl->worker.joinable()) impl->worker.join();
    impl->cache.clear();  // GDI+ bitmaps must be gone before ui::shutdown()
}
