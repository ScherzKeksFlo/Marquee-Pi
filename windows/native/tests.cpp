#include "core.hpp"
#include "event_log.hpp"
#include "pi_sync.hpp"
#include "strings.hpp"
#include "thumbnail.hpp"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wincodec.h>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>
#include <iostream>

// Encodes a 1000x562 H.264 clip (neither side is a multiple of 16, so decoder
// buffers carry padding): red top half, blue bottom half, green bar on the left.
// Returns false when this system has no H.264 encoder.
static bool writeTestVideo(const wchar_t* path) {
    const UINT32 W = 1000, H = 562, FPS = 10, FRAMES = 10;
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) return false;
    bool ok = false;
    IMFSinkWriter* writer = nullptr;
    IMFMediaType *out = nullptr, *in = nullptr;
    DWORD stream = 0;
    do {
        if (FAILED(MFCreateSinkWriterFromURL(path, nullptr, nullptr, &writer))) break;
        if (FAILED(MFCreateMediaType(&out)) || FAILED(MFCreateMediaType(&in))) break;
        out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        out->SetUINT32(MF_MT_AVG_BITRATE, 800000);
        out->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        MFSetAttributeSize(out, MF_MT_FRAME_SIZE, W, H);
        MFSetAttributeRatio(out, MF_MT_FRAME_RATE, FPS, 1);
        MFSetAttributeRatio(out, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        if (FAILED(writer->AddStream(out, &stream))) break;
        in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        in->SetUINT32(MF_MT_DEFAULT_STRIDE, W * 4);  // positive = top-down input
        MFSetAttributeSize(in, MF_MT_FRAME_SIZE, W, H);
        MFSetAttributeRatio(in, MF_MT_FRAME_RATE, FPS, 1);
        MFSetAttributeRatio(in, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        if (FAILED(writer->SetInputMediaType(stream, in, nullptr)) || FAILED(writer->BeginWriting())) break;
        bool failed = false;
        for (UINT32 f = 0; f < FRAMES && !failed; ++f) {
            IMFMediaBuffer* buffer = nullptr;
            IMFSample* sample = nullptr;
            BYTE* data = nullptr;
            if (FAILED(MFCreateMemoryBuffer(W * H * 4, &buffer)) || FAILED(buffer->Lock(&data, nullptr, nullptr))) {
                failed = true;
            } else {
                for (UINT32 y = 0; y < H; ++y)
                    for (UINT32 x = 0; x < W; ++x)
                        reinterpret_cast<uint32_t*>(data)[y * W + x] =
                            x < W / 6 ? 0x0000FF00u : (y < H / 2 ? 0x00FF0000u : 0x000000FFu);
                buffer->Unlock();
                buffer->SetCurrentLength(W * H * 4);
                failed = FAILED(MFCreateSample(&sample)) || FAILED(sample->AddBuffer(buffer)) ||
                         FAILED(sample->SetSampleTime(LONGLONG(f) * 10000000 / FPS)) ||
                         FAILED(sample->SetSampleDuration(10000000 / FPS)) ||
                         FAILED(writer->WriteSample(stream, sample));
            }
            if (sample) sample->Release();
            if (buffer) buffer->Release();
        }
        ok = !failed && SUCCEEDED(writer->Finalize());
    } while (false);
    if (in) in->Release();
    if (out) out->Release();
    if (writer) writer->Release();
    MFShutdown();
    return ok;
}

static uint32_t pixelAt(HBITMAP bitmap, int percentX, int percentY) {
    BITMAP info{};
    GetObjectW(bitmap, sizeof(info), &info);
    auto* pixels = static_cast<uint32_t*>(info.bmBits);
    return pixels[(info.bmHeight * percentY / 100) * info.bmWidth + info.bmWidth * percentX / 100] & 0xFFFFFF;
}

// Writes a gradient image of the given container format (GUID_ContainerFormatPng/Jpeg).
static bool writeTestImage(const std::wstring& path, const GUID& container, UINT width, UINT height) {
    IWICImagingFactory* factory = nullptr;
    IWICBitmap* bitmap = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    std::vector<uint8_t> pixels(size_t(width) * height * 4);
    for (UINT y = 0; y < height; ++y)
        for (UINT x = 0; x < width; ++x) {
            uint8_t* p = &pixels[(size_t(y) * width + x) * 4];
            p[0] = uint8_t(x * 255 / width); p[1] = uint8_t(y * 255 / height); p[2] = 128; p[3] = 255;
        }
    bool ok = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                         IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->CreateBitmapFromMemory(width, height, GUID_WICPixelFormat32bppBGRA, width * 4,
                                                  UINT(pixels.size()), pixels.data(), &bitmap)) &&
        SUCCEEDED(factory->CreateStream(&stream)) &&
        SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
        SUCCEEDED(factory->CreateEncoder(container, nullptr, &encoder)) &&
        SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) &&
        SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
        SUCCEEDED(frame->SetSize(width, height));
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    ok = ok && SUCCEEDED(frame->SetPixelFormat(&format)) && SUCCEEDED(frame->WriteSource(bitmap, nullptr)) &&
         SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
    if (frame) frame->Release();
    if (encoder) encoder->Release();
    if (stream) stream->Release();
    if (bitmap) bitmap->Release();
    if (factory) factory->Release();
    return ok;
}

