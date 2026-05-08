#include "ave/core/JobSystem.h"

#include <algorithm>

namespace ave::core {

JobSystem::~JobSystem()
{
    Stop();
}

void JobSystem::Start(size_t thread_count)
{
    Stop();
    stopping_ = false;
    thread_count = std::max<size_t>(1, thread_count == 0 ? std::thread::hardware_concurrency() : thread_count);

    for (size_t index = 0; index < thread_count; ++index) {
        workers_.emplace_back([this]() { WorkerLoop(); });
    }
}

void JobSystem::Stop()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }

    cv_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    workers_.clear();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!jobs_.empty()) {
            jobs_.pop();
        }
        pending_jobs_ = 0;
    }
}

void JobSystem::WaitIdle()
{
    std::unique_lock<std::mutex> lock(mutex_);
    idle_cv_.wait(lock, [this]() { return pending_jobs_ == 0; });
}

void JobSystem::ParallelFor(size_t item_count, size_t batch_size, std::function<void(size_t, size_t)> const& fn)
{
    if (item_count == 0) {
        return;
    }

    batch_size = std::max<size_t>(1, batch_size);
    std::vector<std::future<void>> futures;

    for (size_t begin = 0; begin < item_count; begin += batch_size) {
        size_t const end = std::min(item_count, begin + batch_size);
        futures.push_back(Enqueue([begin, end, &fn]() { fn(begin, end); }));
    }

    for (auto& future : futures) {
        future.get();
    }
}

size_t JobSystem::ThreadCount() const noexcept
{
    return workers_.size();
}

void JobSystem::WorkerLoop()
{
    while (true) {
        std::function<void()> job;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() { return stopping_ || !jobs_.empty(); });

            if (stopping_ && jobs_.empty()) {
                return;
            }

            job = std::move(jobs_.front());
            jobs_.pop();
        }

        job();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            --pending_jobs_;
            if (pending_jobs_ == 0) {
                idle_cv_.notify_all();
            }
        }
    }
}

} // namespace ave::core
