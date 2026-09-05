#pragma once

#include <anonx/audio/ffmpeg_pipeline.hpp>
#include <anonx/audio/ntgcalls_client.hpp>
#include <anonx/database/models.hpp>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace anonx::audio {

enum class PlayerState {
    Idle,
    Playing,
    Paused,
    Buffering
};

class AudioStreamer {
public:
    using TrackEndedCallback = std::function<void(int64_t chat_id, const database::TrackItem& track)>;
    using TrackStartedCallback = std::function<void(int64_t chat_id, const database::TrackItem& track)>;

    static AudioStreamer& instance();

    bool play(int64_t chat_id, const database::TrackItem& track);
    bool pause(int64_t chat_id);
    bool resume(int64_t chat_id);
    bool stop(int64_t chat_id);
    bool set_volume(int64_t chat_id, int volume);

    [[nodiscard]] PlayerState get_state(int64_t chat_id) const;
    [[nodiscard]] std::optional<database::TrackItem> get_current_track(int64_t chat_id) const;

    void set_on_track_ended(TrackEndedCallback cb);
    void set_on_track_started(TrackStartedCallback cb);

private:
    AudioStreamer();
    ~AudioStreamer();

    struct ChatSession {
        int64_t chat_id{0};
        database::TrackItem current_track;
        std::atomic<PlayerState> state{PlayerState::Idle};
        std::atomic<int> volume{100};
        std::unique_ptr<FFmpegPipeline> pipeline;
    };

    void on_frame_received(int64_t chat_id, int volume, const uint8_t* pcm_data, size_t size_bytes);
    void on_stream_eof(int64_t chat_id);

    mutable std::mutex sessions_mutex_;
    std::unordered_map<int64_t, std::shared_ptr<ChatSession>> sessions_;

    TrackEndedCallback on_track_ended_;
    TrackStartedCallback on_track_started_;
};

} // namespace anonx::audio
