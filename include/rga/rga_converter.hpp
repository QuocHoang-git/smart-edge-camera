/**
 * @file rga_converter.hpp
 * @brief End-to-end Zero-copy hardware 2D image scaling and OSD drawing using DMA-BUF handles.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <rga/rga.h>

namespace edge_camera {

class RgaConverter {
public:
    RgaConverter();
    ~RgaConverter();

    RgaConverter(const RgaConverter&) = delete;
    RgaConverter& operator=(const RgaConverter&) = delete;

    /**
     * @brief Initialize Rockchip RGA context.
     */
    bool init();

    /**
     * @brief Zero-Copy Hardware Blit from Source DMA-BUF to Destination DMA-BUF.
     * 
     * @param src_fd Source V4L2 DMA-BUF descriptor (1080p NV12)
     * @param src_size Total size of source buffer in bytes
     * @param src_w Source width (1920)
     * @param src_h Source height (1080)
     * @param dst_fd Destination CMA DMA-BUF descriptor
     * @param dst_size Total size of destination buffer in bytes
     * @param dst_w Target width (e.g. 640 or 128)
     * @param dst_h Target height (e.g. 360 or 128)
     * @param dst_format RGA format code (e.g. RK_FORMAT_YCbCr_400 or RK_FORMAT_YCbCr_420_SP)
     * @return true on hardware execution success.
     */
    bool blit_dma_to_dma(
        int src_fd, size_t src_size, uint32_t src_w, uint32_t src_h,
        int dst_fd, size_t dst_size, uint32_t dst_w, uint32_t dst_h,
        int dst_format = RK_FORMAT_YCbCr_400
    );

    /**
     * @brief Zero-Copy Hardware Drawing of a Bounding Box Rectangle into DMA-BUF.
     * 
     * @param dst_fd Destination CMA DMA-BUF descriptor (e.g. Sub-stream NV12)
     * @param dst_size Destination buffer size in bytes
     * @param dst_w Image width
     * @param dst_h Image height
     * @param x Top-left X coordinate
     * @param y Top-left Y coordinate
     * @param w Width of bounding box
     * @param h Height of bounding box
     * @param thickness Line thickness in pixels
     * @param color_argb ARGB color (e.g., 0x00FF00 for bright green)
     * @param dst_format Format of target buffer (default NV12)
     * @return true on hardware execution success.
     */
    bool draw_rect_dma(
        int dst_fd, size_t dst_size, uint32_t dst_w, uint32_t dst_h,
        int x, int y, int w, int h, int thickness = 2,
        uint32_t color_argb = 0x0000FF00, int dst_format = RK_FORMAT_YCbCr_420_SP
    );

private:
    bool is_initialized_{false};
};

} // namespace edge_camera
