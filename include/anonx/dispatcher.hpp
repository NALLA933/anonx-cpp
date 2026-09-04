#ifndef ANONX_DISPATCHER_HPP
#define ANONX_DISPATCHER_HPP

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "anonx/telegram_client.hpp"

namespace anonx {

enum class ChatType { Private, Group };

struct MessageContext {
    std::int64_t chatId = 0;
    ChatType chatType = ChatType::Group;
    std::int64_t messageId = 0;
    std::int64_t fromUserId = 0;
    std::string text;

    std::int64_t replyToMessageId = 0;

    std::vector<std::string> command;

    TelegramClient* client = nullptr;

    bool isCommand() const { return !command.empty(); }

    std::int64_t reply(const std::string& html) const;
};

struct CallbackContext {
    std::int64_t chatId = 0;
    std::int64_t messageId = 0;
    std::int64_t fromUserId = 0;
    std::string data;
    std::int64_t queryId = 0;
    TelegramClient* client = nullptr;

    void answer(const std::string& text = "", bool alert = false) const;
};

template <typename Ctx>
class BasicFilter {
public:
    using Predicate = std::function<bool(const Ctx&)>;

    BasicFilter() = default;
    explicit BasicFilter(Predicate p) : pred_(std::move(p)) {}

    bool operator()(const Ctx& c) const { return pred_ ? pred_(c) : true; }

private:
    Predicate pred_;
};

template <typename Ctx>
BasicFilter<Ctx> operator&&(BasicFilter<Ctx> a, BasicFilter<Ctx> b) {
    return BasicFilter<Ctx>([a, b](const Ctx& c) { return a(c) && b(c); });
}

template <typename Ctx>
BasicFilter<Ctx> operator||(BasicFilter<Ctx> a, BasicFilter<Ctx> b) {
    return BasicFilter<Ctx>([a, b](const Ctx& c) { return a(c) || b(c); });
}

template <typename Ctx>
BasicFilter<Ctx> operator!(BasicFilter<Ctx> a) {
    return BasicFilter<Ctx>([a](const Ctx& c) { return !a(c); });
}

using Filter = BasicFilter<MessageContext>;
using CallbackFilter = BasicFilter<CallbackContext>;

namespace filters {

Filter command(std::vector<std::string> names);

Filter privateChat();
Filter groupChat();

Filter user(std::vector<std::int64_t> ids);

Filter userWhere(std::function<bool(std::int64_t)> pred);

Filter textMessage();

CallbackFilter callbackData(std::string exact);
CallbackFilter callbackDataPrefix(std::string prefix);

}

class Dispatcher {
public:
    using MessageHandler = std::function<void(MessageContext&)>;
    using CallbackHandler = std::function<void(CallbackContext&)>;

    Dispatcher() = default;
    ~Dispatcher();

    Dispatcher(const Dispatcher&)            = delete;
    Dispatcher& operator=(const Dispatcher&) = delete;

    void setPrefixes(std::vector<char> prefixes);

    void setBotUsername(std::string username);

    void setWorkers(std::size_t n);

    void attach(TelegramClient& client);

    void stopWorkers();

    void onMessage(Filter filter, MessageHandler handler);
    void onCallback(CallbackFilter filter, CallbackHandler handler);

    void onEveryMessage(MessageHandler handler);

    void onUpdate(const std::string& updateJson);

    bool idle() const;

    bool dispatchMessage(MessageContext& ctx);
    bool dispatchCallback(CallbackContext& ctx);

    std::vector<std::string> parseCommand(const std::string& text) const;

private:

    void handleUpdate(const std::string& updateJson);
    void startWorkers();
    void workerLoop();

    std::mutex mtx_;

    struct MEntry { Filter filter; MessageHandler handler; };
    struct CEntry { CallbackFilter filter; CallbackHandler handler; };
    std::vector<MEntry> messageHandlers_;
    std::vector<CEntry> callbackHandlers_;
    std::vector<MessageHandler> watchers_;

    std::vector<char> prefixes_{'/'};
    std::string botUsername_;
    TelegramClient* client_ = nullptr;

    mutable std::mutex       qMutex_;
    std::condition_variable  qCv_;
    std::deque<std::string>  queue_;
    std::vector<std::thread> workers_;
    std::size_t              workerCount_ = 4;
    std::size_t              busy_ = 0;
    std::atomic<bool>        running_{false};
};

}

#endif
