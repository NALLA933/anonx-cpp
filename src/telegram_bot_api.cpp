#include "anonx/telegram_bot_api.hpp"

namespace anonx {
namespace {

std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;";  break;
            case '>': out += "&gt;";  break;
            default:  out.push_back(c); break;
        }
    }
    return out;
}

}

TelegramBotApi::TelegramBotApi(TelegramClient& bot) : bot_(bot) {}

std::int64_t TelegramBotApi::sendMessage(std::int64_t chatId, const std::string& html,
                                         const InlineKeyboard& kb) {
    return bot_.sendMessage(chatId, html, kb);
}

std::int64_t TelegramBotApi::sendPhoto(std::int64_t chatId, const std::string& photo,
                                       const std::string& captionHtml,
                                       const InlineKeyboard& kb) {
    return bot_.sendPhoto(chatId, photo, captionHtml, kb);
}

bool TelegramBotApi::editMessageText(std::int64_t chatId, std::int64_t messageId,
                                     const std::string& html, const InlineKeyboard& kb) {
    return bot_.editMessageText(chatId, messageId, html, kb);
}

bool TelegramBotApi::editMessageReplyMarkup(std::int64_t chatId, std::int64_t messageId,
                                            const InlineKeyboard& kb) {
    return bot_.editMessageReplyMarkup(chatId, messageId, kb);
}

bool TelegramBotApi::deleteMessage(std::int64_t chatId, std::int64_t messageId) {
    return bot_.deleteMessages(chatId, {messageId});
}

bool TelegramBotApi::deleteMessages(std::int64_t chatId,
                                    const std::vector<std::int64_t>& messageIds) {
    return bot_.deleteMessages(chatId, messageIds);
}

std::string TelegramBotApi::getMessageText(std::int64_t chatId, std::int64_t messageId) {
    return bot_.getMessageText(chatId, messageId);
}

bool TelegramBotApi::copyMessage(std::int64_t fromChatId, std::int64_t messageId,
                                 std::int64_t toChatId) {

    return bot_.forwardMessages(fromChatId, {messageId}, toChatId, true);
}

bool TelegramBotApi::forwardMessage(std::int64_t fromChatId, std::int64_t messageId,
                                    std::int64_t toChatId) {
    return bot_.forwardMessages(fromChatId, {messageId}, toChatId, false);
}

std::int64_t TelegramBotApi::getMessageSenderId(std::int64_t chatId,
                                                std::int64_t messageId) {
    return bot_.getMessageSenderId(chatId, messageId);
}

void TelegramBotApi::answerCallback(std::int64_t queryId, const std::string& text,
                                    bool alert) {
    bot_.answerCallbackQuery(queryId, text, alert);
}

void TelegramBotApi::leaveChat(std::int64_t chatId) {
    bot_.leaveChat(chatId);
}

std::string TelegramBotApi::getChatMemberStatus(std::int64_t chatId,
                                                std::int64_t userId) {
    return bot_.getChatMemberStatus(chatId, userId);
}

std::string TelegramBotApi::botName() {
    const std::string name = bot_.me().firstName;
    return name.empty() ? BotApi::botName() : name;
}

std::string TelegramBotApi::botUsername() {
    return bot_.me().username;
}

void TelegramBotApi::clearCaches() {
    std::lock_guard<std::mutex> lk(cacheMutex_);
    users_.clear();
    chatTitles_.clear();
}

std::string TelegramBotApi::chatTitle(std::int64_t chatId) {
    {
        std::lock_guard<std::mutex> lk(cacheMutex_);
        const auto it = chatTitles_.find(chatId);
        if (it != chatTitles_.end()) return it->second;
    }
    const std::string title = bot_.chatTitle(chatId);
    if (title.empty()) return title;

    std::lock_guard<std::mutex> lk(cacheMutex_);
    if (chatTitles_.size() >= kCacheLimit) chatTitles_.clear();
    chatTitles_[chatId] = title;
    return title;
}

TelegramClient::UserInfo TelegramBotApi::user(std::int64_t userId) {
    {
        std::lock_guard<std::mutex> lk(cacheMutex_);
        const auto it = users_.find(userId);
        if (it != users_.end()) return it->second;
    }
    const TelegramClient::UserInfo info = bot_.getUser(userId);
    if (!info.found) return info;

    std::lock_guard<std::mutex> lk(cacheMutex_);
    if (users_.size() >= kCacheLimit) users_.clear();
    users_[userId] = info;
    return info;
}

std::string TelegramBotApi::userUsername(std::int64_t userId) {
    return user(userId).username;
}

std::string TelegramBotApi::userMention(std::int64_t userId) {
    const TelegramClient::UserInfo info = user(userId);
    if (!info.found || info.firstName.empty()) {

        return BotApi::userMention(userId);
    }
    return "<a href=\"tg://user?id=" + std::to_string(userId) + "\">" +
           escape(info.firstName) + "</a>";
}

std::string TelegramBotApi::messageLink(std::int64_t chatId, std::int64_t messageId) {
    return bot_.messageLink(chatId, messageId);
}

}
