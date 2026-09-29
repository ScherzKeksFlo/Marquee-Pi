#include "core.hpp"
#include "thumbnail.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
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
    std::cout << "native tests passed\n";
}
