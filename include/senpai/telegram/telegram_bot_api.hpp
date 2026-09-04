#ifndef SENPAI_TELEGRAM_BOT_API_HPP
#define SENPAI_TELEGRAM_BOT_API_HPP

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "senpai/bot_api.hpp"
#include "senpai/telegram_client.hpp"

namespace senpai {

class TelegramBotApi : public BotApi {
public:

    explicit TelegramBotApi(TelegramClient& bot);

    std::int64_t sendMessage(std::int64_t chatId, const std::string& html,
                             const InlineKeyboard& kb = {}) override;
    std::int64_t sendPhoto(std::int64_t chatId, const std::string& photo,
                           const std::string& captionHtml = "",
                           const InlineKeyboard& kb = {}) override;
    bool editMessageText(std::int64_t chatId, std::int64_t messageId,
                         const std::string& html,
                         const InlineKeyboard& kb = {}) override;
    bool editMessageReplyMarkup(std::int64_t chatId, std::int64_t messageId,
                                const InlineKeyboard& kb) override;
    bool deleteMessage(std::int64_t chatId, std::int64_t messageId) override;
    bool deleteMessages(std::int64_t chatId,
                        const std::vector<std::int64_t>& messageIds) override;
    std::string getMessageText(std::int64_t chatId, std::int64_t messageId) override;
    bool copyMessage(std::int64_t fromChatId, std::int64_t messageId,
                     std::int64_t toChatId) override;
    bool forwardMessage(std::int64_t fromChatId, std::int64_t messageId,
                        std::int64_t toChatId) override;
    std::int64_t getMessageSenderId(std::int64_t chatId,
                                    std::int64_t messageId) override;
    void answerCallback(std::int64_t queryId, const std::string& text = "",
                        bool alert = false) override;
    void leaveChat(std::int64_t chatId) override;
    std::string getChatMemberStatus(std::int64_t chatId, std::int64_t userId) override;

    std::string botName() override;
    std::string botUsername() override;
    std::string chatTitle(std::int64_t chatId) override;
    std::string userUsername(std::int64_t userId) override;
    std::string userMention(std::int64_t userId) override;
    std::string messageLink(std::int64_t chatId, std::int64_t messageId) override;

    void clearCaches();

private:
    static constexpr std::size_t kCacheLimit = 4096;

    TelegramClient::UserInfo user(std::int64_t userId);

    TelegramClient& bot_;

    std::mutex cacheMutex_;
    std::unordered_map<std::int64_t, TelegramClient::UserInfo> users_;
    std::unordered_map<std::int64_t, std::string> chatTitles_;
};

}

#endif
