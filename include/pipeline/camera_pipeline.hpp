/**
 * @file camera_pipeline.hpp
 * @brief Multi-threaded production camera pipeline coordinator.
 */

#pragma once

#include "camera/v4l2_camera.hpp"
#include "rga/rga_converter.hpp"
#include "utils/dma_allocator.hpp"
#include "npu/rknn_engine.hpp"
#include "pipeline/bounded_queue.hpp"

#include <string>
#include <thread>
#include <atomic>
#include <memory>

namespace edge_camera {

struct CameraConfig {
    std::string video_device{"/dev/video11"};
    std::string model_path{"/tmp/face_detection_rv1106.rknn"};
    uint32_t main_width{1920};
    uint32_t main_height{1080};
    uint32_t buffer_count{4};
};

class CameraPipeline {
public:
    explicit CameraPipeline(CameraConfig config);
    ~CameraPipeline();

    // Disable copy/move
    CameraPipeline(const CameraPipeline&) = delete;
    CameraPipeline& operator=(const CameraPipeline&) = delete;

    /**
     * @brief Initialize all hardware blocks (V4L2, RGA, DMA, RKNN NPU).
     */
    bool init();

    /**
     * @brief Start multi-threaded capture and AI workers.
     */
    bool start();

    /**
     * @brief Stop pipeline gracefully and clean up hardware resources.
     */
    void stop();

    void wait_until_stopped();

private:
    void capture_worker_loop();
    void ai_worker_loop();

    CameraConfig config_;
    std::atomic<bool> is_running_{false};

    V4L2Camera camera_;
    RgaConverter rga_;
    RknnEngine npu_;
    DmaBuffer sub_dma_buffer_;

    BoundedQueue<uint64_t> ai_trigger_queue_{1}; // 1-element frame-dropping queue

    std::thread capture_thread_;
    std::thread ai_thread_;
};

} // namespace edge_camera
