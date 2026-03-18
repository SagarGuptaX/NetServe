#include "concurrency/thread_pool.h"

#include <stdexcept>

namespace concurrency {

ThreadPool::ThreadPool(std::size_t thread_count) : thread_count_(thread_count) {}

ThreadPool::~ThreadPool() = default;

void ThreadPool::enqueue_task(std::function<void()> task) {
    (void)task;
    throw std::logic_error("Not implemented");
}

} // namespace concurrency
