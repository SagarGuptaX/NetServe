#include "concurrency/thread_pool.h"

namespace concurrency {

ThreadPool::ThreadPool(std::size_t thread_count) {
    workers_.reserve(thread_count);
    for (std::size_t i = 0; i < thread_count; ++i)
        workers_.emplace_back(&ThreadPool::worker_loop, this);
}

ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        stop_ = true;
    }
    cv_.notify_all(); // wake every worker so they can exit
    for (auto& t : workers_)
        t.join();
}

void ThreadPool::enqueue_task(std::function<void()> task) {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        task_queue_.push(std::move(task));
    }
    cv_.notify_one();
}

void ThreadPool::worker_loop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            // Block until there is work to do or we are shutting down.
            cv_.wait(lock, [this] { return stop_ || !task_queue_.empty(); });
            if (stop_ && task_queue_.empty()) return;
            task = std::move(task_queue_.front());
            task_queue_.pop();
        }
        task(); // execute outside the lock
    }
}

} // namespace concurrency
