/**
 * @file rga_converter.cpp
 * @brief Implementation of hardware accelerated RGA image converter.
 */

#include "rga/rga_converter.hpp"

#include <iostream>
#include <chrono>
#include <rga/rga.h>
#include <rga/im2d.h>
#include <rga/RgaApi.h>

namespace edge_camera {

RgaConverter::RgaConverter() = default;

RgaConverter::~RgaConverter() {
    if (is_initialized_) {
        c_RkRgaDeInit();
        is_initialized_ = false;
    }
}

bool RgaConverter::init() {
    int ret = c_RkRgaInit();
    if (ret != 0) {
        std::cerr << "[RGA ERROR] c_RkRgaInit failed with code: " << ret << "\n";
        return false;
    }

    is_initialized_ = true;
    std::cout << "[RGA INFO] Rockchip RGA 2D hardware engine initialized.\n";
    return true;
}

bool RgaConverter::convert_nv12_to_rgb888(
    const void* src_ptr, uint32_t src_w, uint32_t src_h,
    void* dst_ptr, uint32_t dst_w, uint32_t dst_h) 
{
    if (!is_initialized_) {
        std::cerr << "[RGA ERROR] RGA converter not initialized!\n";
        return false;
    }

    if (!src_ptr || !dst_ptr) {
        std::cerr << "[RGA ERROR] Null buffer pointers passed to converter!\n";
        return false;
    }

    // Call underlying C function directly to ensure C++17 conformance
    // Source: NV12 (RK_FORMAT_YCbCr_420_SP)
    rga_buffer_t src = wrapbuffer_virtualaddr_t(
        const_cast<void*>(src_ptr),
        static_cast<int>(src_w),
        static_cast<int>(src_h),
        static_cast<int>(src_w),
        static_cast<int>(src_h),
        RK_FORMAT_YCbCr_420_SP
    );

    // Destination: RGB888 (RK_FORMAT_RGB_888)
    rga_buffer_t dst = wrapbuffer_virtualaddr_t(
        dst_ptr,
        static_cast<int>(dst_w),
        static_cast<int>(dst_h),
        static_cast<int>(dst_w),
        static_cast<int>(dst_h),
        RK_FORMAT_RGB_888
    );

    // improcess executes hardware downscale and color transformation
    int ret = improcess(src, dst, {}, {}, {}, {}, 0);

    if (ret != IM_STATUS_SUCCESS) {
        std::cerr << "[RGA ERROR] Hardware improcess failed with code: " << ret << "\n";
        return false;
    }

    return true;
}

} // namespace edge_camera
