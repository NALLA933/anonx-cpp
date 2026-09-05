#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <vector>

namespace anonx::core {

class ThreadPool {
public:
    explicit ThreadPool(size_t threads = std::max<size_t>(4, std::thread::hardware_concurrency()),
                        size_t max_queue_size = 100000);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    template <class F, class... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<typename std::invoke_result<F, Args...>::type> {
        using return_type = typename std::invoke_result<F, Args...>::type;

        if (stop_.load(std::memory_order_relaxed)) {
            throw std::runtime_error("enqueue on stopped ThreadPool");
        }

        // Bounded capacity check for backpressure protection
        if (pending_tasks_.load(std::memory_order_relaxed) >= max_queue_size_) {
            throw std::runtime_error("ThreadPool queue capacity saturated (backpressure applied)");
        }

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            [func = std::forward<F>(f), ...capture_args = std::forward<Args>(args)]() mutable {
                return std::invoke(std::move(func), std::move(capture_args)...);
            }
        );

        std::future<return_type> res = task->get_future();

        pending_tasks_.fetch_add(1, std::memory_order_relaxed);

        {
            std::unique_lock<std::mutex> lock(mutex_);
            tasks_.emplace([this, task]() {
                try {
                    (*task)();
                } catch (...) {
                    // Handled by std::packaged_task future
                }
                completed_tasks_.fetch_add(1, std::memory_order_relaxed);
                pending_tasks_.fetch_sub(1, std::memory_order_relaxed);
            });
        }
        cv_.notify_one();
        return res;
    }

    [[nodiscard]] size_t worker_count() const noexcept { return workers_.size(); }
    [[nodiscard]] size_t pending_tasks() const noexcept {
        return pending_tasks_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] uint64_t completed_tasks() const noexcept {
        return completed_tasks_.load(std::memory_order_relaxed);
    }

    void shutdown();

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stop_{false};
    std::atomic<size_t> pending_tasks_{0};
    std::atomic<uint64_t> completed_tasks_{0};
    size_t max_queue_size_;
};

} // namespace anonx::core
