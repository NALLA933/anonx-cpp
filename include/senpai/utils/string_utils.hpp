#ifndef SENPAI_UTILS_STRING_UTILS_HPP
#define SENPAI_UTILS_STRING_UTILS_HPP

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace senpai {
namespace utils {

inline bool isSpace(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

inline std::string toLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

inline std::string trim(const std::string& s) {
    auto start = s.begin();
    while (start != s.end() && isSpace(*start)) ++start;
    auto end = s.end();
    do {
        if (end == start) break;
        --end;
    } while (isSpace(*end));
    return (start < end + 1) ? std::string(start, end + 1) : std::string();
}

inline std::vector<std::string> splitWs(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string tok;
    while (in >> tok) {
        out.push_back(tok);
    }
    return out;
}

inline bool parseI64(const std::string& text, std::int64_t& out) {
    if (text.empty()) return false;
    const char* first = text.data();
    if (text[0] == '+') ++first;
    const char* last = text.data() + text.size();
    if (first == last) return false;
    auto [ptr, ec] = std::from_chars(first, last, out);
    return ec == std::errc{} && ptr == last;
}

inline bool parseU32(const std::string& text, long& out) {
    if (text.empty() || text[0] == '-') return false;
    const char* first = text.data();
    if (text[0] == '+') ++first;
    const char* last = text.data() + text.size();
    if (first == last) return false;
    auto [ptr, ec] = std::from_chars(first, last, out);
    return ec == std::errc{} && ptr == last;
}

inline std::string htmlEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;";  break;
            case '>': out += "&gt;";  break;
            default:  out.push_back(c);
        }
    }
    return out;
}

inline std::string base64Decode(const std::string& in) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::string out;
    int buf = 0, bits = 0;
    for (char c : in) {
        if (c == '=' || isSpace(c)) continue;
        int v = val(c);
        if (v < 0) continue;
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((buf >> bits) & 0xFF));
        }
    }
    return out;
}

}
}

#endif
