/**
 * @file test_v4l2_camera.cpp
 * @brief Integration tests for V4L2 Camera capture on Luckfox Pico.
 */

#include "camera/v4l2_camera.hpp"
#include "test_framework.hpp"

#include <linux/videodev2.h>

TEST_CASE(camera_init_and_buffer_negotiation) {
    edge_camera::V4L2Camera cam("/dev/video11");
    bool ok = cam.init(1920, 1080, V4L2_PIX_FMT_NV12, 4);
    if (!ok) {
        std::cout << " [SKIP: Camera device /dev/video11 not accessible or in use] ";
        return;
    }
    ASSERT_TRUE(ok);
}

TEST_CASE(camera_stream_capture_frames) {
    edge_camera::V4L2Camera cam("/dev/video11");
    if (!cam.init(1920, 1080, V4L2_PIX_FMT_NV12, 4)) {
        std::cout << " [SKIP: Camera device /dev/video11 not accessible or in use] ";
        return;
    }

    ASSERT_TRUE(cam.start_stream());

    for (int i = 0; i < 5; ++i) {
        edge_camera::FrameBuffer frame;
        bool dequeued = cam.dequeue_frame(frame, 1500);
        ASSERT_TRUE(dequeued);
        ASSERT_GE(frame.dma_fd, 0);
        ASSERT_GE(frame.length, 1920 * 1080);

        ASSERT_TRUE(cam.enqueue_frame(frame));
    }

    cam.stop_stream();
}

int main() {
    return test_runner::run_all_tests("V4L2 Camera Hardware Tests");
}
