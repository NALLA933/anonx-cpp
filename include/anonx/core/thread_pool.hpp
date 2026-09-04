#pragma once

#include <anonx/core/safe_queue.hpp>
#include <concepts>
#include <functional>
#include <future>
#include <memory>
#include <thread>
#include <type_traits>
#include <vector>

namespace anonx::core {

class ThreadPool {
public:
    explicit ThreadPool(size_t thread_count = std::max(2u, std::thread::hardware_concurrency()));
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    template <typename F, typename... Args>
        requires std::invocable<F, Args...>
    auto enqueue(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
        using return_type = std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            [func = std::forward<F>(f), ... captured_args = std::forward<Args>(args)]() mutable {
                return std::invoke(func, std::move(captured_args)...);
            }
        );

        std::future<return_type> res = task->get_future();
        task_queue_.push([task]() { (*task)(); });
        return res;
    }

    void shutdown();
    [[nodiscard]] size_t thread_count() const noexcept;
    [[nodiscard]] size_t pending_tasks() const noexcept;

private:
    void worker_loop();

    std::vector<std::thread> workers_;
    SafeQueue<std::function<void()>> task_queue_;
    std::atomic<bool> stop_{false};
};

} // namespace anonx::core
