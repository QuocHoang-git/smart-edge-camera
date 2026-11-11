/**
 * @file camera_pipeline.cpp
 * @brief Implementation of multi-threaded production camera pipeline coordinator.
 */

#include "pipeline/camera_pipeline.hpp"

#include <iostream>
#include <chrono>

namespace edge_camera {

CameraPipeline::CameraPipeline(CameraConfig config)
    : config_(std::move(config)),
      camera_(config_.video_device) {}

CameraPipeline::~CameraPipeline() {
    stop();
}

bool CameraPipeline::init() {
    std::cout << "========================================================\n";
    std::cout << "  Initializing Smart Edge Camera Pipeline System        \n";
    std::cout << "========================================================\n";

    // 1. Initialize NPU Model
    std::cout << "[INIT] 1/4 Loading RKNN NPU model: " << config_.model_path << "...\n";
    if (!npu_.init(config_.model_path)) {
        std::cerr << "[PIPELINE ERROR] NPU initialization failed!\n";
        return false;
    }

    const auto& npu_input = npu_.input_attr();
    std::cout << "[INIT] Model input: " << npu_input.width << "x" << npu_input.height 
              << "x" << npu_input.channels << ", " << npu_input.size << " bytes.\n";

    // 2. Allocate DMA Contiguous Memory
    std::cout << "[INIT] 2/4 Allocating Sub-Stream DMA buffer...\n";
    if (!sub_dma_buffer_.allocate(npu_input.size)) {
        std::cerr << "[PIPELINE ERROR] Sub-Stream DMA allocation failed!\n";
        return false;
    }

    if (!npu_.bind_input_dma_buffer(sub_dma_buffer_.fd(), sub_dma_buffer_.data(), npu_input.size)) {
        std::cerr << "[PIPELINE ERROR] Binding DMA buffer to NPU failed!\n";
        return false;
    }

    // 3. Initialize RGA 2D Blitter
    std::cout << "[INIT] 3/4 Initializing Rockchip RGA 2D Accelerator...\n";
    if (!rga_.init()) {
        std::cerr << "[PIPELINE ERROR] RGA initialization failed!\n";
        return false;
    }

    // 4. Initialize V4L2 Camera
    std::cout << "[INIT] 4/4 Initializing V4L2 Camera (" << config_.main_width << "x" << config_.main_height << ")...\n";
    if (!camera_.init(config_.main_width, config_.main_height, V4L2_PIX_FMT_NV12, config_.buffer_count)) {
        std::cerr << "[PIPELINE ERROR] V4L2 Camera initialization failed!\n";
        return false;
    }

    std::cout << "[INIT] Pipeline initialized successfully.\n";
    return true;
}

bool CameraPipeline::start() {
    if (is_running_) return true;

    if (!camera_.start_stream()) {
        std::cerr << "[PIPELINE ERROR] Failed to start camera stream!\n";
        return false;
    }

    is_running_ = true;

    // Launch worker threads
    ai_thread_ = std::thread(&CameraPipeline::ai_worker_loop, this);
    capture_thread_ = std::thread(&CameraPipeline::capture_worker_loop, this);

    std::cout << "[PIPELINE INFO] Multi-threaded pipeline threads spawned and running.\n";
    return true;
}

void CameraPipeline::stop() {
    if (!is_running_) return;

    std::cout << "[PIPELINE INFO] Stopping camera pipeline...\n";
    is_running_ = false;

    ai_trigger_queue_.stop();

    if (capture_thread_.joinable()) capture_thread_.join();
    if (ai_thread_.joinable()) ai_thread_.join();

    camera_.stop_stream();
    std::cout << "[PIPELINE INFO] Camera pipeline stopped cleanly.\n";
}

void CameraPipeline::wait_until_stopped() {
    if (capture_thread_.joinable()) capture_thread_.join();
    if (ai_thread_.joinable()) ai_thread_.join();
}

void CameraPipeline::capture_worker_loop() {
    const auto& npu_input = npu_.input_attr();
    int dst_rga_format = (npu_input.channels == 1) ? RK_FORMAT_YCbCr_400 : RK_FORMAT_RGB_888;

    uint64_t frame_index = 0;
    auto last_fps_tp = std::chrono::steady_clock::now();
    uint32_t fps_counter = 0;

    while (is_running_) {
        FrameBuffer main_frame;
        if (!camera_.dequeue_frame(main_frame, 1000)) {
            continue;
        }

        // Zero-Copy Hardware RGA Blit to Sub-Stream
        bool blit_ok = rga_.blit_dma_to_dma(
            main_frame.dma_fd, main_frame.length, config_.main_width, config_.main_height,
            sub_dma_buffer_.fd(), npu_input.size, npu_input.width, npu_input.height,
            dst_rga_format
        );

        // Immediately release V4L2 buffer back to driver pool
        camera_.enqueue_frame(main_frame);

        if (blit_ok) {
            // Trigger AI thread non-blockingly with drop-oldest policy
            ai_trigger_queue_.push(frame_index);
        }

        frame_index++;
        fps_counter++;

        auto now = std::chrono::steady_clock::now();
        double elapsed_sec = std::chrono::duration<double>(now - last_fps_tp).count();
        if (elapsed_sec >= 2.0) {
            double current_fps = static_cast<double>(fps_counter) / elapsed_sec;
            std::cout << "[PRODUCER] Camera Ingestion Rate: " << current_fps << " FPS"
                      << " | Total Frames: " << frame_index 
                      << " | AI Drops: " << ai_trigger_queue_.drop_count() << "\n";
            fps_counter = 0;
            last_fps_tp = now;
        }
    }
}

void CameraPipeline::ai_worker_loop() {
    uint64_t processed_frames = 0;
    auto last_ai_tp = std::chrono::steady_clock::now();
    uint32_t ai_counter = 0;

    while (is_running_) {
        uint64_t frame_seq = 0;
        if (!ai_trigger_queue_.pop(frame_seq, 100)) {
            continue;
        }

        double inference_ms = 0.0;
        bool ok = npu_.run(inference_ms);

        if (ok) {
            processed_frames++;
            ai_counter++;

            auto now = std::chrono::steady_clock::now();
            double elapsed_sec = std::chrono::duration<double>(now - last_ai_tp).count();
            if (elapsed_sec >= 2.0) {
                double current_ai_fps = static_cast<double>(ai_counter) / elapsed_sec;
                std::cout << "  [CONSUMER NPU] BlazeFace AI Rate: " << current_ai_fps << " FPS"
                          << " | Latency: " << inference_ms << " ms"
                          << " | FrameSeq: #" << frame_seq << "\n";
                ai_counter = 0;
                last_ai_tp = now;
            }
        }
    }
}

} // namespace edge_camera
