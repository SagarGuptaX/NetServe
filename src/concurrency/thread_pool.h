#pragma once

#include <functional>

namespace concurrency {

class ThreadPool {
public:
    explicit ThreadPool(std::size_t thread_count);

    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    void enqueue_task(std::function<void()> task);

private:
    std::size_t thread_count_;
};

} // namespace concurrency
