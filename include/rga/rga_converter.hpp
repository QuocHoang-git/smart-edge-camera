/**
 * @file rga_converter.hpp
 * @brief Zero-copy hardware 2D image scaling and color conversion using Rockchip RGA.
 */

#pragma once

#include <cstdint>
#include <cstddef>

namespace edge_camera {

class RgaConverter {
public:
    RgaConverter();
    ~RgaConverter();

    // Disable copy
    RgaConverter(const RgaConverter&) = delete;
    RgaConverter& operator=(const RgaConverter&) = delete;

    /**
     * @brief Initialize Rockchip RGA context.
     * @return true if RGA hardware context is ready.
     */
    bool init();

    /**
     * @brief Hardware downscale and color conversion (NV12 -> RGB888).
     * 
     * Uses Rockchip 2D hardware blit accelerator. No CPU interpolation or memcpy occurs.
     * 
     * @param src_ptr Pointer to source NV12 buffer (e.g. from V4L2 mmap)
     * @param src_w Source width (e.g. 1920)
     * @param src_h Source height (e.g. 1080)
     * @param dst_ptr Pointer to destination RGB888 buffer
     * @param dst_w Target width (e.g. 320)
     * @param dst_h Target height (e.g. 240)
     * @return true on hardware execution success.
     */
    bool convert_nv12_to_rgb888(
        const void* src_ptr, uint32_t src_w, uint32_t src_h,
        void* dst_ptr, uint32_t dst_w, uint32_t dst_h
    );

private:
    bool is_initialized_{false};
};

} // namespace edge_camera
