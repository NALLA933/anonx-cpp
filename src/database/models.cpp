#include <anonx/database/models.hpp>

namespace anonx::database {

nlohmann::json TrackItem::to_json() const {
    return {
        {"id", id},
        {"title", title},
        {"url", url},
        {"file_path", file_path},
        {"stream_type", stream_type},
        {"duration_seconds", duration_seconds},
        {"requester_id", requester_id},
        {"requester_name", requester_name},
        {"added_timestamp", added_timestamp}
    };
}

TrackItem TrackItem::from_json(const nlohmann::json& j) {
    TrackItem t;
    if (j.contains("id")) t.id = j["id"].get<std::string>();
    if (j.contains("title")) t.title = j["title"].get<std::string>();
    if (j.contains("url")) t.url = j["url"].get<std::string>();
    if (j.contains("file_path")) t.file_path = j["file_path"].get<std::string>();
    if (j.contains("stream_type")) t.stream_type = j["stream_type"].get<std::string>();
    if (j.contains("duration_seconds")) t.duration_seconds = j["duration_seconds"].get<int32_t>();
    if (j.contains("requester_id")) t.requester_id = j["requester_id"].get<int64_t>();
    if (j.contains("requester_name")) t.requester_name = j["requester_name"].get<std::string>();
    if (j.contains("added_timestamp")) t.added_timestamp = j["added_timestamp"].get<int64_t>();
    return t;
}

nlohmann::json ChatSettings::to_json() const {
    return {
        {"chat_id", chat_id},
        {"channel_id", channel_id},
        {"volume", volume},
        {"is_muted", is_muted},
        {"is_paused", is_paused},
        {"loop_queue", loop_queue},
        {"language", language},
        {"admin_only", admin_only}
    };
}

ChatSettings ChatSettings::from_json(const nlohmann::json& j) {
    ChatSettings c;
    if (j.contains("chat_id")) c.chat_id = j["chat_id"].get<int64_t>();
    if (j.contains("channel_id")) c.channel_id = j["channel_id"].get<int64_t>();
    if (j.contains("volume")) c.volume = j["volume"].get<int32_t>();
    if (j.contains("is_muted")) c.is_muted = j["is_muted"].get<bool>();
    if (j.contains("is_paused")) c.is_paused = j["is_paused"].get<bool>();
    if (j.contains("loop_queue")) c.loop_queue = j["loop_queue"].get<bool>();
    if (j.contains("language")) c.language = j["language"].get<std::string>();
    if (j.contains("admin_only")) c.admin_only = j["admin_only"].get<bool>();
    return c;
}

nlohmann::json SudoEntry::to_json() const {
    return {
        {"user_id", user_id},
        {"username", username},
        {"added_by", added_by},
        {"added_at", added_at}
    };
}

SudoEntry SudoEntry::from_json(const nlohmann::json& j) {
    SudoEntry s;
    if (j.contains("user_id")) s.user_id = j["user_id"].get<int64_t>();
    if (j.contains("username")) s.username = j["username"].get<std::string>();
    if (j.contains("added_by")) s.added_by = j["added_by"].get<int64_t>();
    if (j.contains("added_at")) s.added_at = j["added_at"].get<int64_t>();
    return s;
}

} // namespace anonx::database
