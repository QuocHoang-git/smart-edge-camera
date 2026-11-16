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

#include "pipeline/pipeline_types.hpp"

namespace edge_camera {

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
    bool dequeue_frame(VideoFrame& buffer, int timeout_ms = 2000);

    /**
     * @brief Requeue the processed buffer back to the kernel driver.
     */
    bool enqueue_frame(const VideoFrame& buffer);

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
    
    struct v4l2_format fmt_;
    std::string device_path_;
    int fd_{-1};
    uint32_t width_{1920};
    uint32_t height_{1080};
    uint32_t pixel_format_{V4L2_PIX_FMT_NV12};
    bool is_streaming_{false};

    std::vector<VideoFrame> buffers_;
};

} // namespace edge_camera
