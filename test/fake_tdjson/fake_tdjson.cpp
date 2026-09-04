#include <td/telegram/td_json_client.h>

#include "fake_tdjson_hook.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

using nlohmann::json;

std::string strField(const json& j, const char* key) {
    if (j.is_object() && j.contains(key) && j[key].is_string()) {
        return j[key].get<std::string>();
    }
    return std::string();
}

std::int64_t intField(const json& j, const char* key) {
    if (j.is_object() && j.contains(key) && j[key].is_number()) {
        return j[key].get<std::int64_t>();
    }
    return 0;
}

class FakeTd {
public:
    static FakeTd& instance() {
        static FakeTd f;
        return f;
    }

    int createClientId() { return nextId_.fetch_add(1); }

    void send(int clientId, const std::string& request) {
        record(request);
        if (firstRequestOf(clientId)) {
            pushAuth(clientId, "authorizationStateWaitTdlibParameters");
        }
        json req = json::parse(request, nullptr, false);
        if (req.is_discarded() || !req.is_object()) return;

        const std::string type = strField(req, "@type");
        const std::string extra = strField(req, "@extra");

        if (type == "setTdlibParameters") {
            pushAuth(clientId, "authorizationStateWaitPhoneNumber");

        } else if (type == "checkAuthenticationBotToken") {
            pushAuth(clientId, "authorizationStateReady");

        } else if (type == "setAuthenticationPhoneNumber") {
            pushAuth(clientId, "authorizationStateWaitCode");

        } else if (type == "checkAuthenticationCode") {
            if (strField(req, "code") == "2fa") {
                pushAuth(clientId, "authorizationStateWaitPassword");
            } else {
                pushAuth(clientId, "authorizationStateReady");
            }

        } else if (type == "checkAuthenticationPassword") {
            pushAuth(clientId, "authorizationStateReady");

        } else if (type == "getMe" || type == "getUser") {
            const bool self = (type == "getMe");
            const std::int64_t id =
                self ? (100000 + clientId) : intField(req, "user_id");
            json user;
            user["@type"] = "user";
            user["id"] = id;
            user["first_name"] = self ? ("Anon" + std::to_string(clientId))
                                      : ("User" + std::to_string(id));
            json usernames;
            usernames["@type"] = "usernames";
            json active = json::array();
            active.push_back(self ? (std::string("anon_bot_") + std::to_string(clientId))
                                  : (std::string("user_") + std::to_string(id)));
            usernames["active_usernames"] = active;
            user["usernames"] = usernames;
            respond(clientId, extra, user);

        } else if (type == "getChat" || type == "searchPublicChat") {
            json chat;
            chat["@type"] = "chat";

            chat["id"] = (type == "getChat") ? intField(req, "chat_id") : -1001122334455LL;
            chat["title"] = "Fake Chat";
            respond(clientId, extra, chat);

        } else if (type == "getChatMember") {
            json status;
            status["@type"] = "chatMemberStatusAdministrator";
            json member;
            member["@type"] = "chatMember";
            member["status"] = status;
            respond(clientId, extra, member);

        } else if (type == "sendMessage") {
            const std::int64_t chatId = intField(req, "chat_id");
            const std::int64_t msgId = nextMessageId_.fetch_add(1);
            json msg = makeMessage(chatId, msgId, 100000 + clientId,
                                   contentOf(req));
            store(msg);
            respond(clientId, extra, msg);

        } else if (type == "editMessageText") {
            const std::int64_t chatId = intField(req, "chat_id");
            const std::int64_t msgId = intField(req, "message_id");
            json msg = makeMessage(chatId, msgId, 100000 + clientId, contentOf(req));
            store(msg);
            respond(clientId, extra, msg);

        } else if (type == "editMessageReplyMarkup") {
            const std::int64_t chatId = intField(req, "chat_id");
            const std::int64_t msgId = intField(req, "message_id");
            json msg = lookup(chatId, msgId);
            if (msg.is_null()) {
                msg = makeMessage(chatId, msgId, 100000 + clientId, textContent(""));
            }
            respond(clientId, extra, msg);

        } else if (type == "getMessage") {
            const std::int64_t chatId = intField(req, "chat_id");
            const std::int64_t msgId = intField(req, "message_id");
            json msg = lookup(chatId, msgId);
            if (msg.is_null()) {
                json err;
                err["@type"] = "error";
                err["code"] = 404;
                err["message"] = "MESSAGE_NOT_FOUND";
                respond(clientId, extra, err);
            } else {
                respond(clientId, extra, msg);
            }

        } else if (type == "forwardMessages") {
            json ids = (req.contains("message_ids") && req["message_ids"].is_array())
                           ? req["message_ids"] : json::array();
            json copies = json::array();
            for (const json& id : ids) {
                (void)id;
                copies.push_back(makeMessage(intField(req, "chat_id"),
                                             nextMessageId_.fetch_add(1),
                                             100000 + clientId, textContent("")));
            }
            json result;
            result["@type"] = "messages";
            result["total_count"] = static_cast<std::int64_t>(copies.size());
            result["messages"] = copies;
            respond(clientId, extra, result);

        } else if (type == "getMessageLink") {
            json link;
            link["@type"] = "messageLink";
            link["link"] = "https://t.me/c/" +
                           std::to_string(intField(req, "chat_id")) + "/" +
                           std::to_string(intField(req, "message_id"));
            link["is_public"] = true;
            respond(clientId, extra, link);

        } else if (type == "deleteMessages" || type == "leaveChat" ||
                   type == "joinChat" || type == "answerCallbackQuery") {
            json ok;
            ok["@type"] = "ok";
            respond(clientId, extra, ok);

        } else if (type == "close") {
            pushAuth(clientId, "authorizationStateClosed");

        } else {

            if (!extra.empty()) {
                json ok;
                ok["@type"] = "ok";
                respond(clientId, extra, ok);
            }
        }
    }

