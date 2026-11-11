/**
 * @file main.cpp
 * @brief Phase 3 PoC: Dual-Stream Pipeline with End-to-End Zero-Copy DMA-BUF RGA Hardware Blit.
 */

#include "camera/v4l2_camera.hpp"
#include "rga/rga_converter.hpp"
#include "utils/dma_allocator.hpp"

#include <iostream>
#include <chrono>

int main(int argc, char* argv[]) {
    std::string dev_node = "/dev/video11";
    if (argc > 1) {
        dev_node = argv[1];
    }

    std::cout << "========================================================\n";
    std::cout << "  Smart Edge Camera - Phase 3 Dual-Stream RGA Pipeline  \n";
    std::cout << "  Hardware: Rockchip RV1103 (End-to-End Zero-Copy DMA)  \n";
    std::cout << "========================================================\n";

    constexpr uint32_t MAIN_W = 1920;
    constexpr uint32_t MAIN_H = 1080;

    constexpr uint32_t SUB_W = 320;
    constexpr uint32_t SUB_H = 240;
    constexpr size_t SUB_SIZE = SUB_W * SUB_H * 3; // 230,400 bytes

    // Allocate Sub-Stream buffer via CMA DMA-Heap
    edge_camera::DmaBuffer sub_dma_buffer;
    std::cout << "[INIT] Allocating Sub-Stream buffer via CMA DMA-Heap...\n";
    if (!sub_dma_buffer.allocate(SUB_SIZE)) {
        std::cerr << "[ERROR] Failed to allocate CMA memory for Sub-Stream!\n";
        return 1;
    }

    edge_camera::RgaConverter rga;
    std::cout << "[INIT] Initializing RGA 2D Hardware Accelerator...\n";
    if (!rga.init()) {
        std::cerr << "[ERROR] Failed to initialize RGA!\n";
        return 1;
    }

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

    // Warm-up ISP pipeline (10 frames)
    std::cout << "[INIT] Warming up ISP pipeline (10 frames)...\n";
    for (int i = 0; i < 10; ++i) {
        edge_camera::FrameBuffer warmup_frame;
        if (camera.dequeue_frame(warmup_frame, 2000)) {
            camera.enqueue_frame(warmup_frame);
        }
    }
    std::cout << "[INIT] ISP warm-up complete. Starting Dual-Stream benchmark...\n";

    constexpr int TEST_FRAMES = 25;
    double total_rga_time_ms = 0.0;
    int successful_frames = 0;

    for (int i = 0; i < TEST_FRAMES; ++i) {
        edge_camera::FrameBuffer main_frame;
        if (!camera.dequeue_frame(main_frame, 2000)) {
            std::cerr << "[WARNING] Drop frame at index " << i << "\n";
            continue;
        }

        // Hardware Acceleration: Zero-Copy DMA to DMA Blit
        auto t0 = std::chrono::steady_clock::now();

        bool rga_ok = rga.blit_dma_to_dma(
            main_frame.dma_fd, main_frame.length, MAIN_W, MAIN_H,
            sub_dma_buffer.fd(), SUB_SIZE, SUB_W, SUB_H
        );

        auto t1 = std::chrono::steady_clock::now();
        double rga_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        // Release Main Stream frame back to kernel immediately
        camera.enqueue_frame(main_frame);

        if (!rga_ok) {
            std::cerr << "[ERROR] RGA conversion failed on frame " << i << "\n";
        } else {
            total_rga_time_ms += rga_ms;
            successful_frames++;
            std::cout << " -> Frame #" << i 
                      << " | Main DMA-FD: " << main_frame.dma_fd 
                      << " | Sub DMA-FD: " << sub_dma_buffer.fd() 
                      << " | RGA HW Latency: " << rga_ms << " ms\n";
        }
    }

    std::cout << "[CLEANUP] Stopping stream...\n";
    camera.stop_stream();

    std::cout << "\n========================================================\n";
    std::cout << "  Dual-Stream Performance Summary:                      \n";
    if (successful_frames > 0) {
        std::cout << "  Successful Frames         : " << successful_frames << "/" << TEST_FRAMES << "\n";
        std::cout << "  Average RGA Hardware Time : " << (total_rga_time_ms / successful_frames) << " ms\n";
    }
    std::cout << "========================================================\n";

    return 0;
}
