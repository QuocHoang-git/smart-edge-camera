/**
 * @file main.cpp
 * @brief Phase 2 PoC: V4L2 streaming capture, frame latency, and raw dump verification.
 */

#include "camera/v4l2_camera.hpp"

#include <iostream>
#include <fstream>
#include <chrono>

int main(int argc, char* argv[]) {
    std::string dev_node = "/dev/video11";
    if (argc > 1) {
        dev_node = argv[1];
    }

    std::cout << "========================================================\n";
    std::cout << "  Smart Edge Camera - Phase 2 V4L2 Hardware Capture     \n";
    std::cout << "  Target Device Node: " << dev_node << "\n";
    std::cout << "========================================================\n";

    constexpr uint32_t WIDTH = 1920;
    constexpr uint32_t HEIGHT = 1080;
    constexpr uint32_t BUFFER_COUNT = 3;

    edge_camera::V4L2Camera camera(dev_node);

    std::cout << "[PHASE 2] Initializing camera: " << WIDTH << "x" << HEIGHT << " (NV12)...\n";
    if (!camera.init(WIDTH, HEIGHT, V4L2_PIX_FMT_NV12, BUFFER_COUNT)) {
        std::cerr << "[ERROR] Camera initialization failed!\n";
        return 1;
    }

    std::cout << "[PHASE 2] Starting video stream...\n";
    if (!camera.start_stream()) {
        std::cerr << "[ERROR] Failed to start video stream!\n";
        return 1;
    }

    constexpr int CAPTURE_COUNT = 15;
    std::cout << "[PHASE 2] Capturing " << CAPTURE_COUNT << " test frames...\n";

    bool frame_dumped = false;
    auto last_tp = std::chrono::steady_clock::now();

    for (int i = 0; i < CAPTURE_COUNT; ++i) {
        edge_camera::FrameBuffer buffer;
        if (!camera.dequeue_frame(buffer, 3000)) {
            std::cerr << "[ERROR] Failed to dequeue frame " << i << "\n";
            continue;
        }

        auto now = std::chrono::steady_clock::now();
        double elapsed_ms = std::chrono::duration<double, std::milli>(now - last_tp).count();
        last_tp = now;

        std::cout << " -> Frame #" << i 
                  << " [Index: " << buffer.index 
                  << ", Size: " << buffer.length << " bytes"
                  << ", Interval: " << elapsed_ms << " ms"
                  << "]\n";

        // Dump 1st frame to flash for analysis
        if (!frame_dumped && buffer.start != nullptr) {
            const std::string dump_path = "/userdata/test_frame_1920x1080.nv12";
            std::ofstream out(dump_path, std::ios::binary);
            if (out.is_open()) {
                out.write(static_cast<const char*>(buffer.start), static_cast<std::streamsize>(buffer.length));
                out.close();
                std::cout << "[SUCCESS] Saved first frame raw data to: " << dump_path << "\n";
            } else {
                std::cerr << "[WARNING] Failed to open " << dump_path << " for writing.\n";
            }
            frame_dumped = true;
        }

        // Must re-enqueue the buffer back to kernel
        if (!camera.enqueue_frame(buffer)) {
            std::cerr << "[ERROR] Failed to enqueue buffer " << buffer.index << "\n";
        }
    }

    std::cout << "[PHASE 2] Stopping stream...\n";
    camera.stop_stream();

    std::cout << "========================================================\n";
    std::cout << "  Phase 2 Test Completed Successfully!                  \n";
    std::cout << "========================================================\n";

    return 0;
}
