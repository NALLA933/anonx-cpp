#ifndef ANONX_CALL_MANAGER_HPP
#define ANONX_CALL_MANAGER_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "anonx/cache_manager.hpp"
#include "anonx/queue.hpp"
#include "anonx/voice_transport.hpp"

namespace anonx {

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

    CallManager(VoiceTransport& transport, Queue& queue, CacheManager& cache);

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

    std::unique_lock<std::recursive_mutex> lockFor(std::int64_t chatId);

    VoiceTransport& transport_;
    Queue&          queue_;
    CacheManager&   cache_;
    Callbacks       cb_;

    mutable std::mutex locksMtx_;
    std::unordered_map<std::int64_t, std::unique_ptr<std::recursive_mutex>> locks_;
};

}

#endif
