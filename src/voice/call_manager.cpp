#include "senpai/voice/call_manager.hpp"

#include <utility>

namespace senpai {

CallManager::CallManager(NtgCallsTransport& transport, Queue& queue, CacheManager& cache)
    : transport_(transport), queue_(queue), cache_(cache) {

    transport_.setStreamEndHandler([this](std::int64_t chatId, StreamKind kind) {
        if (kind == StreamKind::Audio)
            playNext(chatId);
    });
    transport_.setCallClosedHandler([this](std::int64_t chatId) {
        stop(chatId);
    });
}

void CallManager::ensureFilePath(MediaItem& item) {
    if (item.file_path.empty() && cb_.download) {
        auto path = cb_.download(item.id, item.video);
        if (path)
            item.file_path = *path;
    }
}

CallManager::PlayDecision
CallManager::play(std::int64_t chatId, MediaItem item, bool force) {
    std::lock_guard<std::mutex> lk(mutex_);

    if (force) {
        queue_.forceAdd(chatId, item);
        ensureFilePath(item);
        playMediaLocked(chatId, item);
        return {PlayOutcome::StartedNow, 0};
    }

    int position = queue_.add(chatId, item);

    if (position != 0 || cache_.isActiveCall(chatId))
        return {PlayOutcome::Queued, position};

    ensureFilePath(item);
    playMediaLocked(chatId, item);
    return {PlayOutcome::StartedNow, position};
}

void CallManager::playMedia(std::int64_t chatId, MediaItem media, int seekTime) {
    std::lock_guard<std::mutex> lk(mutex_);
    playMediaLocked(chatId, std::move(media), seekTime);
}

void CallManager::playMediaLocked(std::int64_t chatId, MediaItem media, int seekTime) {
    if (media.file_path.empty()) {
        if (cb_.onNotice)
            cb_.onNotice(chatId, Notice::ErrorNoFile);
        playNextLocked(chatId);
        return;
    }

    MediaSource src;
    src.path         = media.file_path;
    src.video        = media.video;
    src.seekSeconds  = seekTime;
    src.audio        = AudioQuality::High;
    src.videoQuality = VideoQuality::HD_720p;

    const PlayResult res = transport_.play(chatId, src);

    switch (res) {
        case PlayResult::Ok:
            if (seekTime == 0) {
                media.time = 1;
                cache_.addCall(chatId);
                std::int64_t msgId = cb_.onNowPlaying ? cb_.onNowPlaying(chatId, media) : 0;
                media.message_id = msgId;

                queue_.replaceCurrent(chatId, media);
            }
            break;

        case PlayResult::FileNotFound:
            if (cb_.onNotice)
                cb_.onNotice(chatId, Notice::ErrorNoFile);
            playNextLocked(chatId);
            break;

        case PlayResult::NoActiveGroupCall:
            stopLocked(chatId);
            if (cb_.onNotice)
                cb_.onNotice(chatId, Notice::ErrorNoCall);
            break;

        case PlayResult::NoAudioSource:
            if (cb_.onNotice)
                cb_.onNotice(chatId, Notice::ErrorNoAudio);
            playNextLocked(chatId);
            break;

        case PlayResult::ServerError:
            stopLocked(chatId);
            if (cb_.onNotice)
                cb_.onNotice(chatId, Notice::ErrorServer);
            break;

        case PlayResult::RtmpUnsupported:
            stopLocked(chatId);
            if (cb_.onNotice)
                cb_.onNotice(chatId, Notice::ErrorRtmp);
            break;
    }
}

void CallManager::replay(std::int64_t chatId) {
    std::lock_guard<std::mutex> lk(mutex_);
    replayLocked(chatId);
}

void CallManager::replayLocked(std::int64_t chatId) {
    if (!cache_.isActiveCall(chatId))
        return;

    auto media = queue_.getCurrent(chatId);
    if (!media)
        return;

    if (cb_.onNotice)
        cb_.onNotice(chatId, Notice::PlayAgain);
    playMediaLocked(chatId, *media);
}

void CallManager::playNext(std::int64_t chatId) {
    std::lock_guard<std::mutex> lk(mutex_);
    playNextLocked(chatId);
}

void CallManager::playNextLocked(std::int64_t chatId) {
    if (int loop = cache_.getLoop(chatId); loop > 0) {
        cache_.setLoop(chatId, loop - 1);
        replayLocked(chatId);
        return;
    }

    auto next = queue_.getNext(chatId);

    if (next && next->message_id != 0) {
        if (cb_.onDeleteMessage)
            cb_.onDeleteMessage(chatId, next->message_id);
        next->message_id = 0;
        queue_.replaceCurrent(chatId, *next);
    }

    if (!next) {
        stopLocked(chatId);
        return;
    }

    if (cb_.onNotice)
        cb_.onNotice(chatId, Notice::PlayNext);

    while (next && next->file_path.empty()) {
        ensureFilePath(*next);
        if (next->file_path.empty()) {
            if (cb_.onNotice)
                cb_.onNotice(chatId, Notice::ErrorNoFile);
            next = queue_.getNext(chatId);
            if (next && next->message_id != 0) {
                if (cb_.onDeleteMessage)
                    cb_.onDeleteMessage(chatId, next->message_id);
                next->message_id = 0;
                queue_.replaceCurrent(chatId, *next);
            }
        }
    }

    if (!next) {
        stopLocked(chatId);
        return;
    }

    queue_.replaceCurrent(chatId, *next);
    playMediaLocked(chatId, *next);
}

bool CallManager::pause(std::int64_t chatId) {
    std::lock_guard<std::mutex> lk(mutex_);
    cache_.setPaused(chatId, true);
    const bool ok = transport_.pause(chatId);
    if (!ok)
        stopLocked(chatId);
    return ok;
}

bool CallManager::resume(std::int64_t chatId) {
    std::lock_guard<std::mutex> lk(mutex_);
    cache_.setPaused(chatId, false);
    const bool ok = transport_.resume(chatId);
    if (!ok)
        stopLocked(chatId);
    return ok;
}

void CallManager::stop(std::int64_t chatId) {
    std::lock_guard<std::mutex> lk(mutex_);
    stopLocked(chatId);
}

void CallManager::stopLocked(std::int64_t chatId) {
    queue_.clear(chatId);
    cache_.removeCall(chatId);
    cache_.setLoop(chatId, 0);
    transport_.stop(chatId);
}

}
