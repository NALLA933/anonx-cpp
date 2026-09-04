#include "senpai/telegram/dispatcher.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>

#include "senpai/utils/string_utils.hpp"

namespace senpai {
namespace {

using nlohmann::json;

std::string strField(const json& j, const char* key) {
    if (j.is_object() && j.contains(key) && j[key].is_string()) {
        return j[key].get<std::string>();
    }
    return std::string();
}

std::int64_t intField(const json& j, const char* key) {
    if (j.is_object() && j.contains(key)) {
        const auto& val = j[key];
        if (val.is_number()) {
            return val.get<std::int64_t>();
        }
        if (val.is_string()) {
            try {
                return std::stoll(val.get<std::string>());
            } catch (...) {
                return 0;
            }
        }
    }
    return 0;
}

} // namespace

std::int64_t MessageContext::reply(const std::string& html) const {
    return client ? client->sendMessage(chatId, html) : 0;
}

void CallbackContext::answer(const std::string& text, bool alert) const {
    if (!client || queryId == 0) return;
    json req;
    req["@type"] = "answerCallbackQuery";
    req["callback_query_id"] = std::to_string(queryId);
    if (!text.empty()) req["text"] = text;
    req["show_alert"] = alert;
    client->raw().send(req.dump());
}

namespace filters {

Filter command(std::vector<std::string> names) {
    for (auto& n : names) n = utils::toLower(n);
    return Filter([names](const MessageContext& c) {
        if (c.command.empty()) return false;
        const std::string name = utils::toLower(c.command[0]);
        for (const auto& n : names) {
            if (n == name) return true;
        }
        return false;
    });
}

Filter privateChat() {
    return Filter([](const MessageContext& c) { return c.chatType == ChatType::Private; });
}

Filter groupChat() {
    return Filter([](const MessageContext& c) { return c.chatType == ChatType::Group; });
}

Filter user(std::vector<std::int64_t> ids) {
    return Filter([ids](const MessageContext& c) {
        for (std::int64_t id : ids) {
            if (id == c.fromUserId) return true;
        }
        return false;
    });
}

Filter userWhere(std::function<bool(std::int64_t)> pred) {
    return Filter([pred](const MessageContext& c) {
        return c.fromUserId != 0 && pred && pred(c.fromUserId);
    });
}

Filter textMessage() {
    return Filter([](const MessageContext& c) { return !c.text.empty(); });
}

CallbackFilter callbackData(std::string exact) {
    return CallbackFilter([exact](const CallbackContext& c) { return c.data == exact; });
}

CallbackFilter callbackDataPrefix(std::string prefix) {
    return CallbackFilter(
        [prefix](const CallbackContext& c) { return c.data.rfind(prefix, 0) == 0; });
}

} // namespace filters

void Dispatcher::setPrefixes(std::vector<char> prefixes) {
    std::lock_guard<std::mutex> lk(mtx_);
    prefixes_ = std::move(prefixes);
}

void Dispatcher::setBotUsername(std::string username) {
    std::lock_guard<std::mutex> lk(mtx_);
    botUsername_ = utils::toLower(std::move(username));
}

void Dispatcher::attach(TelegramClient& client) {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        client_ = &client;
        if (botUsername_.empty() && !client.me().username.empty()) {
            botUsername_ = utils::toLower(client.me().username);
        }
    }
    startWorkers();
    client.setUpdateObserver([this](const std::string& u) { onUpdate(u); });
}

Dispatcher::~Dispatcher() {
    stopWorkers();
}

void Dispatcher::setWorkers(std::size_t n) {
    std::lock_guard<std::mutex> lk(qMutex_);
    workerCount_ = n;
}

void Dispatcher::startWorkers() {
    std::lock_guard<std::mutex> lk(qMutex_);
    if (running_.load() || workerCount_ == 0) return;
    running_.store(true);
    workers_.reserve(workerCount_);
    for (std::size_t i = 0; i < workerCount_; ++i) {
        workers_.emplace_back([this] { workerLoop(); });
    }
}

void Dispatcher::stopWorkers() {
    if (!running_.exchange(false)) {

        std::lock_guard<std::mutex> lk(qMutex_);
        queue_.clear();
        return;
    }
    qCv_.notify_all();
    for (std::thread& t : workers_) {
        if (t.joinable()) t.join();
    }
    std::lock_guard<std::mutex> lk(qMutex_);
    workers_.clear();
    queue_.clear();
}

void Dispatcher::workerLoop() {
    for (;;) {
        std::string update;
        {
            std::unique_lock<std::mutex> lk(qMutex_);
            qCv_.wait(lk, [this] { return !queue_.empty() || !running_.load(); });
            if (queue_.empty()) {
                if (!running_.load()) return;
                continue;
            }
            update = std::move(queue_.front());
            queue_.pop_front();
            ++busy_;
        }

        try {
            handleUpdate(update);
        } catch (...) {
        }

        {
            std::lock_guard<std::mutex> lk(qMutex_);
            --busy_;
        }
        qCv_.notify_all();
    }
}

