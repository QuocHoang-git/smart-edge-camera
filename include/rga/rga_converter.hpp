/**
 * @file rga_converter.hpp
 * @brief End-to-end Zero-copy hardware 2D image scaling using DMA-BUF handles.
 */

#pragma once

#include <cstdint>
#include <cstddef>

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
     * Both buffers are passed via file descriptors, matching RGA handle requirements.
     * 
     * @param src_fd Source V4L2 DMA-BUF descriptor (1080p NV12)
     * @param src_size Total size of source buffer in bytes
     * @param src_w Source width (1920)
     * @param src_h Source height (1080)
     * @param dst_fd Destination CMA DMA-BUF descriptor (320x240 RGB888)
     * @param dst_size Total size of destination buffer in bytes
     * @param dst_w Target width (320)
     * @param dst_h Target height (240)
     * @return true on hardware execution success.
     */
    bool blit_dma_to_dma(
        int src_fd, size_t src_size, uint32_t src_w, uint32_t src_h,
        int dst_fd, size_t dst_size, uint32_t dst_w, uint32_t dst_h
    );

private:
    bool is_initialized_{false};
};

} // namespace edge_camera
