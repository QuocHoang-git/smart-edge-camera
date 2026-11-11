/**
 * @file main.cpp
 * @brief Phase 4 PoC: Complete Hardware Pipeline (V4L2 1080p -> RGA Downscale -> NPU BlazeFace).
 */

#include "camera/v4l2_camera.hpp"
#include "rga/rga_converter.hpp"
#include "utils/dma_allocator.hpp"
#include "npu/rknn_engine.hpp"

#include <iostream>
#include <chrono>

int main(int argc, char* argv[]) {
    std::string dev_node = "/dev/video11";
    std::string model_path = "/tmp/face_detection_rv1106.rknn";

    if (argc > 1) dev_node = argv[1];
    if (argc > 2) model_path = argv[2];

    std::cout << "========================================================\n";
    std::cout << "  Smart Edge Camera - Phase 4 NPU Face Detection Pipeline\n";
    std::cout << "  Hardware: Rockchip RV1103 (V4L2 + RGA + RKNN NPU)     \n";
    std::cout << "========================================================\n";

    constexpr uint32_t MAIN_W = 1920;
    constexpr uint32_t MAIN_H = 1080;

    // Step 1: Initialize RKNN NPU Engine and Inspect Model Topology
    edge_camera::RknnEngine npu;
    std::cout << "[INIT] Initializing RKNN NPU Engine with model: " << model_path << "...\n";
    if (!npu.init(model_path)) {
        std::cerr << "[ERROR] Failed to initialize RKNN NPU Engine!\n";
        return 1;
    }

    const auto& npu_input = npu.input_attr();
    uint32_t sub_w = npu_input.width;
    uint32_t sub_h = npu_input.height;
    uint32_t sub_c = npu_input.channels;
    size_t sub_size = npu_input.size;

    int dst_rga_format = (sub_c == 1) ? RK_FORMAT_YCbCr_400 : RK_FORMAT_RGB_888;

    std::cout << "[INIT] Dynamic Pipeline Linking: RGA Sub-Stream matching NPU Input ("
              << sub_w << "x" << sub_h << "x" << sub_c 
              << ", Format: " << (sub_c == 1 ? "YCbCr_400 (Gray8)" : "RGB888")
              << ", " << sub_size << " bytes)...\n";

    // Step 2: Allocate Physically Contiguous Memory for Sub-Stream via Linux DMA-Heap
    edge_camera::DmaBuffer sub_dma_buffer;
    std::cout << "[INIT] Allocating Sub-Stream buffer via CMA DMA-Heap...\n";
    if (!sub_dma_buffer.allocate(sub_size)) {
        std::cerr << "[ERROR] Failed to allocate CMA memory for Sub-Stream!\n";
        return 1;
    }

    // Step 3: Zero-Copy Binding of DMA Buffer to NPU Input Tensor
    if (!npu.bind_input_dma_buffer(sub_dma_buffer.fd(), sub_dma_buffer.data(), sub_size)) {
        std::cerr << "[ERROR] Failed to bind DMA buffer to NPU input tensor!\n";
        return 1;
    }

    // Step 4: Initialize RGA 2D Hardware Accelerator
    edge_camera::RgaConverter rga;
    std::cout << "[INIT] Initializing RGA 2D Hardware Accelerator...\n";
    if (!rga.init()) {
        std::cerr << "[ERROR] Failed to initialize RGA!\n";
        return 1;
    }

    // Step 5: Initialize V4L2 Camera (1080p NV12)
    edge_camera::V4L2Camera camera(dev_node);
    std::cout << "[INIT] Initializing V4L2 Camera (" << MAIN_W << "x" << MAIN_H << " NV12)...\n";
    if (!camera.init(MAIN_W, MAIN_H, V4L2_PIX_FMT_NV12, 4)) {
        std::cerr << "[ERROR] Camera initialization failed!\n";
        return 1;
    }

    if (!camera.start_stream()) {
        std::cerr << "[ERROR] Failed to start stream!\n";
        return 1;
    }

    // Step 6: Warm-up ISP pipeline (10 frames)
    std::cout << "[INIT] Warming up ISP pipeline (10 frames)...\n";
    for (int i = 0; i < 10; ++i) {
        edge_camera::FrameBuffer warmup_frame;
        if (camera.dequeue_frame(warmup_frame, 2000)) {
            camera.enqueue_frame(warmup_frame);
        }
    }
    std::cout << "[INIT] ISP warm-up complete. Starting Full End-to-End Pipeline Benchmark...\n";

    constexpr int TEST_FRAMES = 25;
    double total_rga_time_ms = 0.0;
    double total_npu_time_ms = 0.0;
    int successful_frames = 0;

    for (int i = 0; i < TEST_FRAMES; ++i) {
        edge_camera::FrameBuffer main_frame;
        if (!camera.dequeue_frame(main_frame, 2000)) {
            std::cerr << "[WARNING] Drop frame at index " << i << "\n";
            continue;
        }

        auto frame_start_tp = std::chrono::steady_clock::now();

        // Stage A: Hardware RGA Scaling (1080p NV12 -> 128x128 Gray8 via Zero-Copy DMA)
        auto t_rga_start = std::chrono::steady_clock::now();
        bool rga_ok = rga.blit_dma_to_dma(
            main_frame.dma_fd, main_frame.length, MAIN_W, MAIN_H,
            sub_dma_buffer.fd(), sub_size, sub_w, sub_h,
            dst_rga_format
        );
        auto t_rga_end = std::chrono::steady_clock::now();
        double rga_ms = std::chrono::duration<double, std::milli>(t_rga_end - t_rga_start).count();

        // Release Main Stream frame back to kernel immediately
        camera.enqueue_frame(main_frame);

        if (!rga_ok) {
            std::cerr << "[ERROR] RGA blit failed on frame " << i << "\n";
            continue;
        }

        // Stage B: Hardware NPU Zero-Copy Inference
        double npu_ms = 0.0;
        bool npu_ok = npu.run(npu_ms);

        auto frame_end_tp = std::chrono::steady_clock::now();
        double total_pipeline_ms = std::chrono::duration<double, std::milli>(frame_end_tp - frame_start_tp).count();

        if (!npu_ok) {
            std::cerr << "[ERROR] NPU inference failed on frame " << i << "\n";
        } else {
            total_rga_time_ms += rga_ms;
            total_npu_time_ms += npu_ms;
            successful_frames++;

            std::cout << " -> Frame #" << i 
                      << " | RGA Blit: " << rga_ms << " ms"
                      << " | NPU BlazeFace: " << npu_ms << " ms"
                      << " | Total Hardware Latency: " << total_pipeline_ms << " ms\n";
        }
    }

    std::cout << "[CLEANUP] Stopping stream...\n";
    camera.stop_stream();

    std::cout << "\n========================================================\n";
    std::cout << "  End-to-End Pipeline Performance Summary:              \n";
    if (successful_frames > 0) {
        std::cout << "  Successful Frames          : " << successful_frames << "/" << TEST_FRAMES << "\n";
        std::cout << "  Average RGA Hardware Blit  : " << (total_rga_time_ms / successful_frames) << " ms\n";
        std::cout << "  Average NPU Inference Time : " << (total_npu_time_ms / successful_frames) << " ms\n";
        std::cout << "  Total End-to-End Latency   : " << ((total_rga_time_ms + total_npu_time_ms) / successful_frames) << " ms\n";
        std::cout << "  Theoretical Max Vision FPS : " << (1000.0 / ((total_rga_time_ms + total_npu_time_ms) / successful_frames)) << " FPS\n";
    }
    std::cout << "========================================================\n";

    return 0;
}
