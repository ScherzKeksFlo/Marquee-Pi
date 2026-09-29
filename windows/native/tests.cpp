#include "core.hpp"
#include "strings.hpp"
#include "thumbnail.hpp"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wincodec.h>
#include <cassert>
#include <cstdint>
#include <filesystem>
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

int main() {
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