    std::string receive(double timeoutSec) {
        std::unique_lock<std::mutex> lk(m_);
        if (q_.empty()) {
            const auto ms = std::chrono::milliseconds(
                static_cast<long long>(timeoutSec * 1000.0));
            cv_.wait_for(lk, ms, [this] { return !q_.empty(); });
        }
        if (q_.empty()) return std::string();
        std::string s = std::move(q_.front());
        q_.pop_front();
        return s;
    }

    std::string execute(const std::string& request) {
        json req = json::parse(request, nullptr, false);
        if (req.is_discarded() || !req.is_object()) return std::string();
        if (strField(req, "@type") == "parseTextEntities") {
            json ft;
            ft["@type"] = "formattedText";
            ft["text"] = strField(req, "text");
            ft["entities"] = json::array();
            return ft.dump();
        }
        json ok;
        ok["@type"] = "ok";
        return ok.dump();
    }

    void inject(int clientId, const std::string& updateJson) {
        json j = json::parse(updateJson, nullptr, false);
        if (j.is_discarded() || !j.is_object()) return;
        j["@client_id"] = clientId;
        enqueue(j.dump());
    }

    void clearRequests() {
        std::lock_guard<std::mutex> lk(reqMutex_);
        requests_.clear();
    }

    std::vector<std::string> requests() {
        std::lock_guard<std::mutex> lk(reqMutex_);
        return requests_;
    }

    std::size_t countOfType(const std::string& type) {
        std::lock_guard<std::mutex> lk(reqMutex_);
        std::size_t n = 0;
        for (const std::string& r : requests_) {
            if (typeOf(r) == type) ++n;
        }
        return n;
    }

    std::string requestOfType(const std::string& type, std::size_t nth) {
        std::lock_guard<std::mutex> lk(reqMutex_);
        std::string last;
        std::size_t seen = 0;
        for (const std::string& r : requests_) {
            if (typeOf(r) != type) continue;
            if (nth != std::string::npos && seen == nth) return r;
            last = r;
            ++seen;
        }
        return (nth == std::string::npos) ? last : std::string();
    }

    void putMessage(std::int64_t chatId, std::int64_t messageId,
                    std::int64_t senderUserId, const std::string& text) {
        store(makeMessage(chatId, messageId, senderUserId, textContent(text)));
    }

    void reset() {
        {
            std::lock_guard<std::mutex> lk(m_);
            q_.clear();
        }
        clearRequests();

        std::lock_guard<std::mutex> lk(msgMutex_);
        messages_.clear();
    }

private:
    FakeTd() = default;

    bool firstRequestOf(int clientId) {
        std::lock_guard<std::mutex> lk(startedMutex_);
        return started_.insert(clientId).second;
    }

    static std::string typeOf(const std::string& requestJson) {
        json j = json::parse(requestJson, nullptr, false);
        if (j.is_discarded()) return std::string();
        return strField(j, "@type");
    }

    void record(const std::string& request) {
        std::lock_guard<std::mutex> lk(reqMutex_);
        requests_.push_back(request);
    }

