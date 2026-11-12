/**
 * @file camera_pipeline.hpp
 * @brief Multi-threaded production camera pipeline coordinator with Dual-Stream H.264 & RTSP.
 */

#pragma once

#include "camera/v4l2_camera.hpp"
#include "rga/rga_converter.hpp"
#include "utils/dma_allocator.hpp"
#include "npu/rknn_engine.hpp"
#include "encoder/mpp_encoder.hpp"
#include "rtsp/rtsp_server.hpp"
#include "pipeline/bounded_queue.hpp"

#include <string>
#include <thread>
#include <atomic>
#include <memory>
#include <vector>

namespace edge_camera {

struct CameraConfig {
    std::string video_device{"/dev/video11"};
    std::string model_path{"/userdata/retinaface.rknn"};
    uint32_t main_width{1920};
    uint32_t main_height{1080};
    uint32_t sub_width{640};
    uint32_t sub_height{360};
    uint32_t fps{30};
    uint32_t buffer_count{4};
    uint16_t rtsp_port{554};
    uint32_t main_bitrate{2500000}; // 2.5 Mbps
    uint32_t sub_bitrate{800000};   // 800 Kbps
};

class CameraPipeline {
public:
    explicit CameraPipeline(CameraConfig config);
    ~CameraPipeline();

    // Disable copy/move
    CameraPipeline(const CameraPipeline&) = delete;
    CameraPipeline& operator=(const CameraPipeline&) = delete;

    /**
     * @brief Initialize all hardware blocks (V4L2, RGA, CMA DMA, RKNN NPU, MPP encoders, RTSP server).
     */
    bool init();

    /**
     * @brief Start multi-threaded capture, AI inference, dual-stream encoders, and RTSP streamer.
     */
    bool start();

    /**
     * @brief Stop pipeline gracefully and clean up all hardware resources.
     */
    void stop();

    void wait_until_stopped();

private:
    void capture_and_stream_loop();
    void ai_worker_loop();
    void detect_faces(std::vector<DetectionBox>& boxes);

    struct Anchor {
        float x_center;
        float y_center;
        float w;
        float h;
    };
    void init_anchors();
    std::vector<Anchor> anchors_;

    CameraConfig config_;
    std::atomic<bool> is_running_{false};

    V4L2Camera camera_;
    RgaConverter rga_;
    RknnEngine npu_;

    // Dual Hardware MPP Encoders
    std::unique_ptr<camera::MppEncoder> mpp_main_;
    std::unique_ptr<camera::MppEncoder> mpp_sub_;

    // Embedded RTSP Server
    std::unique_ptr<camera::RtspServer> rtsp_server_;

    // CMA Zero-Copy Buffers
    DmaBuffer sub_stream_dma_; // 640x360 NV12 for Sub Stream & OSD
    DmaBuffer ai_dma_buffer_;  // 128x128 Gray8 for NPU input

    // Non-blocking trigger queue for AI worker
    BoundedQueue<uint64_t> ai_trigger_queue_{1};

    // Detected face bounding boxes shared between AI thread and stream encoder
    std::mutex detections_mutex_;
    std::vector<DetectionBox> active_boxes_;

    struct TrackedBox {
        DetectionBox box;
        int lost_frames{0};
    };
    std::vector<TrackedBox> tracked_faces_;

    std::thread capture_thread_;
    std::thread ai_thread_;
};

} // namespace edge_camera
