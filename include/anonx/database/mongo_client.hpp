#pragma once

#include <anonx/database/models.hpp>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace anonx::database {

class MongoClient {
public:
    static MongoClient& instance();

    bool connect(const std::string& uri_string, const std::string& db_name = "anonx");
    void disconnect();
    [[nodiscard]] bool is_connected() const noexcept;

    // Chat Settings
    std::optional<ChatSettings> get_chat_settings(int64_t chat_id);
    bool save_chat_settings(const ChatSettings& settings);

    // Track Queue Management
    bool enqueue_track(int64_t chat_id, const TrackItem& track);
    std::vector<TrackItem> get_queue(int64_t chat_id);
    std::optional<TrackItem> pop_next_track(int64_t chat_id);
    bool clear_queue(int64_t chat_id);
    bool remove_track_at(int64_t chat_id, size_t index);

    // Sudo Users Management
    bool add_sudo(int64_t user_id, const std::string& username, int64_t added_by);
    bool remove_sudo(int64_t user_id);
    bool is_sudo(int64_t user_id);
    std::vector<SudoEntry> get_all_sudos();

    // Whitelist / Blacklist
    bool set_chat_blocked(int64_t chat_id, bool blocked);
    bool is_chat_blocked(int64_t chat_id);

private:
    MongoClient();
    ~MongoClient();

    // PIMPL pattern to encapsulate mongocxx driver headers
    struct Impl;
    std::unique_ptr<Impl> pimpl_;

    // Thread-safe in-memory fallback cache (used when Mongo is unreachable or in mock mode)
    mutable std::mutex memory_mtx_;
    std::unordered_map<int64_t, ChatSettings> memory_chat_settings_;
    std::unordered_map<int64_t, std::vector<TrackItem>> memory_queues_;
    std::unordered_map<int64_t, SudoEntry> memory_sudos_;
    std::unordered_map<int64_t, bool> memory_blocked_chats_;
};

} // namespace anonx::database