    static json textContent(const std::string& text) {
        json ft;
        ft["@type"] = "formattedText";
        ft["text"] = text;
        ft["entities"] = json::array();
        json content;
        content["@type"] = "messageText";
        content["text"] = ft;
        return content;
    }

    static json contentOf(const json& req) {
        if (req.contains("input_message_content") &&
            req["input_message_content"].is_object() &&
            req["input_message_content"].contains("text") &&
            req["input_message_content"]["text"].is_object()) {
            json content;
            content["@type"] = "messageText";
            content["text"] = req["input_message_content"]["text"];
            return content;
        }
        return textContent("");
    }

    static json makeMessage(std::int64_t chatId, std::int64_t messageId,
                            std::int64_t senderUserId, json content) {
        json sender;
        sender["@type"] = "messageSenderUser";
        sender["user_id"] = senderUserId;
        json msg;
        msg["@type"] = "message";
        msg["id"] = messageId;
        msg["chat_id"] = chatId;
        msg["sender_id"] = sender;
        msg["content"] = std::move(content);
        return msg;
    }

    void store(const json& msg) {
        std::lock_guard<std::mutex> lk(msgMutex_);
        messages_[std::make_pair(intField(msg, "chat_id"), intField(msg, "id"))] = msg;
    }

    json lookup(std::int64_t chatId, std::int64_t messageId) {
        std::lock_guard<std::mutex> lk(msgMutex_);
        auto it = messages_.find(std::make_pair(chatId, messageId));
        return (it == messages_.end()) ? json() : it->second;
    }

    void enqueue(std::string s) {
        {
            std::lock_guard<std::mutex> lk(m_);
            q_.push_back(std::move(s));
        }
        cv_.notify_one();
    }

    void pushUpdate(int clientId, const std::string& updateType, const json& state) {
        json j;
        j["@type"] = updateType;
        j["authorization_state"] = state;
        j["@client_id"] = clientId;
        enqueue(j.dump());
    }

    void pushAuth(int clientId, const std::string& stateType) {
        json st;
        st["@type"] = stateType;
        pushUpdate(clientId, "updateAuthorizationState", st);
    }

    void respond(int clientId, const std::string& extra, json body) {
        body["@client_id"] = clientId;
        if (!extra.empty()) body["@extra"] = extra;
        enqueue(body.dump());
    }

    std::mutex m_;
    std::condition_variable cv_;
    std::deque<std::string> q_;

    std::atomic<int> nextId_{1};
    std::atomic<std::int64_t> nextMessageId_{5000};

    std::mutex reqMutex_;
    std::vector<std::string> requests_;

    std::mutex msgMutex_;
    std::map<std::pair<std::int64_t, std::int64_t>, json> messages_;

    std::mutex startedMutex_;
    std::set<int> started_;
};

thread_local std::string g_receiveBuf;
thread_local std::string g_executeBuf;

}

extern "C" {

int td_create_client_id(void) {
    return FakeTd::instance().createClientId();
}

void td_send(int client_id, const char* request) {
    if (request) FakeTd::instance().send(client_id, request);
}

const char* td_receive(double timeout) {
    g_receiveBuf = FakeTd::instance().receive(timeout);
    return g_receiveBuf.empty() ? nullptr : g_receiveBuf.c_str();
}

const char* td_execute(const char* request) {
    g_executeBuf = request ? FakeTd::instance().execute(request) : std::string();
    return g_executeBuf.c_str();
}

}

void fake_td_inject(int clientId, const char* updateJson) {
    if (updateJson) FakeTd::instance().inject(clientId, updateJson);
}

void fake_td_clear_requests() { FakeTd::instance().clearRequests(); }

std::size_t fake_td_request_count() { return FakeTd::instance().requests().size(); }

std::vector<std::string> fake_td_requests() { return FakeTd::instance().requests(); }

std::size_t fake_td_count_of_type(const char* type) {
    return type ? FakeTd::instance().countOfType(type) : 0;
}

std::string fake_td_request_of_type(const char* type, std::size_t nth) {
    return type ? FakeTd::instance().requestOfType(type, nth) : std::string();
}

std::string fake_td_last_request_of_type(const char* type) {
    return type ? FakeTd::instance().requestOfType(type, std::string::npos)
                : std::string();
}

void fake_td_put_message(std::int64_t chatId, std::int64_t messageId,
                         std::int64_t senderUserId, const char* text) {
    FakeTd::instance().putMessage(chatId, messageId, senderUserId,
                                  text ? text : "");
}

void fake_td_reset() { FakeTd::instance().reset(); }
