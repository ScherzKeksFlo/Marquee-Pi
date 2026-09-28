#pragma once
#include <cctype>
#include <cstdint>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace mini {
struct Json {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool boolean = false;
    int64_t number = 0;
    double decimal = 0;
    bool integral = false;
    std::string number_text;
    std::string text;
    std::vector<Json> items;
    std::map<std::string, Json> members;

    static Json object() { Json j; j.type = Object; return j; }
    static Json array() { Json j; j.type = Array; return j; }
    static Json str(const std::string& s) { Json j; j.type = String; j.text = s; return j; }
    static Json num(int64_t n) {
        Json j; j.type = Number; j.number = n; j.decimal = double(n); j.integral = true;
        j.number_text = std::to_string(n); return j;
    }
    static Json num(const std::string& source) {
        Json j; j.type = Number; j.number_text = source; j.decimal = std::stod(source);
        if (!std::isfinite(j.decimal)) throw std::runtime_error("JSON number out of range");
        if (source.find_first_of(".eE") == std::string::npos) {
            j.number = std::stoll(source); j.integral = true;
        }
        return j;
    }
    static Json flag(bool b) { Json j; j.type = Bool; j.boolean = b; return j; }
    const Json& get(const std::string& key) const {
        static const Json empty;
        auto it = members.find(key);
        return it == members.end() ? empty : it->second;
    }
    Json& operator[](const std::string& key) { type = Object; return members[key]; }
    std::string value(const std::string& fallback = "") const { return type == String ? text : fallback; }
    int64_t integer(int64_t fallback = 0) const { return type == Number && integral ? number : fallback; }
    double real(double fallback = 0) const { return type == Number ? decimal : fallback; }
    bool is_null() const { return type == Null; }

    static void utf8(std::string& out, uint32_t cp) {
        if (cp <= 0x7f) out.push_back(char(cp));
        else if (cp <= 0x7ff) { out.push_back(char(0xc0 | (cp >> 6))); out.push_back(char(0x80 | (cp & 63))); }
        else if (cp <= 0xffff) {
            out.push_back(char(0xe0 | (cp >> 12))); out.push_back(char(0x80 | ((cp >> 6) & 63)));
            out.push_back(char(0x80 | (cp & 63)));
        } else {
            out.push_back(char(0xf0 | (cp >> 18))); out.push_back(char(0x80 | ((cp >> 12) & 63)));
            out.push_back(char(0x80 | ((cp >> 6) & 63))); out.push_back(char(0x80 | (cp & 63)));
        }
    }
    static std::string quote(const std::string& s) {
        static const char* hex = "0123456789abcdef";
        std::string out = "\"";
        for (unsigned char c : s) {
            if (c == '"' || c == '\\') { out.push_back('\\'); out.push_back(char(c)); }
            else if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else if (c == '\t') out += "\\t";
            else if (c < 32) { out += "\\u00"; out.push_back(hex[c >> 4]); out.push_back(hex[c & 15]); }
            else out.push_back(char(c));
        }
        return out + "\"";
    }
    std::string dump() const {
        switch (type) {
        case Null: return "null";
        case Bool: return boolean ? "true" : "false";
        case Number: return number_text.empty() ? std::to_string(number) : number_text;
        case String: return quote(text);
        case Array: {
            std::string out = "[";
            for (size_t i = 0; i < items.size(); ++i) { if (i) out += ","; out += items[i].dump(); }
            return out + "]";
        }
        case Object: {
            std::string out = "{";
            bool first = true;
            for (const auto& pair : members) {
                if (!first) out += ",";
                first = false;
                out += quote(pair.first) + ":" + pair.second.dump();
            }
            return out + "}";
        }
        }
        return "null";
    }
};

