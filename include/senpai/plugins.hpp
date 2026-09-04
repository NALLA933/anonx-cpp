#ifndef SENPAI_PLUGINS_HPP
#define SENPAI_PLUGINS_HPP

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "senpai/bot_api.hpp"
#include "senpai/cache_manager.hpp"
#include "senpai/call_manager.hpp"
#include "senpai/config.hpp"
#include "senpai/database.hpp"
#include "senpai/lang.hpp"
#include "senpai/queue.hpp"
#include "senpai/youtube.hpp"

namespace senpai {

struct CommandEvent {
    std::int64_t chatId = 0;
    std::int64_t messageId = 0;
    std::int64_t fromUserId = 0;
    bool         isPrivate = false;
    std::vector<std::string> command;

    std::int64_t replyToMessageId = 0;

    bool hasReply() const { return replyToMessageId != 0; }
};

struct ButtonEvent {
    std::int64_t chatId = 0;
    std::int64_t messageId = 0;
    std::int64_t fromUserId = 0;
    std::int64_t queryId = 0;
    std::string  data;
};

class Plugins {
public:
    struct Deps {
        BotApi&         api;
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

    void onPlay(const CommandEvent& ev);
    void onSkip(const CommandEvent& ev);
    void onPause(const CommandEvent& ev);
    void onResume(const CommandEvent& ev);
    void onStop(const CommandEvent& ev);
    void onLoop(const CommandEvent& ev);
    void onQueue(const CommandEvent& ev);
    void onSeek(const CommandEvent& ev);

    void onControls(const ButtonEvent& ev);

    static std::vector<std::string> playCommands();
    static std::vector<std::string> skipCommands();
    static std::vector<std::string> pauseCommands();
    static std::vector<std::string> resumeCommands();
    static std::vector<std::string> stopCommands();
    static std::vector<std::string> loopCommands();
    static std::vector<std::string> queueCommands();
    static std::vector<std::string> seekCommands();

    static bool isSupergroupId(std::int64_t chatId);

    static std::string htmlEscape(const std::string& text);

private:
    LangView tr(std::int64_t chatId) const;

    void         setStatus(std::int64_t chatId, std::int64_t messageId);
    std::int64_t takeStatus(std::int64_t chatId);

    std::int64_t say(std::int64_t chatId, const std::string& html,
                     const InlineKeyboard& kb = {});

    bool requireControl(const CommandEvent& ev, const LangView& L);

    std::string nowPlayingCard(const LangView& L, const MediaItem& item) const;
    std::string queuedCard(const LangView& L, const MediaItem& item, int position) const;

    std::int64_t renderNowPlaying(std::int64_t chatId, const MediaItem& item);
    void         renderNotice(std::int64_t chatId, CallManager::Notice notice);

    BotApi&         api_;
    Database&       db_;
    CacheManager&   cache_;
    Queue&          queue_;
    YouTube&        yt_;
    CallManager&    calls_;
    const Language& lang_;
    const Config&   config_;

    mutable std::mutex                    mutex_;
    std::map<std::int64_t, std::int64_t>  status_;
};

}

#endif
