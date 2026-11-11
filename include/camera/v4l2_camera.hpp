/**
 * @file v4l2_camera.hpp
 * @brief Zero-copy V4L2 Multiplanar video capture abstraction for Rockchip ISP.
 * 
 * Complies with RAII guidelines, managing mmap buffers and file descriptors cleanly.
 */

#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <linux/videodev2.h>

namespace edge_camera {

/**
 * @brief Memory-mapped buffer descriptor managed by V4L2 kernel driver.
 */
struct FrameBuffer {
    void* start{nullptr};       ///< User-space virtual address mapped via mmap()
    size_t length{0};          ///< Total buffer size in bytes
    int dma_fd{-1};            ///< Exported DMA-BUF file descriptor (crucial for Phase 3 Zero-copy RGA)
    uint32_t index{0};         ///< Ring-buffer index inside the V4L2 driver queue
};

class V4L2Camera {
public:
    explicit V4L2Camera(std::string device_path);
    ~V4L2Camera();

    // Disable copy semantics to prevent double-free of file descriptors / mmap buffers
    V4L2Camera(const V4L2Camera&) = delete;
    V4L2Camera& operator=(const V4L2Camera&) = delete;

    // Enable move semantics
    V4L2Camera(V4L2Camera&& other) noexcept;
    V4L2Camera& operator=(V4L2Camera&& other) noexcept;

    /**
     * @brief Configure sensor resolution and pixel format (e.g. V4L2_PIX_FMT_NV12)
     */
    bool init(uint32_t width, uint32_t height, uint32_t pixel_format = V4L2_PIX_FMT_NV12, uint32_t buffer_count = 3);

    /**
     * @brief Start streaming via VIDIOC_STREAMON
     */
    bool start_stream();

    /**
     * @brief Dequeue a captured frame from driver.
     * @param[out] buffer Reference to mapped buffer information.
     * @param timeout_ms Max waiting time in milliseconds before returning false.
     */
    bool dequeue_frame(FrameBuffer& buffer, int timeout_ms = 2000);

    /**
     * @brief Requeue the processed buffer back to the kernel driver.
     */
    bool enqueue_frame(const FrameBuffer& buffer);

    /**
     * @brief Stop streaming via VIDIOC_STREAMOFF
     */
    bool stop_stream();

    [[nodiscard]] uint32_t width() const noexcept { return width_; }
    [[nodiscard]] uint32_t height() const noexcept { return height_; }
    [[nodiscard]] uint32_t pixel_format() const noexcept { return pixel_format_; }
    [[nodiscard]] bool is_streaming() const noexcept { return is_streaming_; }

private:
    bool query_capabilities();
    bool setup_format();
    bool request_and_mmap_buffers(uint32_t buffer_count);
    void unmap_and_release_buffers();

    std::string device_path_;
    int fd_{-1};
    uint32_t width_{1920};
    uint32_t height_{1080};
    uint32_t pixel_format_{V4L2_PIX_FMT_NV12};
    bool is_streaming_{false};

    std::vector<FrameBuffer> buffers_;
};

} // namespace edge_camera
