#include "strings.hpp"
#include <windows.h>
#include <atomic>

namespace {

struct Entry { const wchar_t* en; const wchar_t* de; };

const Entry TABLE[] = {
#define MARQUEE_STRING_ENTRY(id, en, de) {en, de},
    MARQUEE_STRINGS(MARQUEE_STRING_ENTRY)
#undef MARQUEE_STRING_ENTRY
};
static_assert(sizeof(TABLE) / sizeof(TABLE[0]) == size_t(Str::Count), "string table out of sync");

// tr() is also called from the polling thread.
std::atomic<bool> german{false};

}  // namespace

std::string resolveLanguage(const std::string& setting) {
    if (setting == "de" || setting == "en") return setting;
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_GERMAN ? "de" : "en";
}

void setUiLanguage(const std::string& resolved) { german = resolved == "de"; }

const wchar_t* tr(Str id) {
    const size_t index = size_t(id);
    if (index >= size_t(Str::Count)) return L"";
    return german ? TABLE[index].de : TABLE[index].en;
}

const wchar_t* trLang(Str id, bool german) {
    const size_t index = size_t(id);
    if (index >= size_t(Str::Count)) return L"";
    return german ? TABLE[index].de : TABLE[index].en;
}
