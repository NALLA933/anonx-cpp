#ifndef SENPAI_PLUGINS_HPP
#define SENPAI_PLUGINS_HPP

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "senpai/core/config.hpp"
#include "senpai/database/cache_manager.hpp"
#include "senpai/database/database.hpp"
#include "senpai/plugins/lang.hpp"
#include "senpai/telegram/dispatcher.hpp"
#include "senpai/telegram/telegram_client.hpp"
#include "senpai/utils/youtube.hpp"
#include "senpai/voice/call_manager.hpp"
#include "senpai/voice/queue.hpp"

namespace senpai {

class Plugins {
public:
    struct Deps {
        TelegramClient& api;
        Database&       db;
        CacheManager&   cache;
        Queue&          queue;
        YouTube&        yt;
        CallManager&    calls;
        const Language& lang;
        const Config&   config;
    };

    explicit Plugins(const Deps& deps);

    void attachCallbacks();

    CallManager::Callbacks callbacks();

    void onPlay(const MessageContext& ev);
    void onSkip(const MessageContext& ev);
    void onPause(const MessageContext& ev);
    void onResume(const MessageContext& ev);
    void onStop(const MessageContext& ev);
    void onLoop(const MessageContext& ev);
    void onQueue(const MessageContext& ev);
    void onSeek(const MessageContext& ev);

    void onControls(const CallbackContext& ev);

    static std::vector<std::string> playCommands();
    static std::vector<std::string> skipCommands();
    static std::vector<std::string> pauseCommands();
    static std::vector<std::string> resumeCommands();
    static std::vector<std::string> stopCommands();
    static std::vector<std::string> loopCommands();
    static std::vector<std::string> queueCommands();
    static std::vector<std::string> seekCommands();

    static bool isSupergroupId(std::int64_t chatId);

private:
    LangView tr(std::int64_t chatId) const;

    void         setStatus(std::int64_t chatId, std::int64_t messageId);
    std::int64_t takeStatus(std::int64_t chatId);

    std::int64_t say(std::int64_t chatId, const std::string& html,
                     const InlineKeyboard& kb = {});

    bool requireControl(const MessageContext& ev, const LangView& L);

    std::string nowPlayingCard(const LangView& L, const MediaItem& item) const;
    std::string queuedCard(const LangView& L, const MediaItem& item, int position) const;

    std::int64_t renderNowPlaying(std::int64_t chatId, const MediaItem& item);
    void         renderNotice(std::int64_t chatId, CallManager::Notice notice);

    TelegramClient& api_;
    Database&       db_;
    CacheManager&   cache_;
    Queue&          queue_;
    YouTube&        yt_;
    CallManager&    calls_;
    const Language& lang_;
    const Config&   config_;

    mutable std::mutex                   mutex_;
    std::map<std::int64_t, std::int64_t> status_;
};

}

#endif
