/**
 * @file test_dma_allocator.cpp
 * @brief Unit & Integration tests for DMA Buffer Allocator.
 */

#include "utils/dma_allocator.hpp"
#include "test_framework.hpp"

#include <cstring>

TEST_CASE(dma_allocate_and_free) {
    edge_camera::DmaBuffer dma;
    ASSERT_EQ(dma.fd(), -1);
    ASSERT_EQ(dma.data(), nullptr);
    ASSERT_EQ(dma.size(), 0);

    constexpr size_t BUFFER_SIZE = 640 * 360 * 3 / 2; // 345,600 bytes
    bool ok = dma.allocate(BUFFER_SIZE);
    ASSERT_TRUE(ok);

    ASSERT_GE(dma.fd(), 0);
    ASSERT_NE(dma.data(), nullptr);
    ASSERT_EQ(dma.size(), BUFFER_SIZE);

    dma.release();
    ASSERT_EQ(dma.fd(), -1);
    ASSERT_EQ(dma.data(), nullptr);
    ASSERT_EQ(dma.size(), static_cast<size_t>(0));
}

TEST_CASE(dma_mmap_read_write) {
    edge_camera::DmaBuffer dma;
    constexpr size_t TEST_SIZE = 4096;
    ASSERT_TRUE(dma.allocate(TEST_SIZE));

    uint8_t* ptr = static_cast<uint8_t*>(dma.data());
    ASSERT_NE(ptr, nullptr);

    // Write pattern
    for (size_t i = 0; i < TEST_SIZE; ++i) {
        ptr[i] = static_cast<uint8_t>(i & 0xFF);
    }

    // Verify pattern
    for (size_t i = 0; i < TEST_SIZE; ++i) {
        ASSERT_EQ(ptr[i], static_cast<uint8_t>(i & 0xFF));
    }
}

TEST_CASE(dma_move_semantics) {
    edge_camera::DmaBuffer dma1;
    ASSERT_TRUE(dma1.allocate(16384));

    int orig_fd = dma1.fd();
    void* orig_data = dma1.data();
    size_t orig_size = dma1.size();

    // Move construct
    edge_camera::DmaBuffer dma2(std::move(dma1));
    ASSERT_EQ(dma2.fd(), orig_fd);
    ASSERT_EQ(dma2.data(), orig_data);
    ASSERT_EQ(dma2.size(), orig_size);

    ASSERT_EQ(dma1.fd(), -1);
    ASSERT_EQ(dma1.data(), nullptr);
    ASSERT_EQ(dma1.size(), 0);

    // Move assign
    edge_camera::DmaBuffer dma3;
    dma3 = std::move(dma2);
    ASSERT_EQ(dma3.fd(), orig_fd);
    ASSERT_EQ(dma3.data(), orig_data);
    ASSERT_EQ(dma3.size(), orig_size);

    ASSERT_EQ(dma2.fd(), -1);
}

int main() {
    return test_runner::run_all_tests("DMA Allocator Tests");
}
