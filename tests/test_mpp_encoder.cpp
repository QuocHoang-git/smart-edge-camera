/**
 * @file test_mpp_encoder.cpp
 * @brief Integration tests for Rockchip MPP Hardware H.264 Encoder.
 */

#include "encoder/mpp_encoder.hpp"
#include "utils/dma_allocator.hpp"
#include "test_framework.hpp"

#include <cstring>
#include <chrono>

TEST_CASE(mpp_encoder_initialization) {
    camera::MppEncoderConfig cfg;
    cfg.width = 640;
    cfg.height = 360;
    cfg.fps = 30;
    cfg.gop = 60;
    cfg.bps = 800000;
    cfg.is_h265 = false;

    camera::MppEncoder encoder(cfg);
    ASSERT_TRUE(encoder.init());
}

TEST_CASE(mpp_encode_dma_frame) {
    camera::MppEncoderConfig cfg;
    cfg.width = 640;
    cfg.height = 360;
    cfg.fps = 30;
    cfg.gop = 30;
    cfg.bps = 800000;
    cfg.is_h265 = false;

    camera::MppEncoder encoder(cfg);
    ASSERT_TRUE(encoder.init());

    size_t frame_bytes = cfg.width * cfg.height * 3 / 2;
    edge_camera::DmaBuffer dma;
    ASSERT_TRUE(dma.allocate(frame_bytes));
    std::memset(dma.data(), 0x60, frame_bytes); // Gray NV12

    for (int i = 0; i < 3; ++i) {
        uint64_t pts = i * 33333;
        camera::EncodedPacket pkt;
        bool ok = encoder.encode_dma(dma.fd(), frame_bytes, pts, pkt);
        ASSERT_TRUE(ok);
        ASSERT_GE(pkt.data.size(), 16); // Non-empty H.264 stream
        if (i == 0) {
            // First packet should typically be a keyframe (IDR + SPS/PPS)
            ASSERT_TRUE(pkt.is_keyframe);
        }
    }
}

int main() {
    return test_runner::run_all_tests("Rockchip MPP Encoder Hardware Tests");
}
