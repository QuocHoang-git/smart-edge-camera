/**
 * @file bounded_queue.hpp
 * @brief Thread-safe bounded queue with drop-oldest policy for low-latency video pipelines.
 */

#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <cstddef>

namespace edge_camera {

template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(size_t max_capacity = 2)
        : max_capacity_(max_capacity == 0 ? 1 : max_capacity) {}

    ~BoundedQueue() {
        stop();
    }

    // Disable copy
    BoundedQueue(const BoundedQueue&) = delete;
    BoundedQueue& operator=(const BoundedQueue&) = delete;

    /**
     * @brief Push item into queue. If full, drops oldest item (Lowest Latency policy).
     * @param item Rvalue reference to element to move into queue.
     */
    void push(T item) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopped_) return;

            if (queue_.size() >= max_capacity_) {
                queue_.pop(); // Drop oldest frame to avoid queue buildup
                drop_count_++;
            }
            queue_.push(std::move(item));
        }
        cv_not_empty_.notify_one();
    }

    /**
     * @brief Pop item with timeout.
     * @param[out] item Retrieved element.
     * @param timeout_ms Maximum wait time in milliseconds.
     * @return true if an item was successfully popped, false on timeout or shutdown.
     */
    bool pop(T& item, int timeout_ms = 100) {
        std::unique_lock<std::mutex> lock(mutex_);
        bool acquired = cv_not_empty_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] {
            return !queue_.empty() || stopped_;
        });

        if (!acquired || stopped_ || queue_.empty()) {
            return false;
        }

        item = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopped_ = true;
        }
        cv_not_empty_.notify_all();
    }

    [[nodiscard]] size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    [[nodiscard]] uint64_t drop_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return drop_count_;
    }

private:
    const size_t max_capacity_;
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_not_empty_;
    bool stopped_{false};
    uint64_t drop_count_{0};
};

} // namespace edge_camera
