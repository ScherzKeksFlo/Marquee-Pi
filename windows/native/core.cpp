#include <winsock2.h>
#include <ws2tcpip.h>
#include "core.hpp"
#include <winhttp.h>
#include <shlobj.h>
#include <wincrypt.h>
#include <wincodec.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <mutex>
#include <cwctype>
#include <cmath>

namespace fs = std::filesystem;
namespace {
template <typename T> struct ComPtr {
    T* ptr = nullptr;
    ~ComPtr() { if (ptr) ptr->Release(); }
    T** out() { return &ptr; }
    T* operator->() const { return ptr; }
};
struct ComScope {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~ComScope() { if (result == S_OK || result == S_FALSE) CoUninitialize(); }
    bool available() const { return SUCCEEDED(result) || result == RPC_E_CHANGED_MODE; }
};
struct WinHandle {
    HINTERNET handle = nullptr;
    explicit WinHandle(HINTERNET h) : handle(h) {}
    ~WinHandle() { if (handle) WinHttpCloseHandle(handle); }
    operator HINTERNET() const { return handle; }
};
struct SharedHttp {
    HINTERNET session = nullptr;
    std::mutex mutex;
    std::map<std::pair<std::wstring, INTERNET_PORT>, HINTERNET> connections;
    SharedHttp() {
        session = WinHttpOpen(L"Marquee-Pi/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (session) WinHttpSetTimeouts(session, 3000, 3000, 8000, 8000);
    }
    ~SharedHttp() {
        for (const auto& item : connections) WinHttpCloseHandle(item.second);
        if (session) WinHttpCloseHandle(session);
    }
    HINTERNET connection(const std::wstring& host, INTERNET_PORT port) {
        if (!session) throw std::runtime_error("WinHTTP initialization failed");
        std::lock_guard<std::mutex> guard(mutex);
        auto key = std::make_pair(host, port);
        auto existing = connections.find(key);
        if (existing != connections.end()) return existing->second;
        HINTERNET handle = WinHttpConnect(session, host.c_str(), port, 0);
        if (!handle) throw std::runtime_error("Pi connection failed");
        connections.emplace(std::move(key), handle);
        return handle;
    }
};
SharedHttp& httpClient() {
    static SharedHttp client;
    return client;
}
std::string trim(std::string s) {
    const char* chars = " \t\r\n";
    size_t first = s.find_first_not_of(chars);
    if (first == std::string::npos) return "";
    return s.substr(first, s.find_last_not_of(chars) - first + 1);
}
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}
std::wstring lowerW(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return std::towlower(c); });
    return s;
}
std::wstring folder(REFKNOWNFOLDERID id) {
    PWSTR ptr = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &ptr))) throw std::runtime_error("Windows profile folder unavailable");
    std::wstring result = ptr;
    CoTaskMemFree(ptr);
    return result;
}
std::string line(const std::wstring& s) {
    std::string value = toUtf8(s);
    value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
    value.erase(std::remove(value.begin(), value.end(), '\n'), value.end());
    return value;
}
std::wstring filename(const std::wstring& path) { return fs::path(path).filename().wstring(); }
std::wstring modulePath() {
    std::wstring path(32768, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
    if (!n || n >= path.size()) throw std::runtime_error("Executable path unavailable");
    path.resize(n);
    return path;
}
std::wstring startupLink() { return folder(FOLDERID_Startup) + L"\\Marquee-Pi.lnk"; }
std::wstring oldStartupLink() { return folder(FOLDERID_Startup) + L"\\ArcadePiDisplay.lnk"; }
bool portableMode() { return fileExists((fs::path(modulePath()).parent_path() / L"portable.flag").wstring()); }
std::wstring oldDataDirectory() { return folder(FOLDERID_LocalAppData) + L"\\ArcadePiDisplay"; }
std::vector<BYTE> hotkeyKeys(const std::wstring& text) {
    std::vector<std::wstring> parts;
    size_t start = 0;
    while (true) {
        size_t end = text.find(L'+', start);
        std::wstring part = lowerW(text.substr(start, end == std::wstring::npos ? end : end - start));
        size_t l = part.find_first_not_of(L" \t"), r = part.find_last_not_of(L" \t");
        if (l == std::wstring::npos) return {};
        parts.push_back(part.substr(l, r - l + 1));
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    if (parts.empty() || parts.size() > 5) return {};
    std::vector<BYTE> keys;
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        const auto& p = parts[i];
        BYTE key = p == L"ctrl" || p == L"control" || p == L"strg" ? VK_CONTROL :
                   p == L"shift" || p == L"umschalt" ? VK_SHIFT :
                   p == L"alt" ? VK_MENU : p == L"win" || p == L"windows" ? VK_LWIN : 0;
        if (!key || std::find(keys.begin(), keys.end(), key) != keys.end()) return {};
        keys.push_back(key);
    }
    std::wstring p = parts.back();
    BYTE key = 0;
    if (p.size() == 1 && ((p[0] >= L'a' && p[0] <= L'z') || (p[0] >= L'0' && p[0] <= L'9')))
        key = BYTE(std::towupper(p[0]));
    else if (p.size() >= 2 && p[0] == L'f') {
        try { int n = std::stoi(p.substr(1)); if (n >= 1 && n <= 24) key = BYTE(VK_F1 + n - 1); }
        catch (...) {}
    } else {
        static const std::map<std::wstring, BYTE> named = {
            {L"enter", VK_RETURN}, {L"return", VK_RETURN}, {L"space", VK_SPACE},
            {L"tab", VK_TAB}, {L"esc", VK_ESCAPE}, {L"escape", VK_ESCAPE},
            {L"left", VK_LEFT}, {L"right", VK_RIGHT}, {L"up", VK_UP}, {L"down", VK_DOWN},
            {L"back", VK_BACK}, {L"backspace", VK_BACK}, {L"delete", VK_DELETE},
            {L"insert", VK_INSERT}, {L"home", VK_HOME}, {L"end", VK_END},
            {L"pageup", VK_PRIOR}, {L"pagedown", VK_NEXT}
        };
        auto it = named.find(p);
        if (it != named.end()) key = it->second;
    }
    if (!key) return {};
    keys.push_back(key);
    return keys;
}
std::string base64(const std::string& data) {
    if (data.empty()) return "";
    DWORD size = 0;
    DWORD flags = CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF;
    if (!CryptBinaryToStringA((const BYTE*)data.data(), DWORD(data.size()), flags, nullptr, &size))
        throw std::runtime_error("Base64 size failed");
    std::string result(size, '\0');
    if (!CryptBinaryToStringA((const BYTE*)data.data(), DWORD(data.size()), flags, result.data(), &size))
        throw std::runtime_error("Base64 encoding failed");
    result.resize(size);
    while (!result.empty() && result.back() == '\0') result.pop_back();
    return result;
}
std::string scaledArtwork(const std::wstring& path, const std::string& ext, const std::string& original) {
    ComScope com;
    if (!com.available()) return original;
    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(factory.out())))) return original;
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                  WICDecodeMetadataCacheOnLoad, decoder.out()))) return original;
    ComPtr<IWICBitmapFrameDecode> source;
    if (FAILED(decoder->GetFrame(0, source.out()))) return original;
    UINT width = 0, height = 0;
    if (FAILED(source->GetSize(&width, &height)) || !width || !height || std::max(width, height) <= 1600) return original;
    const double scale = 1600.0 / double(std::max(width, height));
    const UINT scaledWidth = std::max(1u, UINT(std::lround(width * scale)));
    const UINT scaledHeight = std::max(1u, UINT(std::lround(height * scale)));
    ComPtr<IWICBitmapScaler> scaler;
    if (FAILED(factory->CreateBitmapScaler(scaler.out())) ||
        FAILED(scaler->Initialize(source.ptr, scaledWidth, scaledHeight, WICBitmapInterpolationModeFant)))
        throw std::runtime_error("Artwork scaling failed");
    ComPtr<IStream> stream;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, stream.out()))) throw std::runtime_error("Artwork stream failed");
    ComPtr<IWICBitmapEncoder> encoder;
    const CLSID encoderId = ext == ".png" ? CLSID_WICPngEncoder : CLSID_WICJpegEncoder;
    if (FAILED(factory->CreateEncoder(encoderId, nullptr, encoder.out())) ||
        FAILED(encoder->Initialize(stream.ptr, WICBitmapEncoderNoCache)))
        throw std::runtime_error("Artwork encoder failed");
    ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(encoder->CreateNewFrame(frame.out(), nullptr)) || FAILED(frame->Initialize(nullptr)) ||
        FAILED(frame->SetSize(scaledWidth, scaledHeight))) throw std::runtime_error("Artwork frame failed");
    WICPixelFormatGUID format = ext == ".png" ? GUID_WICPixelFormat32bppBGRA : GUID_WICPixelFormat24bppBGR;
    if (FAILED(frame->SetPixelFormat(&format))) throw std::runtime_error("Artwork pixel format failed");
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(converter.out())) ||
        FAILED(converter->Initialize(scaler.ptr, format, WICBitmapDitherTypeNone, nullptr, 0,
                                     WICBitmapPaletteTypeCustom)) ||
        FAILED(frame->WriteSource(converter.ptr, nullptr)) || FAILED(frame->Commit()) || FAILED(encoder->Commit()))
        throw std::runtime_error("Artwork encoding failed");
    HGLOBAL memory = nullptr;
    STATSTG stats{};
    if (FAILED(stream->Stat(&stats, STATFLAG_NONAME)) || stats.cbSize.HighPart || !stats.cbSize.LowPart)
        throw std::runtime_error("Artwork size unavailable");
    if (FAILED(GetHGlobalFromStream(stream.ptr, &memory)) || !memory) throw std::runtime_error("Artwork memory unavailable");
    const SIZE_T size = stats.cbSize.LowPart;
    const void* bytes = GlobalLock(memory);
    if (!bytes) throw std::runtime_error("Artwork memory unavailable");
    std::string result(static_cast<const char*>(bytes), static_cast<const char*>(bytes) + size);
    GlobalUnlock(memory);
    return result;
}
mini::Json artwork(const std::wstring& path, size_t& budget) {
    if (path.empty() || !fileExists(path)) return {};
    std::string ext = lower(toUtf8(fs::path(path).extension().wstring()));
    if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".gif" && ext != ".webp") return {};
    try {
        auto length = fs::file_size(fs::path(path));
        if (length > 20 * 1024 * 1024 || length > budget) return {};
        std::string bytes = readFile(path);
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") bytes = scaledArtwork(path, ext, bytes);
        if (bytes.size() > 20 * 1024 * 1024 || bytes.size() > budget) return {};
        budget -= bytes.size();
        mini::Json item = mini::Json::object();
        item["extension"] = mini::Json::str(ext);
        item["base64"] = mini::Json::str(base64(bytes));
        return item;
    } catch (...) { return {}; }
}
}
std::wstring fromUtf8(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0);
    if (!n) throw std::runtime_error("Invalid UTF-8");
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), out.data(), n);
    return out;
}
std::string toUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0, nullptr, nullptr);
    if (!n) throw std::runtime_error("UTF-8 conversion failed");
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}
std::wstring dataDirectory() {
    return portableMode() ? (fs::path(modulePath()).parent_path() / L"Data").wstring() :
           folder(FOLDERID_LocalAppData) + L"\\Marquee-Pi";
}
std::wstring settingsPath() { return dataDirectory() + L"\\settings.ini"; }
std::wstring mediaDirectory() { return dataDirectory() + L"\\media"; }
bool fileExists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
std::string readFile(const std::wstring& path) {
    std::ifstream stream(fs::path(path), std::ios::binary);
    if (!stream) throw std::runtime_error("File cannot be read");
    return std::string(std::istreambuf_iterator<char>(stream), {});
}
void writeFile(const std::wstring& path, const std::string& data) {
    std::ofstream stream(fs::path(path), std::ios::binary | std::ios::trunc);
    if (!stream) throw std::runtime_error("File cannot be written");
    stream.write(data.data(), std::streamsize(data.size()));
    if (!stream) throw std::runtime_error("File write failed");
}
bool autostartEnabled() {
    HKEY key = nullptr;
    bool registry = false;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        registry = RegQueryValueExW(key, L"MarqueePi", nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS ||
                   RegQueryValueExW(key, L"ArcadePiDisplay", nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
        RegCloseKey(key);
    }
    return registry || fileExists(startupLink()) || fileExists(oldStartupLink());
}
void setAutostart(bool enabled) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                        0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        throw std::runtime_error("Autostart registry access failed");
    if (enabled) {
        std::wstring path = modulePath();
        if (lowerW(filename(path)) != L"marquee-pi.exe") {
            RegCloseKey(key);
            throw std::runtime_error("Autostart requires the installed Marquee-Pi.exe");
        }
        path = L"\"" + path + L"\"";
        LONG result = RegSetValueExW(key, L"MarqueePi", 0, REG_SZ,
                                     (const BYTE*)path.c_str(), DWORD((path.size() + 1) * sizeof(wchar_t)));
        if (result != ERROR_SUCCESS) {
            RegCloseKey(key);
            throw std::runtime_error("Autostart registry write failed");
        }
    } else RegDeleteValueW(key, L"MarqueePi");
    RegDeleteValueW(key, L"ArcadePiDisplay");
    RegCloseKey(key);
    DeleteFileW(startupLink().c_str());
    DeleteFileW(oldStartupLink().c_str());
}bool Settings::configured() const {
    if (token.size() < 24 || piUrl.rfind(L"http://", 0) != 0) return false;
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = DWORD(-1);
    return WinHttpCrackUrl(piUrl.c_str(), DWORD(piUrl.size()), 0, &parts) &&
           parts.nScheme == INTERNET_SCHEME_HTTP && parts.dwHostNameLength > 0;
}
Settings loadSettings() {
    Settings s;
    std::wstring ini = settingsPath();
    if (!fileExists(ini) && !portableMode()) {
        std::wstring old = oldDataDirectory();
        if (fileExists(old + L"\\settings.ini")) {
            fs::create_directories(fs::path(dataDirectory()));
            CopyFileW((old + L"\\settings.ini").c_str(), ini.c_str(), TRUE);
            if (fileExists(old + L"\\active-media.txt"))
                CopyFileW((old + L"\\active-media.txt").c_str(),
                          (dataDirectory() + L"\\active-media.txt").c_str(), TRUE);
            if (fs::exists(fs::path(old + L"\\media"))) {
                // Migrate contents individually; copy() rejects an existing destination directory.
                try {
                    const fs::path source(old + L"\\media");
                    const fs::path target(mediaDirectory());
                    fs::create_directories(target);
                    for (const auto& entry : fs::recursive_directory_iterator(source)) {
                        const fs::path dest = target / fs::relative(entry.path(), source);
                        if (entry.is_directory()) fs::create_directories(dest);
                        else if (entry.is_regular_file()) {
                            fs::create_directories(dest.parent_path());
                            fs::copy_file(entry.path(), dest, fs::copy_options::skip_existing);
                        }
                    }
                } catch (const fs::filesystem_error&) {
                    // A locked media file must not prevent the tray from starting.
                }
            }
        }
    }
    if (!fileExists(ini)) {
        if (!portableMode()) {
            try {
                mini::Json old = mini::parse(readFile(oldDataDirectory() + L"\\settings.json"));
                s.piUrl = fromUtf8(old.get("PiUrl").value());
                s.token = fromUtf8(old.get("Token").value());
                s.hotkey = fromUtf8(old.get("RetroArchMenuHotkey").value("F1"));
                for (const auto& pair : old.get("GestureActions").members)
                    s.gestures[pair.first] = pair.second.value();
            } catch (...) {}
            s.autostart = autostartEnabled();
        }
        saveSettings(s);
        return s;
    }    std::string data = readFile(ini);
    if (data.rfind("\xef\xbb\xbf", 0) == 0) data.erase(0, 3);
    std::istringstream in(data);
    std::string row, section;
    while (std::getline(in, row)) {
        row = trim(row);
        if (row.empty() || row[0] == ';' || row[0] == '#') continue;
        if (row.front() == '[' && row.back() == ']') { section = lower(trim(row.substr(1, row.size() - 2))); continue; }
        size_t eq = row.find('=');
        if (eq == std::string::npos) continue;
        std::string key = lower(trim(row.substr(0, eq))), value = trim(row.substr(eq + 1));
        if (section == "connection") {
            if (key == "piurl") s.piUrl = fromUtf8(value);
            else if (key == "token") s.token = fromUtf8(value);
        } else if (section == "gestures") {
            if (key == "retroarchmenuhotkey") s.hotkey = fromUtf8(value);
            else if (key == "retroarchmenumode") s.retroArchNetworkControl = lower(value) == "network";
            else if (key == "retroarchnetworkport") {
                try { int port = std::stoi(value); if (port > 0 && port <= 65535) s.retroArchNetworkPort = port; }
                catch (...) {}
            }
            else {
                static const std::map<std::string, std::string> names = {
                    {"swipedown", "swipe-down"}, {"swipeup", "swipe-up"},
                    {"swiperight", "swipe-right"}, {"swipeleft", "swipe-left"}
                };
                auto it = names.find(key);
                if (it != names.end()) s.gestures[it->second] = value;
            }
        } else if (section == "general" && key == "startwithwindows")
            s.autostart = lower(value) == "true";
    }
    return s;
}
void saveSettings(const Settings& s) {
    fs::create_directories(fs::path(dataDirectory()));
    auto gesture = [&](const char* key) {
        auto it = s.gestures.find(key); return it == s.gestures.end() ? std::string("none") : it->second;
    };
    std::string data = "; Marquee-Pi - use 'Einstellungen neu laden' after manual edits.\r\n"
                       "[Connection]\r\nPiUrl=" + line(s.piUrl) + "\r\nToken=" + line(s.token) +
                       "\r\n\r\n[Gestures]\r\nSwipeDown=" + gesture("swipe-down") +
                       "\r\nSwipeUp=" + gesture("swipe-up") +
                       "\r\nSwipeRight=" + gesture("swipe-right") +
                       "\r\nSwipeLeft=" + gesture("swipe-left") +
                       "\r\nRetroArchMenuHotkey=" + line(s.hotkey) +
                       "\r\nRetroArchMenuMode=" + std::string(s.retroArchNetworkControl ? "network" : "keyboard") +
                       "\r\nRetroArchNetworkPort=" + std::to_string(s.retroArchNetworkPort) +
                       "\r\n\r\n[General]\r\nStartWithWindows=" +
                       std::string(s.autostart ? "true" : "false") + "\r\n";
    std::wstring temp = settingsPath() + L".tmp";
    writeFile(temp, data);
    if (!MoveFileExW(temp.c_str(), settingsPath().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("INI replacement failed");
}
bool validHotkey(const std::wstring& text) { return !hotkeyKeys(text).empty(); }
bool isUser32ShutdownEvent(const std::wstring& xml) {
    return xml.find(L"<Provider Name='User32'") != std::wstring::npos ||
           xml.find(L"<Provider Name=\"User32\"") != std::wstring::npos ||
           xml.find(L"<Provider Name='Microsoft-Windows-User32'") != std::wstring::npos ||
           xml.find(L"<Provider Name=\"Microsoft-Windows-User32\"") != std::wstring::npos;
}
bool isPowerOffType(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return std::towlower(c); });
    const size_t first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return false;
    value = value.substr(first, value.find_last_not_of(L" \t\r\n") - first + 1);
    static const std::vector<std::wstring> restartTerms = {
        L"reboot", L"restart", L"neu starten", L"neustart", L"redémarrer", L"redemarrer",
        L"reiniciar", L"riavvia", L"herstarten", L"перезагрузка"
    };
    for (const auto& term : restartTerms)
        if (value.find(term) != std::wstring::npos) return false;
    return value == L"shutdown" || value == L"power off" ||
           value == L"herunterfahren" || value == L"ausschalten";
}
bool sendRetroArchHotkey(const std::wstring& text) {
    auto keys = hotkeyKeys(text);
    if (keys.empty()) return false;
    HWND window = GetForegroundWindow();
    if (!window) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (!pid) return false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    std::wstring path(32768, L'\0');
    DWORD n = DWORD(path.size());
    BOOL ok = QueryFullProcessImageNameW(process, 0, path.data(), &n);
    CloseHandle(process);
    if (!ok) return false;
    std::wstring name = lowerW(filename(path.substr(0, n)));
    if (name != L"retroarch.exe") return false;
    std::vector<INPUT> press, release;
    for (BYTE key : keys) { INPUT i{}; i.type = INPUT_KEYBOARD; i.ki.wVk = key; press.push_back(i); }
    for (auto it = keys.rbegin(); it != keys.rend(); ++it) {
        INPUT i{}; i.type = INPUT_KEYBOARD; i.ki.wVk = *it; i.ki.dwFlags = KEYEVENTF_KEYUP; release.push_back(i);
    }
    UINT pressedCount = SendInput(UINT(press.size()), press.data(), sizeof(INPUT));
    if (pressedCount == press.size()) Sleep(120);
    UINT releasedCount = SendInput(UINT(release.size()), release.data(), sizeof(INPUT));
    return pressedCount == press.size() && releasedCount == release.size();
}
bool sendRetroArchNetworkCommand(int port) {
    if (port < 1 || port > 65535) return false;
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
    SOCKET socketHandle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socketHandle == INVALID_SOCKET) {
        WSACleanup(); return false;
    }
    DWORD timeout = 500;
    setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<u_short>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    constexpr char probe[] = "VERSION";
    bool ready = sendto(socketHandle, probe, sizeof(probe) - 1, 0,
                        reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == sizeof(probe) - 1;
    char reply[256]{};
    if (ready) ready = recvfrom(socketHandle, reply, sizeof(reply), 0, nullptr, nullptr) > 0;
    bool sent = false;
    if (ready) {
        constexpr char command[] = "MENU_TOGGLE";
        sent = sendto(socketHandle, command, sizeof(command) - 1, 0,
                      reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == sizeof(command) - 1;
    }
    closesocket(socketHandle);
    WSACleanup();
    return sent;
}
HttpResult piRequest(const Settings& s, const std::wstring& method, const std::wstring& path,
                     const std::string& body, const std::wstring& contentType,
                     const std::wstring& extraHeader, int timeoutMs) {
    if (!s.configured()) throw std::runtime_error("Pi-Adresse und Token zuerst einrichten");
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = DWORD(-1);
    if (!WinHttpCrackUrl(s.piUrl.c_str(), DWORD(s.piUrl.size()), 0, &parts) ||
        parts.nScheme != INTERNET_SCHEME_HTTP)
        throw std::runtime_error("Invalid Pi URL");
    std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring base = parts.lpszUrlPath ? std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) : L"";
    while (!base.empty() && base.back() == L'/') base.pop_back();
    HINTERNET connection = httpClient().connection(host, parts.nPort);
    std::wstring target = base + path;
    WinHandle request(WinHttpOpenRequest(connection, method.c_str(), target.c_str(), nullptr,
                                          WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0));
    if (!request) throw std::runtime_error("Pi request failed");
    WinHttpSetTimeouts(request, 3000, 3000, timeoutMs, timeoutMs);
    std::wstring headers = L"X-Arcade-Token: " + s.token + L"\r\n" + extraHeader;
    if (!contentType.empty()) headers += L"Content-Type: " + contentType + L"\r\n";
    if (!WinHttpSendRequest(request, headers.c_str(), DWORD(-1),
                            body.empty() ? nullptr : (void*)body.data(), DWORD(body.size()),
                            DWORD(body.size()), 0) || !WinHttpReceiveResponse(request, nullptr))
        throw std::runtime_error("Pi did not respond");
    HttpResult result;
    DWORD size = sizeof(result.status);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                              WINHTTP_HEADER_NAME_BY_INDEX, &result.status, &size, WINHTTP_NO_HEADER_INDEX))
        throw std::runtime_error("Pi response status unavailable");
    while (true) {
        DWORD available = 0, read = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) throw std::runtime_error("Pi response read failed");
        if (!available) break;
        if (result.body.size() + available > 1024 * 1024) throw std::runtime_error("Pi response too large");
        size_t start = result.body.size();
        result.body.resize(start + available);
        if (!WinHttpReadData(request, result.body.data() + start, available, &read))
            throw std::runtime_error("Pi response read failed");
        result.body.resize(start + read);
    }
    if (result.status < 200 || result.status >= 300)
        throw HttpError(result.status, "Pi HTTP " + std::to_string(result.status) + ": " + result.body);
    return result;
}
std::string gamePayload(const GameMessage& game) {
    size_t budget = MARQUEE_PI_ARTWORK_BUDGET_BYTES;
    mini::Json body = mini::Json::object();
    body["title"] = mini::Json::str(toUtf8(game.title.empty() ? L"Game" : game.title));
    body["marquee"] = artwork(game.marquee, budget);
    body["controls"] = artwork(game.controls, budget);
    body["box_art"] = artwork(game.boxArt, budget);
    body["logo"] = artwork(game.logo, budget);
    return body.dump();
}
std::vector<std::string> gameWarnings(const std::string& response) {
    std::vector<std::string> result;
    mini::Json body = mini::parse(response);
    const auto& warnings = body.get("warnings");
    if (warnings.type != mini::Json::Array) return result;
    for (const auto& warning : warnings.items) {
        if (warning.type != mini::Json::Object) continue;
        std::string kind = warning.get("kind").value();
        std::string error = warning.get("error").value();
        if (kind.empty()) continue;
        result.push_back(error.empty() ? kind : kind + ": " + error);
    }
    return result;
}
std::string gesturePayload(const Settings& s) {
    mini::Json body = mini::Json::object();
    for (const auto& pair : s.gestures) body[pair.first] = mini::Json::str(pair.second);
    return body.dump();
}
GameMessage parseGameMessage(const std::string& json) {
    mini::Json item = mini::parse(json);
    if (item.type != mini::Json::Object) throw std::runtime_error("Invalid game event");
    auto get = [&](const char* key) { return fromUtf8(item.get(key).value()); };
    GameMessage game;
    game.action = item.get("Action").value();
    if (game.action.empty()) game.action = item.get("action").value();
    game.title = get("Title");
    game.marquee = get("MarqueePath");
    game.controls = get("ControlsPath");
    game.boxArt = get("BoxArtPath");
    game.logo = get("LogoPath");
    return game;
}
std::wstring errorText(const std::exception& error) {
    try { return fromUtf8(error.what()); } catch (...) { return L"Unknown error"; }
}
