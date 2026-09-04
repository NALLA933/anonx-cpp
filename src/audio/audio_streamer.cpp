#include <anonx/audio/audio_streamer.hpp>
#include <anonx/core/logger.hpp>
#include <algorithm>
#include <cstring>
#include <vector>

namespace anonx::audio {

AudioStreamer::AudioStreamer() = default;

AudioStreamer::~AudioStreamer() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [_, session] : sessions_) {
        if (session && session->pipeline) {
            session->pipeline->stop();
        }
    }
    sessions_.clear();
}

AudioStreamer& AudioStreamer::instance() {
    static AudioStreamer streamer;
    return streamer;
}

bool AudioStreamer::play(int64_t chat_id, const database::TrackItem& track) {
    std::shared_ptr<ChatSession> session;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(chat_id);
        if (it == sessions_.end()) {
            session = std::make_shared<ChatSession>();
            session->chat_id = chat_id;
            session->volume = 100;
            sessions_[chat_id] = session;
        } else {
            session = it->second;
        }
    }

    if (session->pipeline) {
        session->pipeline->stop();
        session->pipeline.reset();
    }

    session->current_track = track;
    session->state = PlayerState::Buffering;

    session->pipeline = std::make_unique<FFmpegPipeline>();

    std::string source = !track.url.empty() ? track.url : track.file_path;

    ANONX_LOG_INFO("AudioStreamer", "Starting playback for track '", track.title, "' in chat: ", chat_id);

    bool started = session->pipeline->start(
        source,
        [this, chat_id](const uint8_t* pcm_data, size_t size_bytes) {
            this->on_frame_received(chat_id, pcm_data, size_bytes);
        },
        [this, chat_id]() {
            this->on_stream_eof(chat_id);
        },
        [chat_id](const std::string& err) {
            ANONX_LOG_ERROR("AudioStreamer", "Pipeline error in chat ", chat_id, ": ", err);
        }
    );

    if (started) {
        session->state = PlayerState::Playing;
        if (on_track_started_) {
            on_track_started_(chat_id, track);
        }
        return true;
    }

    session->state = PlayerState::Idle;
    return false;
}

bool AudioStreamer::pause(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(chat_id);
    if (it != sessions_.end() && it->second->pipeline) {
        it->second->pipeline->pause();
        it->second->state = PlayerState::Paused;
        NTgCallsClient::instance().pause(chat_id);
        return true;
    }
    return false;
}

bool AudioStreamer::resume(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(chat_id);
    if (it != sessions_.end() && it->second->pipeline) {
        it->second->pipeline->resume();
        it->second->state = PlayerState::Playing;
        NTgCallsClient::instance().resume(chat_id);
        return true;
    }
    return false;
}

bool AudioStreamer::stop(int64_t chat_id) {
    std::shared_ptr<ChatSession> session;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(chat_id);
        if (it != sessions_.end()) {
            session = it->second;
            sessions_.erase(it);
        }
    }

    if (session) {
        if (session->pipeline) {
            session->pipeline->stop();
            session->pipeline.reset();
        }
        session->state = PlayerState::Idle;
        NTgCallsClient::instance().leave_group_call(chat_id);
        return true;
    }
    return false;
}

bool AudioStreamer::set_volume(int64_t chat_id, int volume) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(chat_id);
    if (it != sessions_.end()) {
        it->second->volume = std::clamp(volume, 0, 200);
        NTgCallsClient::instance().set_volume(chat_id, it->second->volume);
        return true;
    }
    return false;
}

PlayerState AudioStreamer::get_state(int64_t chat_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(chat_id);
    if (it != sessions_.end()) {
        return it->second->state;
    }
    return PlayerState::Idle;
}

std::optional<database::TrackItem> AudioStreamer::get_current_track(int64_t chat_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(chat_id);
    if (it != sessions_.end() && it->second->state != PlayerState::Idle) {
        return it->second->current_track;
    }
    return std::nullopt;
}

void AudioStreamer::on_frame_received(int64_t chat_id, const uint8_t* pcm_data, size_t size_bytes) {
    int vol = 100;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(chat_id);
        if (it != sessions_.end()) {
            vol = it->second->volume;
        }
    }

    if (vol == 100) {
        NTgCallsClient::instance().send_pcm_frame(chat_id, pcm_data, size_bytes);
    } else {
        // Apply PCM volume scaling to 16-bit signed samples
        size_t sample_count = size_bytes / sizeof(int16_t);
        const auto* in_samples = reinterpret_cast<const int16_t*>(pcm_data);
        std::vector<int16_t> scaled_samples(sample_count);

        float gain = static_cast<float>(vol) / 100.0f;
        for (size_t i = 0; i < sample_count; ++i) {
            float s = static_cast<float>(in_samples[i]) * gain;
            s = std::clamp(s, -32768.0f, 32767.0f);
            scaled_samples[i] = static_cast<int16_t>(s);
        }

        NTgCallsClient::instance().send_pcm_frame(
            chat_id,
            reinterpret_cast<const uint8_t*>(scaled_samples.data()),
            size_bytes
        );
    }
}

void AudioStreamer::on_stream_eof(int64_t chat_id) {
    database::TrackItem finished_track;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(chat_id);
        if (it != sessions_.end()) {
            finished_track = it->second->current_track;
            it->second->state = PlayerState::Idle;
        }
    }

    ANONX_LOG_INFO("AudioStreamer", "Track playback ended in chat: ", chat_id);

    if (on_track_ended_) {
        on_track_ended_(chat_id, finished_track);
    }
}

void AudioStreamer::set_on_track_ended(TrackEndedCallback cb) {
    on_track_ended_ = std::move(cb);
}

void AudioStreamer::set_on_track_started(TrackStartedCallback cb) {
    on_track_started_ = std::move(cb);
}

} // namespace anonx::audio
