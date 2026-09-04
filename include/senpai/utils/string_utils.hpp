#ifndef SENPAI_UTILS_STRING_UTILS_HPP
#define SENPAI_UTILS_STRING_UTILS_HPP

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace senpai {
namespace utils {

inline bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
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
    if (text.empty() || text.size() > 20) return false;
    std::size_t i = 0;
    bool negative = false;
    if (text[0] == '-' || text[0] == '+') {
        negative = text[0] == '-';
        i = 1;
        if (text.size() == 1) return false;
    }
    std::int64_t value = 0;
    for (; i < text.size(); ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
        if (value > (9223372036854775807LL - (text[i] - '0')) / 10) return false;
        value = value * 10 + (text[i] - '0');
    }
    out = negative ? -value : value;
    return true;
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

}
}

#endif
