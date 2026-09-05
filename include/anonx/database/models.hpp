#pragma once

#include <cstdint>
#include <string>
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

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(
    TrackItem,
    id,
    title,
    url,
    file_path,
    stream_type,
    duration_seconds,
    requester_id,
    requester_name,
    added_timestamp
)

inline nlohmann::json TrackItem::to_json() const {
    return *this;
}

inline TrackItem TrackItem::from_json(const nlohmann::json& j) {
    return j.get<TrackItem>();
}

} // namespace anonx::database
