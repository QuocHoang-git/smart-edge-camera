/**
 * @file test_rga_converter.cpp
 * @brief Integration tests for Rockchip RGA 2D Hardware Acceleration.
 */

#include "rga/rga_converter.hpp"
#include "utils/dma_allocator.hpp"
#include "test_framework.hpp"

#include <cstring>

TEST_CASE(rga_initialization) {
    edge_camera::RgaConverter rga;
    ASSERT_TRUE(rga.init());
}

TEST_CASE(rga_blit_scale_nv12) {
    edge_camera::RgaConverter rga;
    ASSERT_TRUE(rga.init());

    constexpr uint32_t SRC_W = 1920;
    constexpr uint32_t SRC_H = 1080;
    constexpr uint32_t DST_W = 640;
    constexpr uint32_t DST_H = 360;

    size_t src_size = SRC_W * SRC_H * 3 / 2;
    size_t dst_size = DST_W * DST_H * 3 / 2;

    edge_camera::DmaBuffer src_dma;
    edge_camera::DmaBuffer dst_dma;

    ASSERT_TRUE(src_dma.allocate(src_size));
    ASSERT_TRUE(dst_dma.allocate(dst_size));

    // Fill source with dummy pattern
    std::memset(src_dma.data(), 0x80, src_size);

    bool ok = rga.blit_dma_to_dma(
        src_dma.fd(), src_size, SRC_W, SRC_H,
        dst_dma.fd(), dst_size, DST_W, DST_H,
        RK_FORMAT_YCbCr_420_SP
    );

    ASSERT_TRUE(ok);
}

TEST_CASE(rga_blit_letterbox_roi) {
    edge_camera::RgaConverter rga;
    ASSERT_TRUE(rga.init());

    constexpr uint32_t SRC_W = 640;
    constexpr uint32_t SRC_H = 360;
    constexpr uint32_t DST_W = 640;
    constexpr uint32_t DST_H = 640;
    constexpr int PAD_TOP = 140;
    constexpr int PAD_BOTTOM = 140;

    size_t src_size = SRC_W * SRC_H * 3 / 2;      // NV12
    size_t dst_size = DST_W * DST_H * 3;          // BGR888

    edge_camera::DmaBuffer src_dma;
    edge_camera::DmaBuffer dst_dma;

    ASSERT_TRUE(src_dma.allocate(src_size));
    ASSERT_TRUE(dst_dma.allocate(dst_size));

    std::memset(src_dma.data(), 0x55, src_size);
    std::memset(dst_dma.data(), 0, dst_size); // black padding

    bool ok = rga.blit_letterbox_dma_to_dma(
        src_dma.fd(), src_size, SRC_W, SRC_H,
        dst_dma.fd(), dst_size, DST_W, DST_H,
        PAD_TOP, PAD_BOTTOM,
        RK_FORMAT_BGR_888
    );

    ASSERT_TRUE(ok);
}

TEST_CASE(rga_draw_bounding_box_dma) {
    edge_camera::RgaConverter rga;
    ASSERT_TRUE(rga.init());

    constexpr uint32_t WIDTH = 640;
    constexpr uint32_t HEIGHT = 360;
    size_t buf_size = WIDTH * HEIGHT * 3 / 2;

    edge_camera::DmaBuffer dma;
    ASSERT_TRUE(dma.allocate(buf_size));
    std::memset(dma.data(), 0x10, buf_size);

    // Draw box (x=100, y=50, w=120, h=160) with thickness=2 in bright green
    bool ok = rga.draw_rect_dma(
        dma.fd(), buf_size, WIDTH, HEIGHT,
        100, 50, 120, 160, 2, 0x0000FF00,
        RK_FORMAT_YCbCr_420_SP
    );

    ASSERT_TRUE(ok);
}

int main() {
    return test_runner::run_all_tests("RGA 2D Blitter Hardware Tests");
}