static std::string decodeBase64(const std::string& text) {
    DWORD size = 0;
    if (!CryptStringToBinaryA(text.c_str(), 0, CRYPT_STRING_BASE64, nullptr, &size, nullptr, nullptr)) return "";
    std::string bytes(size, '\0');
    if (!CryptStringToBinaryA(text.c_str(), 0, CRYPT_STRING_BASE64, reinterpret_cast<BYTE*>(bytes.data()), &size,
                              nullptr, nullptr)) return "";
    bytes.resize(size);
    return bytes;
}

// True when the channel at the given bit shift (16 red, 8 green, 0 blue) clearly dominates.
static bool isDominant(uint32_t rgb, int shift) {
    for (int other : {16, 8, 0})
        if (other != shift && ((rgb >> other) & 0xFF) >= 60) return false;
    return ((rgb >> shift) & 0xFF) > 200;
}

// ---- PiSync: characterization tests against a scripted fake Pi -------------------------
// These pin the behaviour of the former App::pollLoop, quirks included. The Pi status
// is polled every 7th cycle, so an offline Pi is noticed again only on such a cycle.
namespace {
struct FakePi : PiTransport {
    struct Call { std::wstring method, path; std::string body; };
    std::vector<Call> calls;
    std::function<HttpResult(const Call&)> handler;  // may throw HttpError or runtime_error
    std::string instance = "a";
    std::string events = "[]";

