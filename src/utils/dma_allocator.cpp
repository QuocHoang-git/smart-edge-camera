/**
 * @file dma_allocator.cpp
 * @brief Implementation of CMA DMA heap allocation.
 */

#include "utils/dma_allocator.hpp"

#include <iostream>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

namespace edge_camera {

struct dma_heap_allocation_data {
    uint64_t len;
    uint32_t fd;
    uint32_t fd_flags;
    uint64_t heap_flags;
};

#define DMA_HEAP_IOC_MAGIC   'H'
#define DMA_HEAP_IOCTL_ALLOC _IOWR(DMA_HEAP_IOC_MAGIC, 0x0, struct dma_heap_allocation_data)

DmaBuffer::~DmaBuffer() {
    release();
}

DmaBuffer::DmaBuffer(DmaBuffer&& other) noexcept
    : fd_(other.fd_), va_(other.va_), size_(other.size_) {
    other.fd_ = -1;
    other.va_ = nullptr;
    other.size_ = 0;
}

DmaBuffer& DmaBuffer::operator=(DmaBuffer&& other) noexcept {
    if (this != &other) {
        release();
        fd_ = other.fd_;
        va_ = other.va_;
        size_ = other.size_;

        other.fd_ = -1;
        other.va_ = nullptr;
        other.size_ = 0;
    }
    return *this;
}

bool DmaBuffer::allocate(size_t size, const std::string& heap_path) {
    release();

    int heap_fd = open(heap_path.c_str(), O_RDWR | O_CLOEXEC);
    if (heap_fd < 0) {
        std::cerr << "[DMA ERROR] Failed to open " << heap_path << ": " << std::strerror(errno) << "\n";
        return false;
    }

    struct dma_heap_allocation_data alloc_data;
    std::memset(&alloc_data, 0, sizeof(alloc_data));
    alloc_data.len = size;
    alloc_data.fd_flags = O_CLOEXEC | O_RDWR;

    if (ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &alloc_data) < 0) {
        std::cerr << "[DMA ERROR] DMA_HEAP_IOCTL_ALLOC failed: " << std::strerror(errno) << "\n";
        close(heap_fd);
        return false;
    }

    close(heap_fd); // Heap dev node can be closed immediately after allocation

    void* mmap_ptr = mmap(nullptr, alloc_data.len, PROT_READ | PROT_WRITE, MAP_SHARED, static_cast<int>(alloc_data.fd), 0);
    if (mmap_ptr == MAP_FAILED) {
        std::cerr << "[DMA ERROR] mmap on DMA-BUF failed: " << std::strerror(errno) << "\n";
        close(static_cast<int>(alloc_data.fd));
        return false;
    }

    fd_ = static_cast<int>(alloc_data.fd);
    va_ = mmap_ptr;
    size_ = size;

    std::cout << "[DMA INFO] Allocated CMA buffer: " << size << " bytes, fd=" << fd_ << ", va=" << va_ << "\n";
    return true;
}

void DmaBuffer::release() {
    if (va_ && va_ != MAP_FAILED) {
        munmap(va_, size_);
        va_ = nullptr;
    }
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
    size_ = 0;
}

} // namespace edge_camera
