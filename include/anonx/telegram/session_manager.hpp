#pragma once

#include <anonx/core/config.hpp>
#include <anonx/telegram/tdlib_client.hpp>
#include <memory>
#include <string>

namespace anonx::telegram {

class SessionManager {
public:
    static SessionManager& instance();

    bool start(const core::BotConfig& config);
    void stop();

    [[nodiscard]] std::shared_ptr<TDLibClient> bot_client() const noexcept;
    [[nodiscard]] std::shared_ptr<TDLibClient> assistant_client() const noexcept;
    [[nodiscard]] bool has_assistant() const noexcept;

    // Joins the group voice chat using assistant (or bot if assistant unavailable)
    bool join_voice_chat(int64_t chat_id);
    bool leave_voice_chat(int64_t chat_id);

private:
    SessionManager();
    ~SessionManager();

    std::shared_ptr<TDLibClient> bot_client_;
    std::shared_ptr<TDLibClient> assistant_client_;
    bool has_assistant_{false};
};

} // namespace anonx::telegram
