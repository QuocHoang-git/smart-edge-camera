#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <memory>
#include <functional>

// Forward declarations for Rockchip MPP opaque pointers
typedef void* MppCtx;
struct MppApi_t;
typedef struct MppApi_t MppApi;
typedef void* MppEncCfg;
typedef void* MppBuffer;
typedef void* MppFrame;
typedef void* MppPacket;

namespace camera {

struct EncodedPacket {
    std::vector<uint8_t> data;
    bool is_keyframe{false};
    uint64_t pts_us{0};
};

struct MppEncoderConfig {
    uint32_t width{1920};
    uint32_t height{1080};
    uint32_t fps{30};
    uint32_t gop{60};          // Keyframe interval (e.g., 2 seconds at 30 fps)
    uint32_t bps{2000000};     // Target bitrate (bits per second)
    bool is_h265{false};       // false for H.264, true for H.265
};

/**
 * @brief Zero-Copy Hardware Video Encoder utilizing Rockchip MPP (Media Process Platform).
 * Encapsulates VEPU hardware acceleration via /dev/mpp_service.
 */
class MppEncoder {
public:
    explicit MppEncoder(const MppEncoderConfig& config);
    ~MppEncoder();

    // Delete copy semantics for RAII safety
    MppEncoder(const MppEncoder&) = delete;
    MppEncoder& operator=(const MppEncoder&) = delete;

    // Allow move semantics
    MppEncoder(MppEncoder&& other) noexcept;
    MppEncoder& operator=(MppEncoder&& other) noexcept;

    /**
     * @brief Initialize the MPP hardware encoder and commit hardware parameters.
     * @return true on success, false otherwise.
     */
    bool init();

    /**
     * @brief Retrieve SPS/PPS (or VPS/SPS/PPS for H.265) header synchronization packet.
     * Crucial for RTSP SDP and stream initialization.
     */
    std::vector<uint8_t> get_header();

    /**
     * @brief Encode an NV12 frame imported via Zero-Copy DMA file descriptor.
     * @param dma_fd Physical DMA-BUF file descriptor.
     * @param size Size in bytes of the NV12 buffer.
     * @param pts_us Presentation timestamp in microseconds.
     * @param[out] out_packet Container for the resulting NAL units.
     * @return true if encoding succeeded and packet was generated.
     */
    bool encode_dma(int dma_fd, size_t size, uint64_t pts_us, EncodedPacket& out_packet);

    /**
     * @brief Encode an NV12 frame from a virtual memory buffer.
     * @param vir_addr Virtual memory address of NV12 data.
     * @param size Size in bytes.
     * @param pts_us Presentation timestamp in microseconds.
     * @param[out] out_packet Container for the resulting NAL units.
     * @return true on success.
     */
    bool encode_buffer(void* vir_addr, size_t size, uint64_t pts_us, EncodedPacket& out_packet);

    [[nodiscard]] const MppEncoderConfig& get_config() const noexcept { return config_; }

private:
    void deinit();

    MppEncoderConfig config_;
    MppCtx ctx_{nullptr};
    MppApi* mpi_{nullptr};
    MppEncCfg cfg_{nullptr};
    int valloc_fd_{-1};

    bool initialized_{false};
    std::vector<uint8_t> header_cache_;
};

} // namespace camera
