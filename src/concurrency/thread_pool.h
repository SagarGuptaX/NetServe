#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace concurrency {

class ThreadPool {
public:
    explicit ThreadPool(std::size_t thread_count);
    ~ThreadPool();

    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Enqueue a task for execution by one of the worker threads.
    void enqueue_task(std::function<void()> task);

private:
    std::vector<std::thread>          workers_;
    std::queue<std::function<void()>> task_queue_;
    std::mutex                        queue_mutex_;
    std::condition_variable           cv_;
    bool                              stop_{false};

    void worker_loop();
};

} // namespace concurrency