    HttpResult request(const Settings&, const std::wstring& method, const std::wstring& path,
                       const std::string& body, const std::wstring&, int) override {
        Call call{method, path, body};
        calls.push_back(call);
        if (handler) {
            HttpResult custom = handler(call);
            if (custom.status) return custom;
        }
        if (path == L"/v1/status") return {200, R"({"game_title":""})"};
        if (path.rfind(L"/v1/gesture-events", 0) == 0)
            return {200, "{\"instance_id\":\"" + instance + "\",\"events\":" + events + "}"};
        return {200, "{}"};
    }
    int count(const std::wstring& path) const {
        int n = 0;
        for (const auto& call : calls) n += call.path == path;
        return n;
    }
    void forget() { calls.clear(); }
};

Settings piSettings() {
    Settings s;
    s.piUrl = L"http://10.0.0.10:8765";
    s.token = L"0123456789abcdef0123456789abcdef";
    return s;
}
GameMessage piGame(const wchar_t* title = L"Game") {
    GameMessage g;
    g.action = "game";
    g.title = title;
    return g;
}
void ticks(PiSync& sync, int n) { for (int i = 0; i < n; ++i) sync.tick(); }
HttpResult failWith(DWORD status) { throw HttpError(status, "scripted"); }

void eventLogTests() {
    EventLog log;
    assert(log.snapshot().empty());
    log.add(LogLevel::Info, "pipe", L"first");
    log.add(LogLevel::Error, "conn", L"second");
    auto entries = log.snapshot();  // newest first
    assert(entries.size() == 2 && entries[0].message == L"second" && entries[0].level == LogLevel::Error);
    assert(entries[1].source == "pipe" && entries[0].time >= entries[1].time);
    const unsigned before = log.revision();
    for (size_t i = 0; i < EventLog::CAPACITY + 10; ++i) log.add(LogLevel::Warn, "api", std::to_wstring(i));
    entries = log.snapshot();
    assert(entries.size() == EventLog::CAPACITY);  // the ring drops the oldest entries
    assert(entries.front().message == std::to_wstring(EventLog::CAPACITY + 9));
    assert(log.revision() > before);
    log.clear();
    assert(log.snapshot().empty());
}

void piSyncTests() {
    // A game started while online is sent once; the first tick also pushes gestures and language.
    {
        FakePi pi;
        PiSync sync(piSettings(), pi);
        sync.gameStarted(piGame());
        ticks(sync, 1);
        assert(pi.count(L"/v1/status") == 1);
        assert(pi.count(L"/v1/gesture-config") == 1 && pi.count(L"/v1/language") == 1);
        assert(pi.count(L"/v1/game") == 1);
        assert(pi.calls.back().path.rfind(L"/v1/gesture-events", 0) == 0);
    }
    // Offline Pi: nothing is sent, but the status is polled every tick so a return is noticed at once.
    {
        FakePi pi;
        bool offline = true;
        pi.handler = [&](const FakePi::Call&) -> HttpResult {
            if (offline) throw std::runtime_error("unreachable");
            return {};
        };
        PiSync sync(piSettings(), pi);
        sync.gameStarted(piGame());
        ticks(sync, 1);
        assert(pi.calls.size() == 1 && pi.count(L"/v1/game") == 0);
        pi.forget();
        offline = true;
        ticks(sync, 3);
        assert(pi.count(L"/v1/status") == 3 && pi.calls.size() == 3);
        offline = false;
        pi.forget();
        ticks(sync, 1);  // status succeeds, the pending game goes out
        assert(pi.count(L"/v1/status") == 1 && pi.count(L"/v1/game") == 1);
    }
    // Game exit: /v1/default once, then quiet.
    {
        FakePi pi;
        PiSync sync(piSettings(), pi);
        sync.gameStarted(piGame());
        ticks(sync, 2);
        sync.gameExited();
        pi.forget();
        ticks(sync, 2);
        assert(pi.count(L"/v1/default") == 1);
        assert(!sync.inGame());
    }
    // 4xx from /v1/game is not retried; 5xx is retried, but only after the next status cycle.
    {
        FakePi pi;
        pi.handler = [](const FakePi::Call& c) -> HttpResult {
            if (c.path == L"/v1/game") return failWith(400);
            return {};
        };
        PiSync sync(piSettings(), pi);
        sync.gameStarted(piGame());
        ticks(sync, 10);
        assert(pi.count(L"/v1/game") == 1);
    }
    {
        FakePi pi;
        pi.handler = [](const FakePi::Call& c) -> HttpResult {
            if (c.path == L"/v1/game") return failWith(500);
            return {};
        };
        PiSync sync(piSettings(), pi);
        sync.gameStarted(piGame());
        ticks(sync, 7);  // cycles 0..6
        assert(pi.count(L"/v1/game") == 1);
        ticks(sync, 1);  // cycle 7
        assert(pi.count(L"/v1/game") == 2);
    }
    // A restarted Pi (new instance id) gets game and gestures again; its first sighting does not.
    {
        FakePi pi;
        PiSync sync(piSettings(), pi);
        sync.gameStarted(piGame());
        ticks(sync, 1);
        assert(pi.count(L"/v1/game") == 1 && pi.count(L"/v1/gesture-config") == 1);
        ticks(sync, 2);
        assert(pi.count(L"/v1/game") == 1 && pi.count(L"/v1/gesture-config") == 1);
        pi.instance = "b";  // Pi restarted
        ticks(sync, 2);
        assert(pi.count(L"/v1/game") == 2 && pi.count(L"/v1/gesture-config") == 2);
    }
    // A Pi without language support (404) still counts as configured; other errors keep gestures dirty.
    {
        FakePi pi;
        pi.handler = [](const FakePi::Call& c) -> HttpResult {
            if (c.path == L"/v1/language") return failWith(404);
            return {};
        };
        PiSync sync(piSettings(), pi);
        ticks(sync, 3);
        assert(pi.count(L"/v1/gesture-config") == 1);
        assert(pi.count(L"/v1/language") == 1);
    }
    {
        FakePi pi;
        pi.handler = [](const FakePi::Call& c) -> HttpResult {
            if (c.path == L"/v1/language") return failWith(500);
            return {};
        };
        PiSync sync(piSettings(), pi);
        ticks(sync, 3);
        assert(pi.count(L"/v1/gesture-config") == 3);  // never cleared, pushed every tick
    }
    // Heartbeat: every third status cycle (14, 28, ...) while a game is active, never without one.
    {
        FakePi pi;
        PiSync sync(piSettings(), pi);
        sync.gameStarted(piGame());
        ticks(sync, 14);
        assert(pi.count(L"/v1/heartbeat") == 0);
        ticks(sync, 1);
        assert(pi.count(L"/v1/heartbeat") == 1);
    }
    {
        FakePi pi;
        PiSync sync(piSettings(), pi);
        ticks(sync, 30);
        assert(pi.count(L"/v1/heartbeat") == 0);
    }
    // RetroArch menu events fire only once the cursor has caught up, during a game.
    {
        FakePi pi;
        int fired = 0;
        PiSyncEvents events;
        events.retroArchMenu = [&] { ++fired; };
        PiSync sync(piSettings(), pi, events);
        sync.gameStarted(piGame());
        pi.events = R"([{"id":1,"action":"retroarch_menu"}])";
        ticks(sync, 2);  // tick 0 sees the new instance, tick 1 catches up on the backlog
        assert(fired == 0);
        pi.events = R"([{"id":2,"action":"retroarch_menu"}])";
        ticks(sync, 1);
        assert(fired == 1);
        ticks(sync, 1);  // same id again: already seen
        assert(fired == 1);
        sync.gameExited();
        pi.events = R"([{"id":3,"action":"retroarch_menu"}])";
        ticks(sync, 1);
        assert(fired == 1);  // no game, no menu
    }
    // A settings change that lands while gestures are being sent is not lost.
    {
        FakePi pi;
        PiSync sync(piSettings(), pi);
        bool changed = false;
        pi.handler = [&](const FakePi::Call& c) -> HttpResult {
            if (c.path == L"/v1/gesture-config" && !changed) {
                changed = true;
                Settings next = piSettings();
                next.gestures["swipe-up"] = "marquee";
                sync.settingsChanged(next);
            }
            return {};
        };
        ticks(sync, 1);
        assert(pi.count(L"/v1/gesture-config") == 1);
        pi.forget();
        ticks(sync, 1);
        assert(pi.count(L"/v1/gesture-config") == 1);
        bool sentNewGesture = false;
        for (const auto& call : pi.calls)
            if (call.path == L"/v1/gesture-config") sentNewGesture = call.body.find("swipe-up") != std::string::npos;
        assert(sentNewGesture);
        assert(sync.settings().gestures.count("swipe-up") == 1);
    }
    // Settings changed while the game is being sent (new Pi address or token): send it again.
    {
        FakePi pi;
        PiSync sync(piSettings(), pi);
        bool changed = false;
        pi.handler = [&](const FakePi::Call& c) -> HttpResult {
            if (c.path == L"/v1/game" && !changed) {
                changed = true;
                sync.settingsChanged(piSettings());
            }
            return {};
        };
        sync.gameStarted(piGame());
        ticks(sync, 1);
        assert(pi.count(L"/v1/game") == 1);
        ticks(sync, 1);
        assert(pi.count(L"/v1/game") == 2);
        ticks(sync, 1);
        assert(pi.count(L"/v1/game") == 2);
    }
    // Tray commands: showDefault drops the game and cancels the pending default.
    {
        FakePi pi;
        PiSync sync(piSettings(), pi);
        sync.gameStarted(piGame());
        sync.showDefault();
        assert(!sync.inGame() && pi.count(L"/v1/default") == 1);
        pi.forget();
        ticks(sync, 2);
        assert(pi.count(L"/v1/default") == 0 && pi.count(L"/v1/game") == 0);
        sync.rebootPi(); sync.reloadDisplay(); sync.shutdownPi(3000);
        assert(pi.count(L"/v1/reboot") == 1 && pi.count(L"/v1/reload") == 1 && pi.count(L"/v1/shutdown") == 1);
    }
    // An offline Pi is reported once, then again on every status cycle; recovery is reported at once.
    {
        FakePi pi;
        bool offline = true;
        pi.handler = [&](const FakePi::Call&) -> HttpResult {
            if (offline) throw std::runtime_error("unreachable");
            return {};
        };
        std::vector<bool> seen;
        PiSyncEvents events;
        events.status = [&](bool connected, const std::string&, int) { seen.push_back(connected); };
        PiSync sync(piSettings(), pi, events);
        ticks(sync, 8);  // cycles 0..7
        assert((seen == std::vector<bool>{false, false}));
        offline = false;
        ticks(sync, 1);
        assert((seen == std::vector<bool>{false, false, true}));
    }
    // Status events carry the Pi's game title (empty for the default media) and the round trip.
    {
        FakePi pi;
        std::vector<std::pair<bool, std::string>> seen;
        int latency = -2;
        PiSyncEvents events;
        events.status = [&](bool connected, const std::string& title, int ms) {
            seen.push_back({connected, title});
            latency = ms;
        };
        PiSync sync(piSettings(), pi, events);
        ticks(sync, 1);
        assert(seen.size() == 1 && seen[0].first && seen[0].second.empty());
        assert(latency >= 0);
    }
    // Offline status events report no latency; accepted games report their size.
    {
        FakePi pi;
        pi.handler = [&](const FakePi::Call& c) -> HttpResult {
            if (c.path == L"/v1/status") throw std::runtime_error("unreachable");
            return {};
        };
        int latency = 5;
        PiSyncEvents events;
        events.status = [&](bool, const std::string&, int ms) { latency = ms; };
        PiSync offline(piSettings(), pi, events);
        ticks(offline, 1);
        assert(latency == -1);
    }
    {
        FakePi pi;
        size_t sentBytes = 0;
        int sentCalls = 0;
        PiSyncEvents events;
        events.gameSent = [&](size_t bytes, int) { sentBytes = bytes; ++sentCalls; };
        PiSync sync(piSettings(), pi, events);
        sync.gameStarted(piGame());
        ticks(sync, 2);
        assert(sentCalls == 1 && sentBytes > 0);
    }
}
}  // namespace

