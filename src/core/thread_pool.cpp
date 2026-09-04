#include <anonx/core/thread_pool.hpp>

namespace anonx::core {

ThreadPool::ThreadPool(size_t thread_count) {
    workers_.reserve(thread_count);
    for (size_t i = 0; i < thread_count; ++i) {
        workers_.emplace_back(&ThreadPool::worker_loop, this);
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::shutdown() {
    if (stop_.exchange(true)) {
        return;
    }

    task_queue_.shutdown();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
}

void ThreadPool::worker_loop() {
    while (!stop_) {
        auto task = task_queue_.wait_and_pop();
        if (task.has_value()) {
            try {
                (*task)();
            } catch (...) {
                // Prevent task exceptions from killing the worker thread
            }
        } else if (task_queue_.is_shutdown()) {
            break;
        }
    }
}

size_t ThreadPool::thread_count() const noexcept {
    return workers_.size();
}

size_t ThreadPool::pending_tasks() const noexcept {
    return task_queue_.size();
}

} // namespace anonx::core
