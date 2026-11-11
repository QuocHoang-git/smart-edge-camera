/**
 * @file v4l2_camera.cpp
 * @brief Implementation of V4L2 Multiplanar video capture.
 */

#include "camera/v4l2_camera.hpp"

#include <iostream>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>

namespace edge_camera {

V4L2Camera::V4L2Camera(std::string device_path)
    : device_path_(std::move(device_path)) {}

V4L2Camera::~V4L2Camera() {
    stop_stream();
    unmap_and_release_buffers();
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}

// Move constructor
V4L2Camera::V4L2Camera(V4L2Camera&& other) noexcept
    : device_path_(std::move(other.device_path_)),
      fd_(other.fd_),
      width_(other.width_),
      height_(other.height_),
      pixel_format_(other.pixel_format_),
      is_streaming_(other.is_streaming_),
      buffers_(std::move(other.buffers_)) {
    other.fd_ = -1;
    other.is_streaming_ = false;
}

// Move assignment
V4L2Camera& V4L2Camera::operator=(V4L2Camera&& other) noexcept {
    if (this != &other) {
        stop_stream();
        unmap_and_release_buffers();
        if (fd_ >= 0) close(fd_);

        device_path_ = std::move(other.device_path_);
        fd_ = other.fd_;
        width_ = other.width_;
        height_ = other.height_;
        pixel_format_ = other.pixel_format_;
        is_streaming_ = other.is_streaming_;
        buffers_ = std::move(other.buffers_);

        other.fd_ = -1;
        other.is_streaming_ = false;
    }
    return *this;
}

bool V4L2Camera::init(uint32_t width, uint32_t height, uint32_t pixel_format, uint32_t buffer_count) {
    width_ = width;
    height_ = height;
    pixel_format_ = pixel_format;

    fd_ = open(device_path_.c_str(), O_RDWR | O_NONBLOCK, 0);
    if (fd_ < 0) {
        std::cerr << "[V4L2 ERROR] Failed to open device: " << device_path_ 
                  << " (" << std::strerror(errno) << ")\n";
        return false;
    }

    if (!query_capabilities()) return false;
    if (!setup_format()) return false;
    if (!request_and_mmap_buffers(buffer_count)) return false;

    return true;
}

bool V4L2Camera::query_capabilities() {
    struct v4l2_capability cap;
    std::memset(&cap, 0, sizeof(cap));

    if (ioctl(fd_, VIDIOC_QUERYCAP, &cap) < 0) {
        std::cerr << "[V4L2 ERROR] VIDIOC_QUERYCAP failed: " << std::strerror(errno) << "\n";
        return false;
    }

    uint32_t device_caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;

    if (!(device_caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE)) {
        std::cerr << "[V4L2 ERROR] Device does not support V4L2_CAP_VIDEO_CAPTURE_MPLANE!\n";
        return false;
    }

    if (!(device_caps & V4L2_CAP_STREAMING)) {
        std::cerr << "[V4L2 ERROR] Device does not support V4L2_CAP_STREAMING (mmap/dma-buf)!\n";
        return false;
    }

    return true;
}

bool V4L2Camera::setup_format() {
    struct v4l2_format fmt;
    std::memset(&fmt, 0, sizeof(fmt));

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.width = width_;
    fmt.fmt.pix_mp.height = height_;
    fmt.fmt.pix_mp.pixelformat = pixel_format_;
    fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
    fmt.fmt.pix_mp.num_planes = 1;

    if (ioctl(fd_, VIDIOC_S_FMT, &fmt) < 0) {
        std::cerr << "[V4L2 ERROR] VIDIOC_S_FMT failed: " << std::strerror(errno) << "\n";
        return false;
    }

    // Verify negotiated format
    width_ = fmt.fmt.pix_mp.width;
    height_ = fmt.fmt.pix_mp.height;
    std::cout << "[V4L2 INFO] Negotiated Format: " << width_ << "x" << height_ 
              << ", BytesPerLine: " << fmt.fmt.pix_mp.plane_fmt[0].bytesperline
              << ", SizeImage: " << fmt.fmt.pix_mp.plane_fmt[0].sizeimage << "\n";

    return true;
}

bool V4L2Camera::request_and_mmap_buffers(uint32_t buffer_count) {
    struct v4l2_requestbuffers req;
    std::memset(&req, 0, sizeof(req));
    req.count = buffer_count;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd_, VIDIOC_REQBUFS, &req) < 0) {
        std::cerr << "[V4L2 ERROR] VIDIOC_REQBUFS failed: " << std::strerror(errno) << "\n";
        return false;
    }

    if (req.count < 2) {
        std::cerr << "[V4L2 ERROR] Insufficient buffer memory allocated!\n";
        return false;
    }

    buffers_.resize(req.count);

    for (uint32_t i = 0; i < req.count; ++i) {
        struct v4l2_buffer buf;
        struct v4l2_plane planes[1];
        std::memset(&buf, 0, sizeof(buf));
        std::memset(planes, 0, sizeof(planes));

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.m.planes = planes;
        buf.length = 1;

        if (ioctl(fd_, VIDIOC_QUERYBUF, &buf) < 0) {
            std::cerr << "[V4L2 ERROR] VIDIOC_QUERYBUF index " << i << " failed: " 
                      << std::strerror(errno) << "\n";
            return false;
        }

        void* start = mmap(
            nullptr,
            planes[0].length,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            fd_,
            static_cast<off_t>(planes[0].m.mem_offset)
        );

        if (start == MAP_FAILED) {
            std::cerr << "[V4L2 ERROR] mmap buffer " << i << " failed: " 
                      << std::strerror(errno) << "\n";
            return false;
        }

        buffers_[i].start = start;
        buffers_[i].length = planes[0].length;
        buffers_[i].index = i;
        buffers_[i].dma_fd = -1; // To be populated in Phase 3 if DMA-BUF export is used

        // Queue the initial empty buffer to driver pool
        if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
            std::cerr << "[V4L2 ERROR] Initial VIDIOC_QBUF failed for index " << i 
                      << ": " << std::strerror(errno) << "\n";
            return false;
        }
    }

    std::cout << "[V4L2 INFO] Successfully requested and mapped " << req.count << " buffers.\n";
    return true;
}

