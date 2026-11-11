/**
 * @file rga_converter.cpp
 * @brief Implementation of hardware accelerated RGA image converter.
 */

#include "rga/rga_converter.hpp"

#include <iostream>
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

bool RgaConverter::blit_dma_to_dma(
    int src_fd, size_t src_size, uint32_t src_w, uint32_t src_h,
    int dst_fd, size_t dst_size, uint32_t dst_w, uint32_t dst_h) 
{
    if (!is_initialized_) {
        std::cerr << "[RGA ERROR] RGA converter not initialized!\n";
        return false;
    }

    if (src_fd < 0 || dst_fd < 0) {
        std::cerr << "[RGA ERROR] Invalid file descriptors: src_fd=" << src_fd << ", dst_fd=" << dst_fd << "\n";
        return false;
    }

    // Import BOTH source and destination into RGA kernel driver
    rga_buffer_handle_t src_handle = importbuffer_fd(src_fd, static_cast<int>(src_size));
    if (src_handle == 0) {
        std::cerr << "[RGA ERROR] Failed to import src_fd " << src_fd << "\n";
        return false;
    }

    rga_buffer_handle_t dst_handle = importbuffer_fd(dst_fd, static_cast<int>(dst_size));
    if (dst_handle == 0) {
        std::cerr << "[RGA ERROR] Failed to import dst_fd " << dst_fd << "\n";
        releasebuffer_handle(src_handle);
        return false;
    }

    // Wrap both using hardware handles (Rule: both must use handles!)
    rga_buffer_t src = wrapbuffer_handle(
        src_handle,
        static_cast<int>(src_w),
        static_cast<int>(src_h),
        RK_FORMAT_YCbCr_420_SP
    );

    rga_buffer_t dst = wrapbuffer_handle(
        dst_handle,
        static_cast<int>(dst_w),
        static_cast<int>(dst_h),
        RK_FORMAT_RGB_888
    );

    // Hardware downscale + color conversion (sync=1 wait until complete)
    IM_STATUS ret = imresize_t(src, dst, 0, 0, 0, 1);

    // Clean up handles
    releasebuffer_handle(src_handle);
    releasebuffer_handle(dst_handle);

    if (ret != IM_STATUS_SUCCESS) {
        std::cerr << "[RGA ERROR] imresize_t failed with code: " << static_cast<int>(ret) << "\n";
        return false;
    }

    return true;
}

} // namespace edge_camera
