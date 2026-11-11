/**
 * @file dma_allocator.hpp
 * @brief RAII wrapper for Linux DMA Heap (CMA) contiguous memory allocation.
 * 
 * Target device: /dev/rk_dma_heap/rk-dma-heap-cma
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace edge_camera {

class DmaBuffer {
public:
    DmaBuffer() = default;
    ~DmaBuffer();

    // Disable copy
    DmaBuffer(const DmaBuffer&) = delete;
    DmaBuffer& operator=(const DmaBuffer&) = delete;

    // Enable move
    DmaBuffer(DmaBuffer&& other) noexcept;
    DmaBuffer& operator=(DmaBuffer&& other) noexcept;

    /**
     * @brief Allocate physically contiguous memory from Rockchip CMA heap.
     * @param size Required allocation size in bytes.
     * @param heap_path Path to dma_heap device node (defaults to RV1106 CMA path).
     * @return true on allocation and mmap success.
     */
    bool allocate(size_t size, const std::string& heap_path = "/dev/rk_dma_heap/rk-dma-heap-cma");

    /**
     * @brief Free mapped memory and close file descriptor.
     */
    void release();

    [[nodiscard]] void* data() const noexcept { return va_; }
    [[nodiscard]] int fd() const noexcept { return fd_; }
    [[nodiscard]] size_t size() const noexcept { return size_; }
    [[nodiscard]] bool is_valid() const noexcept { return fd_ >= 0 && va_ != nullptr; }

private:
    int fd_{-1};
    void* va_{nullptr};
    size_t size_{0};
};

} // namespace edge_camera
