#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace ave::core {

class JobSystem {
public:
    JobSystem() = default;
    ~JobSystem();

    JobSystem(JobSystem const&) = delete;
    JobSystem& operator=(JobSystem const&) = delete;

    void Start(size_t thread_count = std::thread::hardware_concurrency());
    void Stop();
    void WaitIdle();

    template <typename Fn>
    auto Enqueue(Fn&& fn) -> std::future<decltype(fn())>
    {
        using Result = decltype(fn());
        auto task = std::make_shared<std::packaged_task<Result()>>(std::forward<Fn>(fn));
        auto future = task->get_future();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            jobs_.push([task]() { (*task)(); });
            ++pending_jobs_;
        }

        cv_.notify_one();
        return future;
    }

    void ParallelFor(size_t item_count, size_t batch_size, std::function<void(size_t, size_t)> const& fn);
    size_t ThreadCount() const noexcept;

private:
    void WorkerLoop();

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> jobs_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable idle_cv_;
    std::atomic_bool stopping_{false};
    size_t pending_jobs_ = 0;
};

} // namespace ave::core
