/**
 * @file main.cpp
 * @brief Production Smart Edge Camera Application Entrypoint.
 */

#include "pipeline/camera_pipeline.hpp"

#include <iostream>
#include <csignal>
#include <memory>

static std::shared_ptr<edge_camera::CameraPipeline> g_pipeline = nullptr;

static void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "\n[SIGNAL] Received shutdown signal (" << signal << "). Terminating pipeline...\n";
        if (g_pipeline) {
            g_pipeline->stop();
        }
    }
}

int main(int argc, char* argv[]) {
    edge_camera::CameraConfig config;
    if (argc > 1) config.video_device = argv[1];
    if (argc > 2) config.model_path = argv[2];

    std::cout << "==================================================================\n";
    std::cout << "  Smart Edge Camera - Dual-Stream & Edge Face Detection (24/7)    \n";
    std::cout << "  Platform: Rockchip RV1103 (Luckfox Pico)                        \n";
    std::cout << "  Target: Motorola Solutions Firmware Specialization              \n";
    std::cout << "==================================================================\n";

    g_pipeline = std::make_shared<edge_camera::CameraPipeline>(config);

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    if (!g_pipeline->init()) {
        std::cerr << "[FATAL] Failed to initialize camera pipeline!\n";
        return 1;
    }

    if (!g_pipeline->start()) {
        std::cerr << "[FATAL] Failed to start camera pipeline!\n";
        return 1;
    }

    std::cout << "[SYSTEM] Camera running in background. Press Ctrl+C to terminate.\n";
    g_pipeline->wait_until_stopped();

    std::cout << "[SYSTEM] Firmware exited cleanly with code 0.\n";
    return 0;
}
