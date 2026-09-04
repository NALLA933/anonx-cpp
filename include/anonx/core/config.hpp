#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_set>
#include <optional>
#include <nlohmann/json.hpp>

namespace anonx::core {

struct BotConfig {
    // Telegram Credentials
    std::string bot_token;
    int32_t api_id{0};
    std::string api_hash;
    std::string string_session; // Assistant session string or empty for bot-only

    // Administration & Permissions
    int64_t owner_id{0};
    std::unordered_set<int64_t> sudo_users;

    // Infrastructure & Storage
    std::string mongo_uri{"mongodb://localhost:27017/anonx"};
    std::string db_name{"anonx"};
    int64_t log_group_id{0};

    // Audio & Streaming Settings
    std::string audio_quality{"high"}; // low (64k), medium (128k), high (320k)
    int32_t duration_limit_sec{7200};  // Default 2 hours
    bool auto_leave_vc{true};
    int32_t auto_leave_delay_sec{300}; // Leave voice chat if idle for 5 mins

    // System & Paths
    std::string log_level{"info"};
    std::string data_dir{"data"};
    std::string downloads_dir{"downloads"};
    bool auto_update_ytdlp{true};

    // Helper methods
    [[nodiscard]] bool is_sudo(int64_t user_id) const noexcept {
        return (user_id == owner_id) || (sudo_users.find(user_id) != sudo_users.end());
    }

    [[nodiscard]] bool is_valid() const noexcept {
        return !bot_token.empty() && (api_id != 0) && !api_hash.empty();
    }
};

class ConfigLoader {
public:
    static BotConfig load(const std::string& config_path = "config.json");

private:
    static void apply_env_overrides(BotConfig& config);
    static std::vector<std::string> split_string(std::string_view s, char delimiter);
};

} // namespace anonx::core
