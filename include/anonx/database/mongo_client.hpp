#pragma once

#include <anonx/database/models.hpp>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace anonx::database {

class MongoClient {
public:
    static MongoClient& instance();

    bool connect(const std::string& uri_string = "", const std::string& db_name = "");
    void disconnect();
    [[nodiscard]] bool is_connected() const noexcept;

    // Track Queue Management
    bool enqueue_track(int64_t chat_id, const TrackItem& track);
    std::vector<TrackItem> get_queue(int64_t chat_id);
    std::optional<TrackItem> pop_next_track(int64_t chat_id);
    bool clear_queue(int64_t chat_id);
    bool remove_track_at(int64_t chat_id, size_t index);

    // Whitelist / Blacklist
    bool set_chat_blocked(int64_t chat_id, bool blocked);
    bool is_chat_blocked(int64_t chat_id);

private:
    MongoClient() = default;
    ~MongoClient() = default;

    MongoClient(const MongoClient&) = delete;
    MongoClient& operator=(const MongoClient&) = delete;

    mutable std::mutex mutex_;
    bool connected_{false};
    std::unordered_map<int64_t, std::deque<TrackItem>> queues_;
    std::unordered_set<int64_t> blocked_chats_;
};

} // namespace anonx::database
