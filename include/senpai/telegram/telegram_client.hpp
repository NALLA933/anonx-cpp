#ifndef SENPAI_TELEGRAM_CLIENT_HPP
#define SENPAI_TELEGRAM_CLIENT_HPP

#include <cstdint>
#include <functional>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "senpai/inline_keyboard.hpp"
#include "senpai/td_client.hpp"

namespace senpai {

class TelegramClient {
public:
    struct Options {
        int apiId = 0;
        std::string apiHash;

        std::string databaseDirectory = "tdlib";

        std::string botToken;
        std::string phoneNumber;

        std::string name = "account";

        std::function<std::string()> phoneProvider;
        std::function<std::string()> codeProvider;
        std::function<std::string()> passwordProvider;

        std::string deviceModel = "SenpaiMusic";
        std::string applicationVersion = "1.0";
        std::string systemLanguageCode = "en";
    };

    struct Me {
        std::int64_t id = 0;
        std::string firstName;
        std::string username;
        std::string mention;
    };

    struct UserInfo {
        std::int64_t id = 0;
        std::string firstName;
        std::string username;
        bool found = false;
    };

    explicit TelegramClient(Options opts);
    ~TelegramClient();

    TelegramClient(const TelegramClient&) = delete;
    TelegramClient& operator=(const TelegramClient&) = delete;

    bool boot(int timeoutMs = 60000);

    void exit();

    bool authorized() const { return status_.load() == Status::Ready; }
    const Me& me() const { return me_; }
    const std::string& name() const { return opts_.name; }

    Me getMe();

    std::int64_t sendMessage(std::int64_t chatId, const std::string& html,
                             const InlineKeyboard& kb = {});

    std::int64_t sendPhoto(std::int64_t chatId, const std::string& photo,
                           const std::string& captionHtml = "",
                           const InlineKeyboard& kb = {});

    bool editMessageText(std::int64_t chatId, std::int64_t messageId,
                         const std::string& html, const InlineKeyboard& kb = {});

    bool editMessageReplyMarkup(std::int64_t chatId, std::int64_t messageId,
                                const InlineKeyboard& kb);

    bool deleteMessages(std::int64_t chatId,
                        const std::vector<std::int64_t>& messageIds,
                        bool revoke = true);

    std::string getMessageText(std::int64_t chatId, std::int64_t messageId);

    std::int64_t getMessageSenderId(std::int64_t chatId, std::int64_t messageId);

    bool forwardMessages(std::int64_t fromChatId,
                         const std::vector<std::int64_t>& messageIds,
                         std::int64_t toChatId, bool sendCopy);

    void answerCallbackQuery(std::int64_t queryId, const std::string& text,
                             bool alert);

    void leaveChat(std::int64_t chatId);

    std::string chatTitle(std::int64_t chatId);

    UserInfo getUser(std::int64_t userId);

    std::string messageLink(std::int64_t chatId, std::int64_t messageId);

    std::string getChatMemberStatus(std::int64_t chatId, std::int64_t userId);

    std::int64_t resolveMessageId(std::int64_t messageId);

    TdClient& raw() { return client_; }

    void setUpdateObserver(TdClient::UpdateHandler observer);

private:
    enum class Status { Pending, Ready, Closed, Error };

    void onUpdate(const std::string& updateJson);
    void setStatus(Status s);

    Options opts_;
    TdClient client_;
    Me me_;

    std::atomic<Status> status_{Status::Pending};
    std::mutex cvMutex_;
    std::condition_variable cv_;

    std::mutex observerMutex_;
    TdClient::UpdateHandler observer_;

    std::atomic<bool> closeRequested_{false};

    std::mutex sentMsgMutex_;
    std::unordered_map<std::int64_t, std::int64_t> sentMsgMap_;
};

}

#endif
