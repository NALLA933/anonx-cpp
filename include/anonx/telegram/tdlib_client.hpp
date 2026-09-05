#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace anonx::telegram {

enum class AuthState {
    None,
    WaitParameters,
    WaitPhoneNumber,
    WaitCode,
    WaitPassword,
    WaitBotToken,
    Ready,
    LoggingOut,
    Closed
};

class TDLibClient {
public:
    using UpdateCallback = std::function<void(const nlohmann::json& update)>;
    using ResponseCallback = std::function<void(const nlohmann::json& response)>;

    explicit TDLibClient(std::string session_name = "bot", bool is_bot = true);
    ~TDLibClient();

    TDLibClient(const TDLibClient&) = delete;
    TDLibClient& operator=(const TDLibClient&) = delete;

    bool start(int32_t api_id,
               const std::string& api_hash,
               const std::string& token_or_phone,
               const std::string& data_dir = "data");
    void stop();

    // Send asynchronous request without blocking
    void send_request(const nlohmann::json& request, ResponseCallback on_response = nullptr);

    // Message sending helper
    void send_message(int64_t chat_id,
                      const std::string& text,
                      int64_t reply_to_message_id = 0,
                      ResponseCallback on_sent = nullptr);

    // Update listeners
    void add_update_listener(const std::string& update_type, UpdateCallback callback);

    [[nodiscard]] AuthState auth_state() const noexcept;
    [[nodiscard]] int client_id() const noexcept;

private:
    void receiver_loop();
    void process_update(const nlohmann::json& update);
    void handle_auth_state(const nlohmann::json& state);

    std::string session_name_;
    bool is_bot_{true};
    int client_id_{-1};
    int32_t api_id_{0};
    std::string api_hash_;
    std::string token_or_phone_;
    std::string data_dir_;

    std::atomic<AuthState> auth_state_{AuthState::None};
    std::atomic<bool> is_running_{false};
    std::thread receiver_thread_;

    mutable std::mutex callback_mutex_;
    std::atomic<uint64_t> current_request_id_{1};
    std::unordered_map<std::string, ResponseCallback> pending_callbacks_;

    std::unordered_map<std::string, std::vector<UpdateCallback>> update_listeners_;
};

} // namespace anonx::telegram