class Parser {
    const std::string& source;
    size_t pos = 0;
    void ws() { while (pos < source.size() && std::isspace((unsigned char)source[pos])) ++pos; }
    char take() { if (pos >= source.size()) throw std::runtime_error("JSON ended unexpectedly"); return source[pos++]; }
    void expect(char c) { if (take() != c) throw std::runtime_error("Invalid JSON"); }
    uint32_t hex4() {
        uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            char c = take();
            value <<= 4;
            if (c >= '0' && c <= '9') value |= c - '0';
            else if (c >= 'a' && c <= 'f') value |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') value |= c - 'A' + 10;
            else throw std::runtime_error("Invalid JSON unicode escape");
        }
        return value;
    }
    std::string string() {
        expect('"');
        std::string out;
        while (true) {
            char c = take();
            if (c == '"') break;
            if ((unsigned char)c < 32) throw std::runtime_error("Invalid JSON string");
            if (c != '\\') { out.push_back(c); continue; }
            switch (c = take()) {
            case '"': case '\\': case '/': out.push_back(c); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                uint32_t cp = hex4();
                if (cp >= 0xd800 && cp <= 0xdbff) {
                    expect('\\'); expect('u');
                    uint32_t low = hex4();
                    if (low < 0xdc00 || low > 0xdfff) throw std::runtime_error("Invalid JSON surrogate");
                    cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                } else if (cp >= 0xdc00 && cp <= 0xdfff) throw std::runtime_error("Invalid JSON surrogate");
                Json::utf8(out, cp);
                break;
            }
            default: throw std::runtime_error("Invalid JSON escape");
            }
        }
        return out;
    }
    Json parse_value(int depth) {
        if (depth > 32) throw std::runtime_error("JSON too deep");
        ws();
        if (pos >= source.size()) throw std::runtime_error("Empty JSON");
        char c = source[pos];
        if (c == '"') return Json::str(string());
        if (c == '{') {
            ++pos; Json j = Json::object(); ws();
            if (pos < source.size() && source[pos] == '}') { ++pos; return j; }
            while (true) {
                ws(); std::string key = string(); ws(); expect(':');
                j.members[key] = parse_value(depth + 1); ws();
                c = take();
                if (c == '}') return j;
                if (c != ',') throw std::runtime_error("Invalid JSON object");
            }
        }
        if (c == '[') {
            ++pos; Json j = Json::array(); ws();
            if (pos < source.size() && source[pos] == ']') { ++pos; return j; }
            while (true) {
                j.items.push_back(parse_value(depth + 1)); ws();
                c = take();
                if (c == ']') return j;
                if (c != ',') throw std::runtime_error("Invalid JSON array");
            }
        }
        if (source.compare(pos, 4, "null") == 0) { pos += 4; return {}; }
        if (source.compare(pos, 4, "true") == 0) { pos += 4; return Json::flag(true); }
        if (source.compare(pos, 5, "false") == 0) { pos += 5; return Json::flag(false); }
        size_t start = pos;
        if (c == '-') ++pos;
        if (pos >= source.size() || !std::isdigit((unsigned char)source[pos])) throw std::runtime_error("Invalid JSON value");
        if (source[pos] == '0') {
            ++pos;
            if (pos < source.size() && std::isdigit((unsigned char)source[pos])) throw std::runtime_error("Invalid JSON number");
        } else {
            while (pos < source.size() && std::isdigit((unsigned char)source[pos])) ++pos;
        }
        if (pos < source.size() && source[pos] == '.') {
            ++pos;
            if (pos >= source.size() || !std::isdigit((unsigned char)source[pos])) throw std::runtime_error("Invalid JSON number");
            while (pos < source.size() && std::isdigit((unsigned char)source[pos])) ++pos;
        }
        if (pos < source.size() && (source[pos] == 'e' || source[pos] == 'E')) {
            ++pos;
            if (pos < source.size() && (source[pos] == '+' || source[pos] == '-')) ++pos;
            if (pos >= source.size() || !std::isdigit((unsigned char)source[pos])) throw std::runtime_error("Invalid JSON number");
            while (pos < source.size() && std::isdigit((unsigned char)source[pos])) ++pos;
        }
        return Json::num(source.substr(start, pos - start));
    }
public:
    explicit Parser(const std::string& s) : source(s) {}
    Json parse() {
        Json j = parse_value(0); ws();
        if (pos != source.size()) throw std::runtime_error("Trailing JSON data");
        return j;
    }
};
inline Json parse(const std::string& source) { return Parser(source).parse(); }
}
