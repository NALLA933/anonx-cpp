#include <anonx/database/mongo_client.hpp>
#include <anonx/core/logger.hpp>

namespace anonx::database {

MongoClient& MongoClient::instance() {
    static MongoClient client;
    return client;
}

bool MongoClient::connect([[maybe_unused]] const std::string& uri_string, [[maybe_unused]] const std::string& db_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    connected_ = true;
    ANONX_LOG_INFO("Database", "Database initialized (in-memory queue store).");
    return true;
}

void MongoClient::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    connected_ = false;
    queues_.clear();
    blocked_chats_.clear();
    ANONX_LOG_INFO("Database", "Database disconnected.");
}

bool MongoClient::is_connected() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return connected_;
}

bool MongoClient::enqueue_track(int64_t chat_id, const TrackItem& track) {
    std::lock_guard<std::mutex> lock(mutex_);
    queues_[chat_id].push_back(track);
    return true;
}

std::vector<TrackItem> MongoClient::get_queue(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = queues_.find(chat_id);
    if (it != queues_.end()) {
        return std::vector<TrackItem>(it->second.begin(), it->second.end());
    }
    return {};
}

std::optional<TrackItem> MongoClient::pop_next_track(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = queues_.find(chat_id);
    if (it != queues_.end() && !it->second.empty()) {
        TrackItem item = std::move(it->second.front());
        it->second.pop_front();
        return item;
    }
    return std::nullopt;
}

bool MongoClient::clear_queue(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    queues_[chat_id].clear();
    return true;
}

bool MongoClient::remove_track_at(int64_t chat_id, size_t index) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = queues_.find(chat_id);
    if (it != queues_.end() && index < it->second.size()) {
        it->second.erase(it->second.begin() + static_cast<std::ptrdiff_t>(index));
        return true;
    }
    return false;
}

bool MongoClient::set_chat_blocked(int64_t chat_id, bool blocked) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (blocked) {
        blocked_chats_.insert(chat_id);
    } else {
        blocked_chats_.erase(chat_id);
    }
    return true;
}

bool MongoClient::is_chat_blocked(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return blocked_chats_.find(chat_id) != blocked_chats_.end();
}

} // namespace anonx::database

