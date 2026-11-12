/**
 * @file rga_converter.cpp
 * @brief Implementation of hardware accelerated RGA image converter and OSD drawer.
 */

#include "rga/rga_converter.hpp"

#include <iostream>
#include <algorithm>
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
    int dst_fd, size_t dst_size, uint32_t dst_w, uint32_t dst_h,
    int dst_format) 
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

    // Wrap both using hardware handles
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
        dst_format
    );

    // Hardware downscale (sync=1 wait until complete)
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

bool RgaConverter::blit_letterbox_dma_to_dma(
    int src_fd, size_t src_size, uint32_t src_w, uint32_t src_h,
    int dst_fd, size_t dst_size, uint32_t dst_w, uint32_t dst_h,
    int dst_pad_top, int dst_pad_bottom,
    int dst_format)
{
    if (!is_initialized_) {
        std::cerr << "[RGA ERROR] RGA converter not initialized!\n";
        return false;
    }

    if (src_fd < 0 || dst_fd < 0) {
        return false;
    }

    rga_buffer_handle_t src_handle = importbuffer_fd(src_fd, static_cast<int>(src_size));
    if (src_handle == 0) return false;

    rga_buffer_handle_t dst_handle = importbuffer_fd(dst_fd, static_cast<int>(dst_size));
    if (dst_handle == 0) {
        releasebuffer_handle(src_handle);
        return false;
    }

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
        dst_format
    );

    im_rect srect = {0, 0, static_cast<int>(src_w), static_cast<int>(src_h)};
    im_rect drect = {0, dst_pad_top, static_cast<int>(dst_w), static_cast<int>(dst_h - dst_pad_top - dst_pad_bottom)};

    IM_STATUS ret = improcess(src, dst, {}, srect, drect, {}, IM_SYNC);

    releasebuffer_handle(src_handle);
    releasebuffer_handle(dst_handle);

    if (ret != IM_STATUS_SUCCESS) {
        std::cerr << "[RGA ERROR] blit_letterbox improcess failed with code: " << static_cast<int>(ret) << "\n";
        return false;
    }

    return true;
}

bool RgaConverter::draw_rect_dma(
    int dst_fd, size_t dst_size, uint32_t dst_w, uint32_t dst_h,
    int x, int y, int w, int h, int thickness,
    uint32_t color_argb, int dst_format)
{
    if (!is_initialized_ || dst_fd < 0 || w <= 0 || h <= 0) {
        return false;
    }

    rga_buffer_handle_t dst_handle = importbuffer_fd(dst_fd, static_cast<int>(dst_size));
    if (dst_handle == 0) {
        return false;
    }

    rga_buffer_t dst = wrapbuffer_handle(
        dst_handle,
        static_cast<int>(dst_w),
        static_cast<int>(dst_h),
        dst_format
    );

    // Clamp boundary
    int x1 = std::max(0, x);
    int y1 = std::max(0, y);
    int x2 = std::min(static_cast<int>(dst_w), x + w);
    int y2 = std::min(static_cast<int>(dst_h), y + h);

    // Align to 2 for NV12 chroma subsampling compatibility
    x1 &= ~1;
    y1 &= ~1;
    x2 &= ~1;
    y2 &= ~1;

    int rect_w = x2 - x1;
    int rect_h = y2 - y1;
    int t = std::min(thickness, std::min(rect_w, rect_h));
    t = (t < 2) ? 2 : (t & ~1); // ensure thickness is even and >= 2

    if (rect_w > 4 && rect_h > 4) {
        // Draw 4 borders: Top, Bottom, Left, Right
        im_rect top = {x1, y1, rect_w, t};
        im_rect bottom = {x1, y2 - t, rect_w, t};
        im_rect left = {x1, y1, t, rect_h};
        im_rect right = {x2 - t, y1, t, rect_h};

        imfill_t(dst, top, static_cast<int>(color_argb), 1);
        imfill_t(dst, bottom, static_cast<int>(color_argb), 1);
        imfill_t(dst, left, static_cast<int>(color_argb), 1);
        imfill_t(dst, right, static_cast<int>(color_argb), 1);
    }

    releasebuffer_handle(dst_handle);
    return true;
}

} // namespace edge_camera
