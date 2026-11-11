/**
 * @file main.cpp
 * @brief Production Smart Edge Camera Application Entrypoint.
 */

#include "pipeline/camera_pipeline.hpp"

#include <iostream>
#include <csignal>
#include <memory>
#include <chrono>
#include <thread>
#include <unistd.h>

static volatile std::sig_atomic_t g_stop_signal = 0;

static void signal_handler(int signal) {
    g_stop_signal = signal;
    const char msg[] = "\n[SIGNAL TRAP] Caught signal, triggering shutdown...\n";
    (void)write(STDERR_FILENO, msg, sizeof(msg) - 1);
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

    auto pipeline = std::make_shared<edge_camera::CameraPipeline>(config);

    // Register async-signal-safe handlers
    struct sigaction sa{};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    signal(SIGHUP, SIG_IGN);

    if (!pipeline->init()) {
        std::cerr << "[FATAL] Failed to initialize camera pipeline!\n";
        return 1;
    }

    if (!pipeline->start()) {
        std::cerr << "[FATAL] Failed to start camera pipeline!\n";
        return 1;
    }

    std::cout << "[SYSTEM] Dual-stream camera running in background.\n";
    std::cout << "[SYSTEM] Main: rtsp://192.168.8.2:8554/live/main\n";
    std::cout << "[SYSTEM] Sub:  rtsp://192.168.8.2:8554/live/sub\n";
    std::cout << "[SYSTEM] Press Ctrl+C to terminate.\n";

    // Main thread event loop (Signal check outside interrupt stack)
    while (!g_stop_signal) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << "\n[SIGNAL] Received signal (" << g_stop_signal << "). Stopping pipeline gracefully...\n";
    pipeline->stop();

    std::cout << "[SYSTEM] Firmware exited cleanly with code 0.\n";
    return 0;
}
