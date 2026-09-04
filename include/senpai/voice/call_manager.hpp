#ifndef SENPAI_CALL_MANAGER_HPP
#define SENPAI_CALL_MANAGER_HPP

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>

#include "senpai/database/cache_manager.hpp"
#include "senpai/voice/ntgcalls_transport.hpp"
#include "senpai/voice/queue.hpp"

namespace senpai {

class CallManager {
public:

    enum class Notice {
        PlayAgain,
        PlayNext,
        ErrorNoFile,
        ErrorNoCall,
        ErrorNoAudio,
        ErrorServer,
        ErrorRtmp,
    };

    enum class PlayOutcome {
        StartedNow,
        Queued,
    };

    struct PlayDecision {
        PlayOutcome outcome;
        int         position;
    };

    struct Callbacks {
        std::function<std::optional<std::string>(const std::string& videoId, bool video)> download;
        std::function<std::int64_t(std::int64_t chatId, const MediaItem& media)> onNowPlaying;
        std::function<void(std::int64_t chatId, Notice notice)> onNotice;
        std::function<void(std::int64_t chatId, std::int64_t messageId)> onDeleteMessage;
    };

    CallManager(NtgCallsTransport& transport, Queue& queue, CacheManager& cache);

    CallManager(const CallManager&)            = delete;
    CallManager& operator=(const CallManager&) = delete;
    CallManager(CallManager&&)                 = delete;
    CallManager& operator=(CallManager&&)      = delete;

    void setCallbacks(Callbacks cb) { cb_ = std::move(cb); }

    PlayDecision play(std::int64_t chatId, MediaItem item, bool force = false);

    void playMedia(std::int64_t chatId, MediaItem media, int seekTime = 0);

    void replay(std::int64_t chatId);

    void playNext(std::int64_t chatId);

    bool pause(std::int64_t chatId);
    bool resume(std::int64_t chatId);

    void stop(std::int64_t chatId);

    double ping() const { return transport_.ping(); }

private:
    void ensureFilePath(MediaItem& item);

    void playMediaLocked(std::int64_t chatId, MediaItem media, int seekTime = 0);
    void replayLocked(std::int64_t chatId);
    void playNextLocked(std::int64_t chatId);
    void stopLocked(std::int64_t chatId);

    NtgCallsTransport& transport_;
    Queue&             queue_;
    CacheManager&      cache_;
    Callbacks          cb_;

    mutable std::mutex mutex_;
};

}

#endif
