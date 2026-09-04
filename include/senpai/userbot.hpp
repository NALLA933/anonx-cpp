#ifndef SENPAI_USERBOT_HPP
#define SENPAI_USERBOT_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "senpai/telegram_client.hpp"

namespace senpai {

class Userbot {
public:
    struct AssistantSpec {
        std::string name = "assistant";
        std::string phoneNumber;
        std::string sessionDirectory;

        std::function<std::string()> phoneProvider;
        std::function<std::string()> codeProvider;
        std::function<std::string()> passwordProvider;
    };

    Userbot(int apiId, std::string apiHash);
    ~Userbot();

    Userbot(const Userbot&) = delete;
    Userbot& operator=(const Userbot&) = delete;

    void addAssistant(AssistantSpec spec);
    std::size_t count() const { return specs_.size(); }

    void setLoggerChatId(std::int64_t id);

    void setSupportChat(std::string username);

    void setInteractiveLogin(bool on);

    bool bootAll(int timeoutMs = 120000);
    void exitAll();

    TelegramClient* at(std::size_t index);
    const std::vector<std::unique_ptr<TelegramClient>>& clients() const { return clients_; }

private:
    void announce(TelegramClient& client);
    void joinSupport(TelegramClient& client);

    int apiId_;
    std::string apiHash_;
    std::vector<AssistantSpec> specs_;
    std::vector<std::unique_ptr<TelegramClient>> clients_;

    std::int64_t loggerChatId_ = 0;
    std::string supportChat_;
    bool interactive_ = true;
};

}

#endif
