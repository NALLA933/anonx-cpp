#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace anonx::audio {

enum class ConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Failed,
    Closed
};

class NTgCallsClient {
public:
    using StreamEndCallback = std::function<void(int64_t chat_id)>;
    using StateCallback = std::function<void(int64_t chat_id, ConnectionState state)>;

    static NTgCallsClient& instance();

    bool init();
    void shutdown();

    // Voice Chat Connection
    bool join_group_call(int64_t chat_id, const std::string& transport_json);
    bool leave_group_call(int64_t chat_id);

    // Audio Frame Injection
    bool send_pcm_frame(int64_t chat_id, const uint8_t* pcm_data, size_t size_bytes);

    // Controls
    bool pause(int64_t chat_id);
    bool resume(int64_t chat_id);
    bool set_volume(int64_t chat_id, int volume); // 0-200%

    // Callbacks
    void set_on_stream_end(StreamEndCallback cb);
    void set_on_state_change(StateCallback cb);

    [[nodiscard]] bool is_initialized() const noexcept;
    [[nodiscard]] bool is_in_call(int64_t chat_id) const noexcept;

private:
    NTgCallsClient();
    ~NTgCallsClient();

    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace anonx::audio
