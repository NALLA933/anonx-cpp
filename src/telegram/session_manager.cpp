#include <anonx/telegram/session_manager.hpp>
#include <anonx/audio/ntgcalls_client.hpp>
#include <anonx/core/logger.hpp>

namespace anonx::telegram {

SessionManager::SessionManager()
    : bot_client_(std::make_shared<TDLibClient>("bot", true)),
      assistant_client_(std::make_shared<TDLibClient>("assistant", false)) {}

SessionManager::~SessionManager() {
    stop();
}

SessionManager& SessionManager::instance() {
    static SessionManager manager;
    return manager;
}

bool SessionManager::start(const core::BotConfig& config) {
    ANONX_LOG_INFO("SessionManager", "Starting Telegram bot session...");
    bool bot_started = bot_client_->start(config.api_id, config.api_hash, config.bot_token, config.data_dir);
    if (!bot_started) {
        ANONX_LOG_ERROR("SessionManager", "Failed to initialize primary bot TDLib client.");
        return false;
    }

    if (!config.string_session.empty()) {
        ANONX_LOG_INFO("SessionManager", "Assistant string session detected; starting assistant userbot...");
        bool assistant_started = assistant_client_->start(config.api_id, config.api_hash, config.string_session, config.data_dir);
        has_assistant_ = assistant_started;
    } else {
        ANONX_LOG_INFO("SessionManager", "No assistant session string configured. Running in bot-only mode.");
        has_assistant_ = false;
    }

    // Initialize WebRTC voice bridge
    audio::NTgCallsClient::instance().init();

    return true;
}

void SessionManager::stop() {
    if (bot_client_) {
        bot_client_->stop();
    }
    if (assistant_client_ && has_assistant_) {
        assistant_client_->stop();
    }
    audio::NTgCallsClient::instance().shutdown();
}

std::shared_ptr<TDLibClient> SessionManager::bot_client() const noexcept {
    return bot_client_;
}

std::shared_ptr<TDLibClient> SessionManager::assistant_client() const noexcept {
    return assistant_client_;
}

bool SessionManager::has_assistant() const noexcept {
    return has_assistant_;
}

bool SessionManager::join_voice_chat(int64_t chat_id) {
    ANONX_LOG_INFO("SessionManager", "Attempting to join voice chat for chat ID: ", chat_id);

    // Simulated transport JSON for WebRTC bridge
    nlohmann::json transport_desc = {
        {"chat_id", chat_id},
        {"mode", "rtc"},
        {"codec", "opus"}
    };

    return audio::NTgCallsClient::instance().join_group_call(chat_id, transport_desc.dump());
}

bool SessionManager::leave_voice_chat(int64_t chat_id) {
    return audio::NTgCallsClient::instance().leave_group_call(chat_id);
}

} // namespace anonx::telegram
