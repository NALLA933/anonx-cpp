#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace anonx::core {

class RateLimiter {
public:
    explicit RateLimiter(double tokens_per_sec = 2.0, double burst_limit = 5.0, size_t stripe_count = 0);
    ~RateLimiter() = default;

    bool allow(int64_t entity_id, double tokens_requested = 1.0);

private:
    struct Bucket {
        double tokens{0.0};
        std::chrono::steady_clock::time_point last_update;
    };

    double tokens_per_sec_;
    double burst_limit_;
    mutable std::mutex mtx_;
    std::unordered_map<int64_t, Bucket> buckets_;
};

} // namespace anonx::core
