#include <anonx/core/thread_pool.hpp>

namespace anonx::core {

ThreadPool::ThreadPool(size_t threads, size_t max_queue_size)
    : max_queue_size_(max_queue_size) {
    workers_.reserve(threads);
    for (size_t i = 0; i < threads; ++i) {
        workers_.emplace_back([this]() {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(this->mutex_);
                    this->cv_.wait(lock, [this]() {
                        return this->stop_.load(std::memory_order_relaxed) || !this->tasks_.empty();
                    });

                    if (this->stop_.load(std::memory_order_relaxed) && this->tasks_.empty()) {
                        return;
                    }

                    task = std::move(this->tasks_.front());
                    this->tasks_.pop();
                }

                if (task) {
                    try {
                        task();
                    } catch (...) {}
                }
            }
        });
    }
}

void ThreadPool::shutdown() {
    bool expected = false;
    if (!stop_.compare_exchange_strong(expected, true)) {
        return;
    }

    cv_.notify_all();

    for (std::thread& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
}

ThreadPool::~ThreadPool() {
    shutdown();
}

} // namespace anonx::core
