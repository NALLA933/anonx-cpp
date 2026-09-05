#include <anonx/core/rate_limiter.hpp>
#include <algorithm>

namespace anonx::core {

RateLimiter::RateLimiter(double tokens_per_sec, double burst_limit, size_t)
    : tokens_per_sec_(tokens_per_sec),
      burst_limit_(burst_limit) {}

bool RateLimiter::allow(int64_t entity_id, double tokens_requested) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto now = std::chrono::steady_clock::now();

    auto it = buckets_.find(entity_id);
    if (it == buckets_.end()) {
        if (tokens_requested <= burst_limit_) {
            buckets_[entity_id] = Bucket{
                .tokens = burst_limit_ - tokens_requested,
                .last_update = now
            };
            return true;
        }
        return false;
    }

    Bucket& b = it->second;
    double elapsed_sec = std::chrono::duration<double>(now - b.last_update).count();
    b.last_update = now;

    // Refill tokens according to elapsed time
    b.tokens = std::min(burst_limit_, b.tokens + (elapsed_sec * tokens_per_sec_));

    if (b.tokens >= tokens_requested) {
        b.tokens -= tokens_requested;
        return true;
    }

    return false;
}

} // namespace anonx::core
