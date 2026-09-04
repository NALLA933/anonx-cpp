#ifndef SENPAI_QUEUE_HPP
#define SENPAI_QUEUE_HPP

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "senpai/utils/youtube.hpp"

namespace senpai {

using MediaItem = Track;

class Queue {
public:
    Queue() = default;

    Queue(const Queue&)            = delete;
    Queue& operator=(const Queue&) = delete;
    Queue(Queue&&)                 = delete;
    Queue& operator=(Queue&&)      = delete;

    int add(std::int64_t chatId, const MediaItem& item) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto& dq = queues_[chatId];
        dq.push_back(item);
        return static_cast<int>(dq.size()) - 1;
    }

    std::pair<int, std::optional<MediaItem>>
    checkItem(std::int64_t chatId, const std::string& itemId) const {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = queues_.find(chatId);
        if (it != queues_.end()) {
            const auto& dq = it->second;
            for (std::size_t i = 0; i < dq.size(); ++i) {
                if (dq[i].id == itemId)
                    return {static_cast<int>(i), dq[i]};
            }
        }
        return {-1, std::nullopt};
    }

    void forceAdd(std::int64_t chatId, const MediaItem& item, int removeAt = 0) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto& dq = queues_[chatId];
        if (!dq.empty())
            dq.pop_front();
        dq.push_front(item);
        if (removeAt > 0 &&
            static_cast<std::size_t>(removeAt) < dq.size()) {
            dq.erase(dq.begin() + removeAt);
        }
    }

    std::optional<MediaItem> getCurrent(std::int64_t chatId) const {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = queues_.find(chatId);
        if (it == queues_.end() || it->second.empty())
            return std::nullopt;
        return it->second.front();
    }

    std::optional<MediaItem> getNext(std::int64_t chatId, bool check = false) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = queues_.find(chatId);
        if (it == queues_.end() || it->second.empty())
            return std::nullopt;
        auto& dq = it->second;
        if (check)
            return dq.size() > 1 ? std::optional<MediaItem>(dq[1]) : std::nullopt;
        dq.pop_front();
        if (dq.empty())
            return std::nullopt;
        return dq.front();
    }

    std::vector<MediaItem> getQueue(std::int64_t chatId) const {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = queues_.find(chatId);
        if (it == queues_.end())
            return {};
        return std::vector<MediaItem>(it->second.begin(), it->second.end());
    }

    void removeCurrent(std::int64_t chatId) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = queues_.find(chatId);
        if (it != queues_.end() && !it->second.empty())
            it->second.pop_front();
    }

    void clear(std::int64_t chatId) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = queues_.find(chatId);
        if (it != queues_.end())
            it->second.clear();
    }

    bool replaceCurrent(std::int64_t chatId, const MediaItem& item) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = queues_.find(chatId);
        if (it == queues_.end() || it->second.empty())
            return false;
        it->second.front() = item;
        return true;
    }

    std::size_t size(std::int64_t chatId) const {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = queues_.find(chatId);
        return it == queues_.end() ? 0 : it->second.size();
    }

    bool empty(std::int64_t chatId) const {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = queues_.find(chatId);
        return it == queues_.end() || it->second.empty();
    }

private:
    mutable std::mutex mtx_;
    std::unordered_map<std::int64_t, std::deque<MediaItem>> queues_;
};

}

#endif
