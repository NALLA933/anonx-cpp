#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace anonx::database {

struct TrackItem {
    std::string id;
    std::string title;
    std::string url;
    std::string file_path;
    std::string stream_type{"youtube"}; // "youtube", "direct", "telegram"
    int32_t duration_seconds{0};
    int64_t requester_id{0};
    std::string requester_name;
    int64_t added_timestamp{0};

    [[nodiscard]] nlohmann::json to_json() const;
    static TrackItem from_json(const nlohmann::json& j);
};

struct ChatSettings {
    int64_t chat_id{0};
    int64_t channel_id{0};
    int32_t volume{100};
    bool is_muted{false};
    bool is_paused{false};
    bool loop_queue{false};
    std::string language{"en"};
    bool admin_only{false};

    [[nodiscard]] nlohmann::json to_json() const;
    static ChatSettings from_json(const nlohmann::json& j);
};

struct SudoEntry {
    int64_t user_id{0};
    std::string username;
    int64_t added_by{0};
    int64_t added_at{0};

    [[nodiscard]] nlohmann::json to_json() const;
    static SudoEntry from_json(const nlohmann::json& j);
};

} // namespace anonx::database
