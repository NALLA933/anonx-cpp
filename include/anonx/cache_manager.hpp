#pragma once

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace anonx {

class CacheManager {
public:
    CacheManager() = default;

    CacheManager(const CacheManager&)            = delete;
    CacheManager& operator=(const CacheManager&) = delete;
    CacheManager(CacheManager&&)                 = delete;
    CacheManager& operator=(CacheManager&&)      = delete;

    bool isActiveCall(std::int64_t chatId) const {
        std::lock_guard<std::mutex> lk(mtx_);
        return activeCalls_.find(chatId) != activeCalls_.end();
    }

    void addCall(std::int64_t chatId) {
        std::lock_guard<std::mutex> lk(mtx_);
        activeCalls_[chatId] = 1;
    }

    void removeCall(std::int64_t chatId) {
        std::lock_guard<std::mutex> lk(mtx_);
        activeCalls_.erase(chatId);
    }

    bool setPaused(std::int64_t chatId, bool paused) {
        std::lock_guard<std::mutex> lk(mtx_);
        activeCalls_[chatId] = paused ? 0 : 1;
        return !paused;
    }

    bool isPlaying(std::int64_t chatId) const {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = activeCalls_.find(chatId);
        return it != activeCalls_.end() && it->second != 0;
    }

    int getLoop(std::int64_t chatId) const {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = loop_.find(chatId);
        return it == loop_.end() ? 0 : it->second;
    }

    void setLoop(std::int64_t chatId, int count) {
        std::lock_guard<std::mutex> lk(mtx_);
        if (count <= 0)
            loop_.erase(chatId);
        else
            loop_[chatId] = count;
    }

    void clearChat(std::int64_t chatId) {
        std::lock_guard<std::mutex> lk(mtx_);
        activeCalls_.erase(chatId);
        loop_.erase(chatId);
    }

    std::size_t activeCallCount() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return activeCalls_.size();
    }

    std::vector<std::int64_t> activeChats() const {
        std::lock_guard<std::mutex> lk(mtx_);
        std::vector<std::int64_t> out;
        out.reserve(activeCalls_.size());
        for (const auto& kv : activeCalls_)
            out.push_back(kv.first);
        std::sort(out.begin(), out.end());
        return out;
    }

private:
    mutable std::mutex mtx_;
    std::unordered_map<std::int64_t, int> activeCalls_;
    std::unordered_map<std::int64_t, int> loop_;
};

}
