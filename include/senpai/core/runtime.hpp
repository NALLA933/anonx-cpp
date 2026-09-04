#ifndef SENPAI_RUNTIME_HPP
#define SENPAI_RUNTIME_HPP

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "senpai/plugins/admin_plugins.hpp"
#include "senpai/database/cache_manager.hpp"
#include "senpai/core/config.hpp"
#include "senpai/database/database.hpp"
#include "senpai/telegram/dispatcher.hpp"
#include "senpai/plugins/lang.hpp"
#include "senpai/plugins/plugins.hpp"
#include "senpai/voice/queue.hpp"
#include "senpai/utils/sysinfo.hpp"
#include "senpai/voice/ntgcalls_transport.hpp"
#include "senpai/utils/youtube.hpp"

namespace senpai {

struct RuntimeOptions {

    std::string localesDir = "locales";

    std::string botSessionDir = "tdlib/bot";

    bool interactiveAssistantLogin = true;

    bool bootAssistants = true;
};

class Runtime {
public:
    using Options = RuntimeOptions;

    Runtime(const Config& config, NtgCallsTransport& transport, Options opts = {});
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
    TelegramClient& api()        { return bot_; }

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

    CallManager  calls_;
    Plugins      plugins_;
    AdminPlugins admin_;

    std::unique_ptr<Dispatcher> dispatcher_;

    std::atomic<bool> started_{false};
    std::atomic<bool> stopped_{false};
};

}

#endif
