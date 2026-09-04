#ifndef ANONX_BOT_API_HPP
#define ANONX_BOT_API_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "anonx/inline_keyboard.hpp"

namespace anonx {

class BotApi {
public:
    virtual ~BotApi() = default;

    virtual std::int64_t sendMessage(std::int64_t chatId, const std::string& html,
                                     const InlineKeyboard& kb = {}) = 0;

    virtual std::int64_t sendPhoto(std::int64_t chatId, const std::string& photo,
                                   const std::string& captionHtml = "",
                                   const InlineKeyboard& kb = {}) {
        (void)photo;
        return sendMessage(chatId, captionHtml, kb);
    }

    virtual bool editMessageText(std::int64_t chatId, std::int64_t messageId,
                                 const std::string& html,
                                 const InlineKeyboard& kb = {}) = 0;

    virtual bool editMessageReplyMarkup(std::int64_t chatId, std::int64_t messageId,
                                        const InlineKeyboard& kb) = 0;

    virtual bool deleteMessage(std::int64_t chatId, std::int64_t messageId) = 0;

    virtual bool deleteMessages(std::int64_t chatId,
                                const std::vector<std::int64_t>& messageIds) = 0;

    virtual std::string getMessageText(std::int64_t chatId, std::int64_t messageId) = 0;

    virtual bool copyMessage(std::int64_t fromChatId, std::int64_t messageId,
                             std::int64_t toChatId) = 0;

    virtual bool forwardMessage(std::int64_t fromChatId, std::int64_t messageId,
                               std::int64_t toChatId) = 0;

    virtual std::int64_t getMessageSenderId(std::int64_t chatId,
                                            std::int64_t messageId) = 0;

    virtual void answerCallback(std::int64_t queryId, const std::string& text = "",
                                bool alert = false) = 0;

    virtual void leaveChat(std::int64_t chatId) = 0;

    virtual std::string getChatMemberStatus(std::int64_t chatId, std::int64_t userId) = 0;

    virtual std::string botName() { return "AnonXMusic"; }
    virtual std::string botUsername() { return ""; }

    virtual std::string chatTitle(std::int64_t chatId) { (void)chatId; return ""; }
    virtual std::string userUsername(std::int64_t userId) { (void)userId; return ""; }

    virtual std::string userMention(std::int64_t userId) {
        const std::string id = std::to_string(userId);
        return "<a href=tg://user?id=" + id + ">" + id + "</a>";
    }

    virtual std::string messageLink(std::int64_t chatId, std::int64_t messageId) {
        (void)chatId; (void)messageId; return "";
    }
};

}

#endif