void V4L2Camera::unmap_and_release_buffers() {
    for (auto& buf : buffers_) {
        if (buf.start && buf.start != MAP_FAILED) {
            munmap(buf.start, buf.length);
            buf.start = nullptr;
        }
        if (buf.dma_fd >= 0) {
            close(buf.dma_fd);
            buf.dma_fd = -1;
        }
    }
    buffers_.clear();
}

bool V4L2Camera::start_stream() {
    if (is_streaming_) return true;

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(fd_, VIDIOC_STREAMON, &type) < 0) {
        std::cerr << "[V4L2 ERROR] VIDIOC_STREAMON failed: " << std::strerror(errno) << "\n";
        return false;
    }

    is_streaming_ = true;
    std::cout << "[V4L2 INFO] Video streaming started successfully.\n";
    return true;
}

bool V4L2Camera::stop_stream() {
    if (!is_streaming_) return true;

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(fd_, VIDIOC_STREAMOFF, &type) < 0) {
        std::cerr << "[V4L2 ERROR] VIDIOC_STREAMOFF failed: " << std::strerror(errno) << "\n";
        return false;
    }

    is_streaming_ = false;
    std::cout << "[V4L2 INFO] Video streaming stopped.\n";
    return true;
}

bool V4L2Camera::dequeue_frame(FrameBuffer& buffer, int timeout_ms) {
    if (!is_streaming_) return false;

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd_, &fds);

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int r = select(fd_ + 1, &fds, nullptr, nullptr, &tv);
    if (r == -1) {
        if (errno == EINTR) return false;
        std::cerr << "[V4L2 ERROR] select() error: " << std::strerror(errno) << "\n";
        return false;
    }
    if (r == 0) {
        std::cerr << "[V4L2 WARNING] select() timeout waiting for frame (" << timeout_ms << "ms)\n";
        return false;
    }

    struct v4l2_buffer buf;
    struct v4l2_plane planes[1];
    std::memset(&buf, 0, sizeof(buf));
    std::memset(planes, 0, sizeof(planes));

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.m.planes = planes;
    buf.length = 1;

    if (ioctl(fd_, VIDIOC_DQBUF, &buf) < 0) {
        std::cerr << "[V4L2 ERROR] VIDIOC_DQBUF failed: " << std::strerror(errno) << "\n";
        return false;
    }

    buffer = buffers_[buf.index];
    return true;
}

bool V4L2Camera::enqueue_frame(const FrameBuffer& buffer) {
    if (!is_streaming_) return false;

    struct v4l2_buffer buf;
    struct v4l2_plane planes[1];
    std::memset(&buf, 0, sizeof(buf));
    std::memset(planes, 0, sizeof(planes));

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = buffer.index;
    buf.m.planes = planes;
    buf.length = 1;

    if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
        std::cerr << "[V4L2 ERROR] VIDIOC_QBUF index " << buffer.index 
                  << " failed: " << std::strerror(errno) << "\n";
        return false;
    }

    return true;
}

} // namespace edge_camera
