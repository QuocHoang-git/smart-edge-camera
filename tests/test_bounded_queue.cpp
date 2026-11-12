/**
 * @file test_bounded_queue.cpp
 * @brief Unit tests for thread-safe BoundedQueue.
 */

#include "pipeline/bounded_queue.hpp"
#include "test_framework.hpp"

#include <thread>
#include <vector>
#include <chrono>

TEST_CASE(queue_push_and_pop_fifo) {
    edge_camera::BoundedQueue<int> q(5);
    ASSERT_EQ(q.size(), static_cast<size_t>(0));

    q.push(10);
    q.push(20);
    q.push(30);

    ASSERT_EQ(q.size(), static_cast<size_t>(3));

    int val = 0;
    ASSERT_TRUE(q.pop(val));
    ASSERT_EQ(val, 10);

    ASSERT_TRUE(q.pop(val));
    ASSERT_EQ(val, 20);

    ASSERT_TRUE(q.pop(val));
    ASSERT_EQ(val, 30);

    ASSERT_EQ(q.size(), static_cast<size_t>(0));
}

TEST_CASE(queue_bounded_drop_oldest) {
    edge_camera::BoundedQueue<int> q(3);

    // Push 5 items into capacity 3 queue
    q.push(1);
    q.push(2);
    q.push(3);
    q.push(4);
    q.push(5);

    ASSERT_EQ(q.size(), static_cast<size_t>(3));
    ASSERT_EQ(q.drop_count(), static_cast<uint64_t>(2));

    int val = 0;
    ASSERT_TRUE(q.pop(val));
    ASSERT_EQ(val, 3); // 1 and 2 were dropped

    ASSERT_TRUE(q.pop(val));
    ASSERT_EQ(val, 4);

    ASSERT_TRUE(q.pop(val));
    ASSERT_EQ(val, 5);

    ASSERT_EQ(q.size(), static_cast<size_t>(0));
}

TEST_CASE(queue_pop_timeout) {
    edge_camera::BoundedQueue<int> q(2);
    int val = 0;

    auto t0 = std::chrono::steady_clock::now();
    bool popped = q.pop(val, 50); // 50ms timeout
    auto t1 = std::chrono::steady_clock::now();

    ASSERT_FALSE(popped);
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    ASSERT_GE(elapsed_ms, 40);
}

TEST_CASE(queue_concurrent_producer_consumer) {
    constexpr int NUM_ITEMS = 200;
    edge_camera::BoundedQueue<int> q(500); // Sufficient capacity to avoid dropping in unit test

    std::thread producer([&]() {
        for (int i = 0; i < NUM_ITEMS; ++i) {
            q.push(i);
        }
    });

    std::vector<int> received;
    received.reserve(NUM_ITEMS);

    std::thread consumer([&]() {
        int val = 0;
        while (static_cast<int>(received.size()) < NUM_ITEMS) {
            if (q.pop(val, 100)) {
                received.push_back(val);
            }
        }
    });

    producer.join();
    consumer.join();

    ASSERT_EQ(static_cast<int>(received.size()), NUM_ITEMS);
    for (int i = 0; i < NUM_ITEMS; ++i) {
        ASSERT_EQ(received[i], i);
    }
}

int main() {
    return test_runner::run_all_tests("BoundedQueue Unit Tests");
}
