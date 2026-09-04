#ifndef SENPAI_INLINE_KEYBOARD_HPP
#define SENPAI_INLINE_KEYBOARD_HPP

#include <string>
#include <utility>
#include <vector>

namespace senpai {

struct InlineButton {
    enum class Kind { Callback, Url, Copy };

    std::string text;
    Kind        kind = Kind::Callback;
    std::string data;
    std::string url;
    std::string copy;

    static InlineButton callback(std::string t, std::string d) {
        InlineButton b; b.text = std::move(t); b.kind = Kind::Callback; b.data = std::move(d); return b;
    }
    static InlineButton link(std::string t, std::string u) {
        InlineButton b; b.text = std::move(t); b.kind = Kind::Url; b.url = std::move(u); return b;
    }
    static InlineButton copyText(std::string t, std::string c) {
        InlineButton b; b.text = std::move(t); b.kind = Kind::Copy; b.copy = std::move(c); return b;
    }
};

using InlineKeyboard = std::vector<std::vector<InlineButton>>;

}

#endif
