/**
 * @file rga_converter.hpp
 * @brief End-to-end Zero-copy hardware 2D image scaling using DMA-BUF handles.
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
     * @param dst_w Target width (128)
     * @param dst_h Target height (128)
     * @param dst_format RGA format code (e.g. RK_FORMAT_YCbCr_400 for 1-channel Grayscale)
     * @return true on hardware execution success.
     */
    bool blit_dma_to_dma(
        int src_fd, size_t src_size, uint32_t src_w, uint32_t src_h,
        int dst_fd, size_t dst_size, uint32_t dst_w, uint32_t dst_h,
        int dst_format = RK_FORMAT_YCbCr_400
    );

private:
    bool is_initialized_{false};
};

} // namespace edge_camera