int main() {
    eventLogTests();
    piSyncTests();
    using namespace mini;
    static_assert(MARQUEE_PI_ARTWORK_BUDGET_BYTES == 24379392);
    static_assert((MARQUEE_PI_ARTWORK_BUDGET_BYTES * 4 + 2) / 3 +
                  MARQUEE_PI_JSON_HEADROOM_BYTES <= MARQUEE_PI_MAX_REQUEST_BYTES);
    auto parsed = parse(R"({"Action":"game","Title":"P\u00e4c Man","MarqueePath":"C:\\art.png","n":42,"a":[true,null]})");
    assert(parsed.get("Title").value() == (std::string("P") + "\xc3\xa4" + "c Man"));
    assert(parsed.get("n").integer() == 42);
    assert(parsed.get("a").items.size() == 2);
    assert(parse(parsed.dump()).get("Title").value() == parsed.get("Title").value());
    auto decimals = parse(R"({"ratio":1.25,"small":-2e-3,"large":4E+2})");
    assert(decimals.get("ratio").real() == 1.25);
    assert(decimals.get("small").real() == -0.002);
    assert(decimals.get("large").real() == 400.0);
    assert(decimals.get("ratio").integer(-1) == -1);
    assert(parse(decimals.dump()).get("small").real() == -0.002);
    auto game = parseGameMessage(parsed.dump());
    assert(game.action == "game" && game.title == L"P\u00e4c Man");
    auto warnings = gameWarnings(R"({"ok":true,"warnings":[{"kind":"marquee","error":"bad image"},{"kind":"logo","error":""},null]})");
    assert(warnings.size() == 2);
    assert(warnings[0] == "marquee: bad image");
    assert(warnings[1] == "logo");
    assert(validHotkey(L"Ctrl+Shift+F1"));
    assert(!validHotkey(L"Ctrl+Ctrl+F1"));
    assert(!validHotkey(L"garbage"));
    assert(isPowerOffType(L" Shutdown "));
    assert(isPowerOffType(L"HERUNTERFAHREN"));
    assert(!isPowerOffType(L"restart"));
    assert(!isPowerOffType(L"Computer neu starten"));
    assert(!isPowerOffType(L"shutdown and restart"));
    assert(!isPowerOffType(L"arrêter"));
    assert(isUser32ShutdownEvent(L"<Event><System><Provider Name='User32' Guid='{x}'/></System><EventData><Data Name='param5'>Herunterfahren</Data></EventData></Event>"));
    assert(isUser32ShutdownEvent(L"<Provider Name=\"User32\"/>"));
    assert(!isUser32ShutdownEvent(L"<Provider Name='Kernel-Power'/>"));
    assert(std::filesystem::exists(std::filesystem::path(dataDirectory()).parent_path() / L"portable.flag"));
    Settings settings = loadSettings();
    assert(!settings.configured());
    settings.piUrl = L"http://10.0.0.10:8765";
    settings.token = L"abcdefghijklmnopqrstuvwxyz0123456789";
    settings.hotkey = L"Ctrl+F1";
    settings.retroArchNetworkControl = true;
    settings.retroArchNetworkPort = 55355;
    settings.gestures["swipe-left"] = "box_art";
    settings.autostart = false;
    saveSettings(settings);
    Settings loaded = loadSettings();
    assert(loaded.configured());
    assert(loaded.hotkey == L"Ctrl+F1");
    assert(loaded.retroArchNetworkControl);
    assert(loaded.retroArchNetworkPort == 55355);
    assert(loaded.gestures.at("swipe-left") == "box_art");
    assert(!loaded.autostart);

    // Long press keeps the touch menu by default and is sent to the Pi with the swipes.
    assert(loaded.gestures.at("long-press") == "touch_menu");
    auto payload = parse(gesturePayload(loaded));
    assert(payload.get("long-press").value() == "touch_menu");
    assert(payload.get("swipe-left").value() == "box_art");

    // The menu can move to a swipe while long press takes another action.
    settings.gestures["swipe-down"] = "touch_menu";
    settings.gestures["long-press"] = "retroarch_menu";
    saveSettings(settings);
    loaded = loadSettings();
    assert(loaded.gestures.at("swipe-down") == "touch_menu");
    assert(loaded.gestures.at("long-press") == "retroarch_menu");
    assert(parse(gesturePayload(loaded)).get("long-press").value() == "retroarch_menu");

    // An INI written before long press became configurable still opens the menu.
    std::string ini = readFile(settingsPath());
    size_t start = ini.find("LongPress=");
    assert(start != std::string::npos);
    ini.erase(start, ini.find('\n', start) - start + 1);
    writeFile(settingsPath(), ini);
    assert(loadSettings().gestures.at("long-press") == "touch_menu");

    // Language: follows Windows by default, persists an explicit choice, ignores junk.
    assert(loadSettings().language == "auto");
    settings.language = "de";
    saveSettings(settings);
    assert(loadSettings().language == "de");
    assert(languagePayload(loadSettings()) == R"({"language":"de"})");
    settings.language = "en";
    saveSettings(settings);
    assert(loadSettings().language == "en");
    assert(languagePayload(loadSettings()) == R"({"language":"en"})");
    ini = readFile(settingsPath());
    start = ini.find("Language=en");
    assert(start != std::string::npos);
    ini.replace(start, 11, "Language=klingon");
    writeFile(settingsPath(), ini);
    assert(loadSettings().language == "auto");
    assert(resolveLanguage("de") == "de" && resolveLanguage("en") == "en");
    assert(resolveLanguage("auto") == "de" || resolveLanguage("auto") == "en");
    assert(resolveLanguage("klingon") == resolveLanguage("auto"));

    // Both string tables are complete, and the English one really is English.
    for (int i = 0; i < int(Str::Count); ++i) {
        setUiLanguage("en");
        const std::wstring english = tr(Str(i));
        setUiLanguage("de");
        const std::wstring german = tr(Str(i));
        assert(!english.empty() && !german.empty());
        for (wchar_t c : english) assert(c != L'ä' && c != L'ö' && c != L'ü' && c != L'ß');
    }
    setUiLanguage("en");
    assert(std::wstring(tr(Str::TabMedia)) == L"Media");
    setUiLanguage("de");
    assert(std::wstring(tr(Str::TabMedia)) == L"Medien");
    assert(std::wstring(trAt(Str::TabConnection, 4)) == L"Allgemein");
    setUiLanguage("en");

    // Thumbnails: images fill a fixed-size DIB, broken or missing files yield nullptr.
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const char* pngBase64 = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+/lZkAAAAASUVORK5CYII=";
    BYTE png[128]; DWORD pngSize = sizeof(png);
    assert(CryptStringToBinaryA(pngBase64, 0, CRYPT_STRING_BASE64, png, &pngSize, nullptr, nullptr));
    std::wstring imagePath = dataDirectory() + L"\\thumb-test.png";
    writeFile(imagePath, std::string(reinterpret_cast<char*>(png), pngSize));
    HBITMAP thumb = createThumbnail(imagePath, 112, 70);
    assert(thumb);
    BITMAP info{};
    GetObjectW(thumb, sizeof(info), &info);
    assert(info.bmWidth == 112 && info.bmHeight == 70 && info.bmBitsPixel == 32);
    DeleteObject(thumb);
    writeFile(dataDirectory() + L"\\thumb-broken.png", "not an image");
    assert(!createThumbnail(dataDirectory() + L"\\thumb-broken.png", 112, 70));
    assert(!createThumbnail(dataDirectory() + L"\\missing.png", 112, 70));
    assert(!createThumbnail(dataDirectory() + L"\\missing.mp4", 112, 70));
    assert(!createThumbnail(imagePath, 0, 70));

    // Artwork wider than 1600 px is scaled down before it is sent (regression: the
    // encoder was created from a CLSID instead of a container GUID, every scaled
    // image failed and was silently dropped, so large marquees never reached the Pi).
    {
        const std::wstring bigPng = dataDirectory() + L"\\big-marquee.png";
        const std::wstring bigJpg = dataDirectory() + L"\\big-logo.jpg";
        assert(writeTestImage(bigPng, GUID_ContainerFormatPng, 2000, 800));
        assert(writeTestImage(bigJpg, GUID_ContainerFormatJpeg, 2000, 800));
        GameMessage game;
        game.action = "game";
        game.title = L"Test";
        game.marquee = bigPng;
        game.logo = bigJpg;
        auto body = parse(gamePayload(game));
        assert(takeArtworkErrors().empty());  // nothing was dropped
        const std::string png = decodeBase64(body.get("marquee").get("base64").value());
        assert(png.size() > 24 && png.compare(1, 3, "PNG") == 0);
        const auto beU32 = [&](size_t at) {
            return (uint32_t(uint8_t(png[at])) << 24) | (uint32_t(uint8_t(png[at + 1])) << 16) |
                   (uint32_t(uint8_t(png[at + 2])) << 8) | uint32_t(uint8_t(png[at + 3]));
        };
        assert(beU32(16) == 1600 && beU32(20) == 640);  // IHDR width and height after scaling
        const std::string jpg = decodeBase64(body.get("logo").get("base64").value());
        assert(jpg.size() > 4 && uint8_t(jpg[0]) == 0xFF && uint8_t(jpg[1]) == 0xD8);
        const std::wstring scaledJpg = dataDirectory() + L"\\scaled-logo.jpg";
        writeFile(scaledJpg, jpg);
        HBITMAP scaled = createThumbnail(scaledJpg, 112, 70);
        assert(scaled);
        DeleteObject(scaled);
    }

    // Video thumbnails must keep orientation and row alignment (regression: decoder
    // buffers are padded to 16, which once sheared and flipped the picture).
    const std::wstring videoPath = dataDirectory() + L"\\thumb-test.mp4";
    if (writeTestVideo(videoPath.c_str())) {
        HBITMAP frame = createThumbnail(videoPath, 112, 70);
        assert(frame);
        const uint32_t top = pixelAt(frame, 60, 25), bottom = pixelAt(frame, 60, 75), marker = pixelAt(frame, 8, 50);
        assert(isDominant(top, 16));     // red on top
        assert(isDominant(bottom, 0));   // blue below
        assert(isDominant(marker, 8));   // green bar stays vertical
        DeleteObject(frame);
    } else {
        std::cout << "video thumbnail test skipped (no H.264 encoder)\n";
    }
    std::cout << "native tests passed\n";
}
