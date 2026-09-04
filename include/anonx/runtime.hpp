#ifndef ANONX_RUNTIME_HPP
#define ANONX_RUNTIME_HPP

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "anonx/admin_plugins.hpp"
#include "anonx/cache_manager.hpp"
#include "anonx/config.hpp"
#include "anonx/database.hpp"
#include "anonx/dispatcher.hpp"
#include "anonx/lang.hpp"
#include "anonx/plugins.hpp"
#include "anonx/queue.hpp"
#include "anonx/sysinfo.hpp"
#include "anonx/telegram_bot_api.hpp"
#include "anonx/telegram_client.hpp"
#include "anonx/userbot.hpp"
#include "anonx/voice_transport.hpp"
#include "anonx/youtube.hpp"

namespace anonx {

struct RuntimeOptions {

    std::string localesDir = "locales";

    std::string botSessionDir = "tdlib/bot";

    bool interactiveAssistantLogin = true;

    bool bootAssistants = true;
};

class Runtime {
public:
    using Options = RuntimeOptions;

    Runtime(const Config& config, VoiceTransport& transport, Options opts = {});
    ~Runtime();

    Runtime(const Runtime&)            = delete;
    Runtime& operator=(const Runtime&) = delete;

    bool start();

    void stop();

    std::size_t assistantsUp() const;

    Database&       db()         { return db_; }
    CacheManager&   cache()      { return cache_; }
    Language&       lang()       { return lang_; }
    Queue&          queue()      { return queue_; }
    CallManager&    calls()      { return calls_; }
    Plugins&        plugins()    { return plugins_; }
    AdminPlugins&   admin()      { return admin_; }
    Dispatcher&     dispatcher() { return *dispatcher_; }
    TelegramClient& bot()        { return bot_; }
    Userbot&        userbot()    { return userbot_; }
    BotApi&         api()        { return api_; }

private:
    void announceStartup();

    const Config& config_;
    Options       opts_;

    Database     db_;
    CacheManager cache_;
    Language     lang_;
    Queue        queue_;
    YouTube      yt_;
    SystemInfo   sys_;

    TelegramClient bot_;
    Userbot        userbot_;
    TelegramBotApi api_;

    CallManager  calls_;
    Plugins      plugins_;
    AdminPlugins admin_;

    std::unique_ptr<Dispatcher> dispatcher_;

    std::atomic<bool> started_{false};
    std::atomic<bool> stopped_{false};
};

}

#endif
