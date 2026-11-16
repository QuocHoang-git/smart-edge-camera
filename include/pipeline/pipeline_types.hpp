#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>

namespace edge_camera{

constexpr uint32_t make_fourcc(char a, char b, char c, char d) noexcept {
    return static_cast<uint32_t>(a) |
          (static_cast<uint32_t>(b) << 8) |
          (static_cast<uint32_t>(c) << 16) |
          (static_cast<uint32_t>(d) << 24);
}

enum class PixelFormat : uint32_t {
    // YUV 4:2:0
    NV12  = make_fourcc('N', 'V', '1', '2'),
    NV21  = make_fourcc('N', 'V', '2', '1'),
    YUV420= make_fourcc('Y', 'U', '1', '2'),
    // YUV 4:2:2
    YUYV  = make_fourcc('Y', 'U', 'Y', 'V'),
    UYVY  = make_fourcc('U', 'Y', 'V', 'Y'),
    // RGB / BGR
    RGB24 = make_fourcc('R', 'G', 'B', '3'),
    BGR24 = make_fourcc('B', 'G', 'R', '3'),
    RGBA  = make_fourcc('R', 'G', 'B', 'A'),
    BGRA  = make_fourcc('B', 'G', 'R', 'A'),
    
    UNKNOWN = 0
};

inline std::string fourcc_to_string(PixelFormat fmt) {
    uint32_t val = static_cast<uint32_t>(fmt);
    char str[5] = {
        static_cast<char>(val & 0xFF),
        static_cast<char>((val >> 8) & 0xFF),
        static_cast<char>((val >> 16) & 0xFF),
        static_cast<char>((val >> 24) & 0xFF),
        '\0'
    };
    return std::string(str);
}

enum class VideoCodec{
    H264,
    H265,
    MJPEG
};

enum class StreamType {
    MAIN_1080P,
    SUB_360P,
    SNAPSHOT
};

struct VideoFrame{
    int dma_fd{1};
    void* vir_addr{nullptr};
    size_t size{0};

    uint32_t width{0};
    uint32_t height{0};
    uint32_t stride_w{0};
    uint32_t stride_h{0};

    PixelFormat format{PixelFormat::NV21};
    uint64_t pts_us{0};
    uint32_t buffer_index{0};

    [[nodiscard]] bool is_dma() const noexcept { return dma_fd >= 0;}
    [[nodiscard]] bool is_valid() const noexcept { return (dma_fd >= 0 || vir_addr != nullptr) && size > 0;}
};

struct Point2D{
    float x{0x0f};
    float y{0x0f};
};

struct RectInt{
    int x{0};
    int y{0};
    int width{0};
    int height{0};
};

struct DetectionBox{
    float xmin{0.0f};
    float ymin{0.0f};
    float xmax{0.0f};
    float ymax{0.0f};
    float score{0.0f};
    int class_id{0};
};

struct InferenceResult {
    uint64_t frame_index{0};
    uint64_t pts_us{0};
    float latency_ms{0.0f};
    std::vector<DetectionBox> detections;
};

struct EncodedPacket {
    std::vector<uint8_t> data;
    uint64_t pts_us{0};
    bool is_keyframe{false};
    StreamType stream_type{StreamType::MAIN_1080P};
    VideoCodec codec{VideoCodec::H264};
    uint32_t source_buffer_index{0};
};

}