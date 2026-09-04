#ifndef SENPAI_BUTTONS_HPP
#define SENPAI_BUTTONS_HPP

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "senpai/plugins/inline_keyboard.hpp"

namespace senpai {
namespace buttons {

InlineKeyboard controls(std::int64_t chatId,
                        const std::string& status = "",
                        const std::string& timer = "",
                        bool remove = false);

InlineKeyboard playQueued(std::int64_t chatId, const std::string& itemId,
                          const std::string& text);

InlineKeyboard queueMarkup(std::int64_t chatId, const std::string& text, bool playing);

InlineKeyboard cancelDl(const std::string& text);

InlineKeyboard pingMarkup(const std::string& text, const std::string& url);

struct MenuText {
    std::string help;
    std::string addMe;
    std::string support;
    std::string channel;
    std::string source;
    std::string language;
    std::string cmdDelete;
    std::string playMode;
    std::string back;
    std::string close;
};

InlineKeyboard startPrivate(const MenuText& t, const std::string& addMeUrl,
                            const std::string& supportChat,
                            const std::string& supportChannel,
                            const std::string& sourceUrl);

InlineKeyboard startGroup(const MenuText& t, const std::string& addMeUrl,
                          const std::string& supportChat,
                          const std::string& supportChannel);

InlineKeyboard helpMenu(const std::vector<std::string>& topics,
                        const MenuText& t);

InlineKeyboard helpTopic(const MenuText& t);

InlineKeyboard backClose(const MenuText& t, const std::string& backData);

InlineKeyboard langMenu(const std::vector<std::pair<std::string, std::string>>& langs,
                        const MenuText& t);

InlineKeyboard settingsMenu(const MenuText& t, bool cmdDelete, bool playMode);

const char* toggleMark(bool enabled);

}
}

#endif
