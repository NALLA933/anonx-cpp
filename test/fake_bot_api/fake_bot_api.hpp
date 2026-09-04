#ifndef ANONX_TEST_FAKE_BOT_API_HPP
#define ANONX_TEST_FAKE_BOT_API_HPP

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "anonx/bot_api.hpp"

namespace anonx {

class FakeBotApi : public BotApi {
public:
    struct Record {
        std::string    op;

        std::int64_t   chatId = 0;
        std::int64_t   messageId = 0;
        std::string    text;
        InlineKeyboard kb;
        bool           alert = false;
    };

    bool editsFail = false;
    bool copiesFail = false;
    std::int64_t nextMessageId = 1000;
    std::string defaultStatus = "chatMemberStatusMember";
    std::string botDisplayName = "AnonXMusic";
    std::string botUser = "AnonXMusicBot";

    std::vector<Record> log;
    std::map<std::pair<std::int64_t, std::int64_t>, std::string> texts;
    std::map<std::pair<std::int64_t, std::int64_t>, std::string> memberStatus;
    std::map<std::pair<std::int64_t, std::int64_t>, std::int64_t> senders;
    std::map<std::int64_t, std::string> titles;
    std::map<std::int64_t, std::string> usernames;
    std::vector<std::int64_t> copyTargets;

    void setStatus(std::int64_t chatId, std::int64_t userId, std::string status) {
        memberStatus[{chatId, userId}] = std::move(status);
    }
    void makeAdmin(std::int64_t chatId, std::int64_t userId) {
        setStatus(chatId, userId, "chatMemberStatusAdministrator");
    }

    void seedMessage(std::int64_t chatId, std::int64_t messageId, std::string text,
                     std::int64_t senderId = 0) {
        texts[{chatId, messageId}] = std::move(text);
        senders[{chatId, messageId}] = senderId;
    }

    std::int64_t sendMessage(std::int64_t chatId, const std::string& html,
                             const InlineKeyboard& kb = {}) override {
        const std::int64_t id = nextMessageId++;
        texts[{chatId, id}] = html;
        log.push_back({"send", chatId, id, html, kb, false});
        return id;
    }

    bool editMessageText(std::int64_t chatId, std::int64_t messageId,
                         const std::string& html,
                         const InlineKeyboard& kb = {}) override {
        if (editsFail)
            return false;
        texts[{chatId, messageId}] = html;
        log.push_back({"edit", chatId, messageId, html, kb, false});
        return true;
    }

    bool editMessageReplyMarkup(std::int64_t chatId, std::int64_t messageId,
                                const InlineKeyboard& kb) override {
        log.push_back({"markup", chatId, messageId, "", kb, false});
        return !editsFail;
    }

    bool deleteMessage(std::int64_t chatId, std::int64_t messageId) override {
        texts.erase({chatId, messageId});
        log.push_back({"delete", chatId, messageId, "", {}, false});
        return true;
    }

    bool deleteMessages(std::int64_t chatId,
                        const std::vector<std::int64_t>& messageIds) override {
        for (std::int64_t id : messageIds)
            texts.erase({chatId, id});
        log.push_back({"deleteMany", chatId, 0, "", {}, false});
        return true;
    }

    std::string getMessageText(std::int64_t chatId, std::int64_t messageId) override {
        auto it = texts.find({chatId, messageId});
        return it == texts.end() ? std::string() : it->second;
    }

    bool copyMessage(std::int64_t fromChatId, std::int64_t messageId,
                     std::int64_t toChatId) override {
        return relay("copy", fromChatId, messageId, toChatId);
    }

    bool forwardMessage(std::int64_t fromChatId, std::int64_t messageId,
                        std::int64_t toChatId) override {
        return relay("forward", fromChatId, messageId, toChatId);
    }

    std::int64_t getMessageSenderId(std::int64_t chatId,
                                    std::int64_t messageId) override {
        auto it = senders.find({chatId, messageId});
        return it == senders.end() ? 0 : it->second;
    }

    void answerCallback(std::int64_t queryId, const std::string& text = "",
                        bool alert = false) override {
        log.push_back({"answer", 0, queryId, text, {}, alert});
    }

    void leaveChat(std::int64_t chatId) override {
        log.push_back({"leave", chatId, 0, "", {}, false});
    }

    std::string getChatMemberStatus(std::int64_t chatId, std::int64_t userId) override {
        auto it = memberStatus.find({chatId, userId});
        return it == memberStatus.end() ? defaultStatus : it->second;
    }

    std::string userMention(std::int64_t userId) override {
        return "@u" + std::to_string(userId);
    }

    std::string botName() override { return botDisplayName; }
    std::string botUsername() override { return botUser; }

    std::string chatTitle(std::int64_t chatId) override {
        auto it = titles.find(chatId);
        return it == titles.end() ? std::string("Chat " + std::to_string(chatId))
                                  : it->second;
    }
    std::string userUsername(std::int64_t userId) override {
        auto it = usernames.find(userId);
        return it == usernames.end() ? std::string() : it->second;
    }

    void clear() { log.clear(); copyTargets.clear(); }

    const Record* last(const std::string& op = "") const {
        for (auto it = log.rbegin(); it != log.rend(); ++it)
            if (op.empty() || it->op == op)
                return &*it;
        return nullptr;
    }

    std::size_t count(const std::string& op) const {
        std::size_t n = 0;
        for (const Record& r : log)
            if (r.op == op)
                ++n;
        return n;
    }

    bool said(const std::string& needle) const {
        for (const Record& r : log)
            if ((r.op == "send" || r.op == "edit") &&
                r.text.find(needle) != std::string::npos)
                return true;
        return false;
    }

    std::string lastSaid() const {
        for (auto it = log.rbegin(); it != log.rend(); ++it)
            if (it->op == "send" || it->op == "edit")
                return it->text;
        return {};
    }

    std::vector<std::string> lastKeyboardData() const {
        for (auto it = log.rbegin(); it != log.rend(); ++it) {
            if (it->op == "send" || it->op == "edit" || it->op == "markup") {
                std::vector<std::string> out;
                for (const auto& row : it->kb)
                    for (const auto& btn : row)
                        out.push_back(btn.data);
                return out;
            }
        }
        return {};
    }

private:

    bool relay(const char* op, std::int64_t fromChatId, std::int64_t messageId,
               std::int64_t toChatId) {
        if (copiesFail)
            return false;
        auto it = texts.find({fromChatId, messageId});
        if (it == texts.end())
            return false;
        copyTargets.push_back(toChatId);
        const std::int64_t id = nextMessageId++;
        texts[{toChatId, id}] = it->second;
        log.push_back({op, toChatId, id, it->second, {}, false});
        return true;
    }
};

}

#endif
