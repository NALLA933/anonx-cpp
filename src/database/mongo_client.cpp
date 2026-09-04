#include <anonx/database/mongo_client.hpp>
#include <anonx/core/logger.hpp>
#include <chrono>

#if __has_include(<mongocxx/client.hpp>) && __has_include(<mongocxx/instance.hpp>) && __has_include(<mongocxx/pool.hpp>)
#define ANONX_HAS_MONGOCXX 1
#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/pool.hpp>
#include <mongocxx/uri.hpp>
#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/builder/stream/helpers.hpp>
#include <bsoncxx/json.hpp>
#else
#define ANONX_HAS_MONGOCXX 0
#endif

namespace anonx::database {

struct MongoClient::Impl {
    bool connected{false};
    std::string db_name{"anonx"};

#if ANONX_HAS_MONGOCXX
    std::unique_ptr<mongocxx::instance> instance;
    std::unique_ptr<mongocxx::pool> pool;
#endif
};

MongoClient::MongoClient() : pimpl_(std::make_unique<Impl>()) {}

MongoClient::~MongoClient() {
    disconnect();
}

MongoClient& MongoClient::instance() {
    static MongoClient client;
    return client;
}

bool MongoClient::connect(const std::string& uri_string, const std::string& db_name) {
    pimpl_->db_name = db_name;

#if ANONX_HAS_MONGOCXX
    try {
        if (!pimpl_->instance) {
            pimpl_->instance = std::make_unique<mongocxx::instance>();
        }
        mongocxx::uri uri(uri_string);
        pimpl_->pool = std::make_unique<mongocxx::pool>(uri);

        // Ping database to verify connection
        auto client = pimpl_->pool->acquire();
        auto db = (*client)[db_name];
        bsoncxx::builder::stream::document ping_cmd;
        ping_cmd << "ping" << 1;
        db.run_command(ping_cmd.view());

        pimpl_->connected = true;
        ANONX_LOG_INFO("MongoDB", "Successfully connected to MongoDB cluster: ", db_name);
        return true;
    } catch (const std::exception& ex) {
        ANONX_LOG_WARN("MongoDB", "Could not connect to MongoDB (", ex.what(), "). Activating in-memory fallback store.");
        pimpl_->connected = false;
        return false;
    }
#else
    ANONX_LOG_INFO("MongoDB", "Compiled without mongocxx or in offline mode. Active in-memory storage.");
    pimpl_->connected = false;
    return true;
#endif
}

void MongoClient::disconnect() {
    pimpl_->connected = false;
#if ANONX_HAS_MONGOCXX
    pimpl_->pool.reset();
#endif
}

bool MongoClient::is_connected() const noexcept {
    return pimpl_->connected;
}

std::optional<ChatSettings> MongoClient::get_chat_settings(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(memory_mtx_);
    auto it = memory_chat_settings_.find(chat_id);
    if (it != memory_chat_settings_.end()) {
        return it->second;
    }

#if ANONX_HAS_MONGOCXX
    if (pimpl_->connected && pimpl_->pool) {
        try {
            auto client = pimpl_->pool->acquire();
            auto coll = (*client)[pimpl_->db_name]["chat_settings"];
            bsoncxx::builder::stream::document filter;
            filter << "chat_id" << chat_id;
            auto doc = coll.find_one(filter.view());
            if (doc) {
                auto json_str = bsoncxx::to_json(*doc);
                auto j = nlohmann::json::parse(json_str);
                ChatSettings s = ChatSettings::from_json(j);
                memory_chat_settings_[chat_id] = s;
                return s;
            }
        } catch (const std::exception& ex) {
            ANONX_LOG_ERROR("MongoDB", "Error querying chat settings: ", ex.what());
        }
    }
#endif

    // Default chat settings
    ChatSettings def;
    def.chat_id = chat_id;
    return def;
}

bool MongoClient::save_chat_settings(const ChatSettings& settings) {
    {
        std::lock_guard<std::mutex> lock(memory_mtx_);
        memory_chat_settings_[settings.chat_id] = settings;
    }

#if ANONX_HAS_MONGOCXX
    if (pimpl_->connected && pimpl_->pool) {
        try {
            auto client = pimpl_->pool->acquire();
            auto coll = (*client)[pimpl_->db_name]["chat_settings"];

            std::string json_str = settings.to_json().dump();
            auto bson_doc = bsoncxx::from_json(json_str);

            bsoncxx::builder::stream::document filter;
            filter << "chat_id" << settings.chat_id;

            mongocxx::options::replace opts;
            opts.upsert(true);
            coll.replace_one(filter.view(), bson_doc.view(), opts);
            return true;
        } catch (const std::exception& ex) {
            ANONX_LOG_ERROR("MongoDB", "Error saving chat settings: ", ex.what());
            return false;
        }
    }
#endif
    return true;
}

bool MongoClient::enqueue_track(int64_t chat_id, const TrackItem& track) {
    std::lock_guard<std::mutex> lock(memory_mtx_);
    memory_queues_[chat_id].push_back(track);
    return true;
}

std::vector<TrackItem> MongoClient::get_queue(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(memory_mtx_);
    auto it = memory_queues_.find(chat_id);
    if (it != memory_queues_.end()) {
        return it->second;
    }
    return {};
}

std::optional<TrackItem> MongoClient::pop_next_track(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(memory_mtx_);
    auto it = memory_queues_.find(chat_id);
    if (it != memory_queues_.end() && !it->second.empty()) {
        TrackItem item = it->second.front();
        it->second.erase(it->second.begin());
        return item;
    }
    return std::nullopt;
}

bool MongoClient::clear_queue(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(memory_mtx_);
    memory_queues_[chat_id].clear();
    return true;
}

bool MongoClient::remove_track_at(int64_t chat_id, size_t index) {
    std::lock_guard<std::mutex> lock(memory_mtx_);
    auto it = memory_queues_.find(chat_id);
    if (it != memory_queues_.end() && index < it->second.size()) {
        it->second.erase(it->second.begin() + static_cast<std::ptrdiff_t>(index));
        return true;
    }
    return false;
}

bool MongoClient::add_sudo(int64_t user_id, const std::string& username, int64_t added_by) {
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    SudoEntry entry{user_id, username, added_by, now};
    {
        std::lock_guard<std::mutex> lock(memory_mtx_);
        memory_sudos_[user_id] = entry;
    }

#if ANONX_HAS_MONGOCXX
    if (pimpl_->connected && pimpl_->pool) {
        try {
            auto client = pimpl_->pool->acquire();
            auto coll = (*client)[pimpl_->db_name]["sudos"];
            bsoncxx::builder::stream::document filter;
            filter << "user_id" << user_id;

            std::string json_str = entry.to_json().dump();
            auto bson_doc = bsoncxx::from_json(json_str);

            mongocxx::options::replace opts;
            opts.upsert(true);
            coll.replace_one(filter.view(), bson_doc.view(), opts);
            return true;
        } catch (const std::exception& ex) {
            ANONX_LOG_ERROR("MongoDB", "Error adding sudo: ", ex.what());
        }
    }
#endif
    return true;
}

bool MongoClient::remove_sudo(int64_t user_id) {
    {
        std::lock_guard<std::mutex> lock(memory_mtx_);
        memory_sudos_.erase(user_id);
    }

#if ANONX_HAS_MONGOCXX
    if (pimpl_->connected && pimpl_->pool) {
        try {
            auto client = pimpl_->pool->acquire();
            auto coll = (*client)[pimpl_->db_name]["sudos"];
            bsoncxx::builder::stream::document filter;
            filter << "user_id" << user_id;
            coll.delete_one(filter.view());
            return true;
        } catch (const std::exception& ex) {
            ANONX_LOG_ERROR("MongoDB", "Error removing sudo: ", ex.what());
        }
    }
#endif
    return true;
}

bool MongoClient::is_sudo(int64_t user_id) {
    std::lock_guard<std::mutex> lock(memory_mtx_);
    return memory_sudos_.find(user_id) != memory_sudos_.end();
}

std::vector<SudoEntry> MongoClient::get_all_sudos() {
    std::lock_guard<std::mutex> lock(memory_mtx_);
    std::vector<SudoEntry> result;
    result.reserve(memory_sudos_.size());
    for (const auto& [_, entry] : memory_sudos_) {
        result.push_back(entry);
    }
    return result;
}

bool MongoClient::set_chat_blocked(int64_t chat_id, bool blocked) {
    std::lock_guard<std::mutex> lock(memory_mtx_);
    memory_blocked_chats_[chat_id] = blocked;
    return true;
}

bool MongoClient::is_chat_blocked(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(memory_mtx_);
    auto it = memory_blocked_chats_.find(chat_id);
    return (it != memory_blocked_chats_.end()) && it->second;
}

} // namespace anonx::database
