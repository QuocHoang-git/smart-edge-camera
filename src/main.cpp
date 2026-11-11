/**
 * @file main.cpp
 * @brief Phase 3 PoC: Dual-Stream Pipeline with Zero-Copy Hardware RGA Scaling & Conversion.
 */

#include "camera/v4l2_camera.hpp"
#include "rga/rga_converter.hpp"

#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>

int main(int argc, char* argv[]) {
    std::string dev_node = "/dev/video11";
    if (argc > 1) {
        dev_node = argv[1];
    }

    std::cout << "========================================================\n";
    std::cout << "  Smart Edge Camera - Phase 3 Dual-Stream RGA Pipeline  \n";
    std::cout << "  Hardware: Rockchip RV1103 (RGA 2D Blitter)            \n";
    std::cout << "========================================================\n";

    // Main Stream Spec: 1080p NV12 (Direct from ISP)
    constexpr uint32_t MAIN_W = 1920;
    constexpr uint32_t MAIN_H = 1080;

    // Sub Stream Spec: 320x240 RGB888 (Ready for NPU Tensor)
    constexpr uint32_t SUB_W = 320;
    constexpr uint32_t SUB_H = 240;
    constexpr size_t SUB_SIZE = SUB_W * SUB_H * 3; // 230,400 bytes (~0.22MB)

    edge_camera::V4L2Camera camera(dev_node);
    edge_camera::RgaConverter rga;

    std::cout << "[INIT] Initializing RGA 2D Hardware Accelerator...\n";
    if (!rga.init()) {
        std::cerr << "[ERROR] Failed to initialize RGA!\n";
        return 1;
    }

    std::cout << "[INIT] Initializing V4L2 Camera (" << MAIN_W << "x" << MAIN_H << " NV12)...\n";
    if (!camera.init(MAIN_W, MAIN_H, V4L2_PIX_FMT_NV12, 3)) {
        std::cerr << "[ERROR] Camera initialization failed!\n";
        return 1;
    }

    if (!camera.start_stream()) {
        std::cerr << "[ERROR] Failed to start stream!\n";
        return 1;
    }

    // Allocate memory for Sub-Stream RGB buffer (Only 230 KB!)
    std::vector<uint8_t> sub_stream_buffer(SUB_SIZE);

    constexpr int TEST_FRAMES = 20;
    std::cout << "[RUN] Processing " << TEST_FRAMES << " frames across Dual-Stream Pipeline...\n";

    double total_rga_time_ms = 0.0;
    bool sub_dumped = false;

    for (int i = 0; i < TEST_FRAMES; ++i) {
        edge_camera::FrameBuffer main_frame;
        if (!camera.dequeue_frame(main_frame, 2000)) {
            std::cerr << "[WARNING] Drop frame at index " << i << "\n";
            continue;
        }

        // Hardware Acceleration: 1080p NV12 -> 320x240 RGB888 via RGA
        auto t0 = std::chrono::steady_clock::now();

        bool rga_ok = rga.convert_nv12_to_rgb888(
            main_frame.start, MAIN_W, MAIN_H,
            sub_stream_buffer.data(), SUB_W, SUB_H
        );

        auto t1 = std::chrono::steady_clock::now();
        double rga_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        total_rga_time_ms += rga_ms;

        if (!rga_ok) {
            std::cerr << "[ERROR] RGA conversion failed on frame " << i << "\n";
        } else {
            std::cout << " -> Frame #" << i 
                      << " | Main: " << MAIN_W << "x" << MAIN_H << " NV12 (" << main_frame.length << " B)"
                      << " | Sub: " << SUB_W << "x" << SUB_H << " RGB888"
                      << " | RGA HW Latency: " << rga_ms << " ms\n";
        }

        // Dump 1st Sub-Stream frame for visual/hex inspection
        if (!sub_dumped && rga_ok) {
            const std::string dump_path = "/userdata/substream_320x240.rgb";
            std::ofstream out(dump_path, std::ios::binary);
            if (out.is_open()) {
                out.write(reinterpret_cast<const char*>(sub_stream_buffer.data()), static_cast<std::streamsize>(SUB_SIZE));
                out.close();
                std::cout << "[SUCCESS] Dumped Sub-Stream RGB888 frame to " << dump_path << "\n";
            }
            sub_dumped = true;
        }

        // Release Main Stream frame back to kernel immediately
        camera.enqueue_frame(main_frame);
    }

    camera.stop_stream();

    std::cout << "\n========================================================\n";
    std::cout << "  Dual-Stream Performance Summary:                      \n";
    std::cout << "  Average RGA Hardware Time : " << (total_rga_time_ms / TEST_FRAMES) << " ms\n";
    std::cout << "========================================================\n";

    return 0;
}
