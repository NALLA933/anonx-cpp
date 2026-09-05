#pragma once

#include <anonx/core/config.hpp>
#include <anonx/core/rate_limiter.hpp>
#include <anonx/core/thread_pool.hpp>
#include <anonx/telegram/tdlib_client.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace anonx::telegram {

class CommandDispatcher {
public:
    static CommandDispatcher& instance();

    void init(const core::BotConfig& config, std::shared_ptr<TDLibClient> client);

    void dispatch_message(const nlohmann::json& message);

private:
    CommandDispatcher();
    ~CommandDispatcher() = default;

    void execute_command(const std::string& cmd, const std::string& args,
                         int64_t chat_id, int64_t sender_user_id, int64_t msg_id);

    // Command Handlers
    void handle_play(int64_t chat_id, int64_t user_id, int64_t msg_id, const std::string& query);
    void handle_skip(int64_t chat_id, int64_t user_id, int64_t msg_id);
    void handle_pause(int64_t chat_id, int64_t user_id, int64_t msg_id);
    void handle_resume(int64_t chat_id, int64_t user_id, int64_t msg_id);
    void handle_stop(int64_t chat_id, int64_t user_id, int64_t msg_id);
    void handle_queue(int64_t chat_id, int64_t user_id, int64_t msg_id);
    void handle_volume(int64_t chat_id, int64_t user_id, int64_t msg_id, const std::string& arg);
    void handle_ping(int64_t chat_id, int64_t msg_id);
    void handle_help(int64_t chat_id, int64_t msg_id);

    // Track Autoplay from Queue
    void play_next_in_queue(int64_t chat_id);

    core::BotConfig config_;
    std::shared_ptr<TDLibClient> client_;
    std::unique_ptr<core::ThreadPool> thread_pool_;
    std::unique_ptr<core::RateLimiter> rate_limiter_;
    int64_t start_time_{0};
};

} // namespace anonx::telegram