bool Dispatcher::idle() const {
    std::lock_guard<std::mutex> lk(qMutex_);
    return queue_.empty() && busy_ == 0;
}

void Dispatcher::onUpdate(const std::string& updateJson) {
    if (running_.load()) {
        {
            std::lock_guard<std::mutex> lk(qMutex_);
            queue_.push_back(updateJson);
        }
        qCv_.notify_one();
        return;
    }

    handleUpdate(updateJson);
}

void Dispatcher::onMessage(Filter filter, MessageHandler handler) {
    std::lock_guard<std::mutex> lk(mtx_);
    messageHandlers_.push_back(MEntry{std::move(filter), std::move(handler)});
}

void Dispatcher::onCallback(CallbackFilter filter, CallbackHandler handler) {
    std::lock_guard<std::mutex> lk(mtx_);
    callbackHandlers_.push_back(CEntry{std::move(filter), std::move(handler)});
}

void Dispatcher::onEveryMessage(MessageHandler handler) {
    std::lock_guard<std::mutex> lk(mtx_);
    watchers_.push_back(std::move(handler));
}

std::vector<std::string> Dispatcher::parseCommand(const std::string& text) const {
    std::vector<std::string> out;
    if (text.empty()) return out;

    bool okPrefix = false;
    for (char p : prefixes_) {
        if (p == text[0]) { okPrefix = true; break; }
    }
    if (!okPrefix) return out;

    std::vector<std::string> tokens = utils::splitWs(text);
    if (tokens.empty()) return out;

    std::string head = tokens[0].substr(1);
    const auto at = head.find('@');
    if (at != std::string::npos) {
        const std::string uname = head.substr(at + 1);
        head = head.substr(0, at);

        if (!uname.empty() && !botUsername_.empty() && utils::toLower(uname) != botUsername_) {
            return {};
        }
    }
    if (head.empty()) return {};

    out.push_back(head);
    for (std::size_t i = 1; i < tokens.size(); ++i) out.push_back(tokens[i]);
    return out;
}

bool Dispatcher::dispatchMessage(MessageContext& ctx) {
    std::vector<MEntry> handlers;
    std::vector<MessageHandler> watchers;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        handlers = messageHandlers_;
        watchers = watchers_;
    }

    for (const auto& w : watchers)
        w(ctx);
    for (const auto& e : handlers) {
        if (e.filter(ctx)) {
            e.handler(ctx);
            return true;
        }
    }
    return false;
}

bool Dispatcher::dispatchCallback(CallbackContext& ctx) {
    std::vector<CEntry> handlers;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        handlers = callbackHandlers_;
    }
    for (const auto& e : handlers) {
        if (e.filter(ctx)) {
            e.handler(ctx);
            return true;
        }
    }
    return false;
}

void Dispatcher::handleUpdate(const std::string& updateJson) {
    TelegramClient* client = nullptr;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        client = client_;
    }

    json j = json::parse(updateJson, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return;

    const std::string type = strField(j, "@type");

    if (type == "updateNewMessage" && j.contains("message") && j["message"].is_object()) {
        const json& m = j["message"];
        MessageContext ctx;
        ctx.client = client;
        ctx.chatId = intField(m, "chat_id");
        ctx.chatType = ctx.chatId >= 0 ? ChatType::Private : ChatType::Group;
        ctx.isPrivate = ctx.chatId >= 0;
        ctx.messageId = intField(m, "id");

        if (m.contains("sender_id") && m["sender_id"].is_object()) {
            const json& s = m["sender_id"];
            if (strField(s, "@type") == "messageSenderUser") {
                ctx.fromUserId = intField(s, "user_id");
            }
        }

        if (m.contains("reply_to") && m["reply_to"].is_object()) {
            const json& r = m["reply_to"];
            if (strField(r, "@type") == "messageReplyToMessage") {
                const std::int64_t rChat = intField(r, "chat_id");
                if (rChat == 0 || rChat == ctx.chatId)
                    ctx.replyToMessageId = intField(r, "message_id");
            }
        } else {
            ctx.replyToMessageId = intField(m, "reply_to_message_id");
        }
        if (m.contains("content") && m["content"].is_object()) {
            const json& content = m["content"];
            if (strField(content, "@type") == "messageText" &&
                content.contains("text") && content["text"].is_object()) {
                ctx.text = strField(content["text"], "text");
            } else if (content.contains("caption") && content["caption"].is_object()) {
                ctx.text = strField(content["caption"], "text");
            }
        }
        ctx.command = parseCommand(ctx.text);
        dispatchMessage(ctx);

    } else if (type == "updateNewCallbackQuery") {
        CallbackContext ctx;
        ctx.client = client;
        ctx.queryId = intField(j, "id");
        ctx.fromUserId = intField(j, "sender_user_id");
        ctx.chatId = intField(j, "chat_id");
        ctx.messageId = intField(j, "message_id");
        if (j.contains("payload") && j["payload"].is_object()) {
            const json& p = j["payload"];
            if (strField(p, "@type") == "callbackQueryPayloadData") {
                ctx.data = utils::base64Decode(strField(p, "data"));
            }
        }
        dispatchCallback(ctx);
    }
}

}
