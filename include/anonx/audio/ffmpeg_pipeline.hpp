#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace anonx::audio {

constexpr size_t PCM_SAMPLE_RATE = 48000;
constexpr size_t PCM_CHANNELS = 2;
constexpr size_t PCM_FRAME_DURATION_MS = 20;
constexpr size_t SAMPLES_PER_FRAME = (PCM_SAMPLE_RATE * PCM_FRAME_DURATION_MS) / 1000; // 960
constexpr size_t PCM_FRAME_BYTES = SAMPLES_PER_FRAME * PCM_CHANNELS * sizeof(int16_t); // 3840 bytes

class FFmpegPipeline {
public:
    using FrameCallback = std::function<void(const uint8_t* pcm_data, size_t size_bytes)>;
    using EofCallback = std::function<void()>;
    using ErrorCallback = std::function<void(const std::string& error_message)>;

    FFmpegPipeline();
    ~FFmpegPipeline();

    // Start decoding audio from URL or file
    bool start(const std::string& source_uri,
               FrameCallback on_frame,
               EofCallback on_eof = nullptr,
               ErrorCallback on_error = nullptr);

    void stop();
    void pause();
    void resume();

    [[nodiscard]] bool is_running() const noexcept;
    [[nodiscard]] bool is_paused() const noexcept;

    // Helper: Resolves YouTube / media URL to direct audio stream using yt-dlp (with TTL cache)
    static std::string resolve_stream_url(const std::string& input_query);
    static void clear_stream_url_cache();

private:
    void reader_thread_loop(const std::string& source_uri,
                            FrameCallback on_frame,
                            EofCallback on_eof,
                            ErrorCallback on_error);

    std::atomic<bool> is_running_{false};
    std::atomic<bool> is_paused_{false};
    std::thread worker_thread_;

#if defined(_WIN32)
    void* process_handle_{nullptr};
    void* pipe_read_{nullptr};
#else
    pid_t child_pid_{-1};
    int pipe_fd_{-1};
#endif
};

} // namespace anonx::audio
