#include "encoder/mpp_encoder.hpp"

#include <iostream>
#include <cstring>
#include <unistd.h>
#include <cerrno>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

// Rockchip MPP Headers
#include <rk_mpi.h>
#include <rk_mpi_cmd.h>
#include <rk_mpi_mb_cmd.h>
#include <rk_venc_cfg.h>
#include <rk_venc_cmd.h>
#include <rk_venc_rc.h>
#include <mpp_buffer.h>
#include <mpp_frame.h>
#include <mpp_packet.h>
#include <mpp_err.h>

namespace camera {

static int g_enc_chan_counter = 0;

MppEncoder::MppEncoder(const MppEncoderConfig& config)
    : config_(config) {
}

MppEncoder::~MppEncoder() {
    deinit();
}

MppEncoder::MppEncoder(MppEncoder&& other) noexcept
    : config_(other.config_),
      ctx_(other.ctx_),
      mpi_(other.mpi_),
      cfg_(other.cfg_),
      valloc_fd_(other.valloc_fd_),
      initialized_(other.initialized_),
      header_cache_(std::move(other.header_cache_)) {
    other.ctx_ = nullptr;
    other.mpi_ = nullptr;
    other.cfg_ = nullptr;
    other.valloc_fd_ = -1;
    other.initialized_ = false;
}

MppEncoder& MppEncoder::operator=(MppEncoder&& other) noexcept {
    if (this != &other) {
        deinit();
        config_ = other.config_;
        ctx_ = other.ctx_;
        mpi_ = other.mpi_;
        cfg_ = other.cfg_;
        valloc_fd_ = other.valloc_fd_;
        initialized_ = other.initialized_;
        header_cache_ = std::move(other.header_cache_);

        other.ctx_ = nullptr;
        other.mpi_ = nullptr;
        other.cfg_ = nullptr;
        other.valloc_fd_ = -1;
        other.initialized_ = false;
    }
    return *this;
}

bool MppEncoder::init() {
    if (initialized_) {
        return true;
    }

    std::cout << "[MppEncoder] Initializing MPP hardware encoder: "
              << config_.width << "x" << config_.height << " @ " << config_.fps << " FPS, "
              << "Bitrate=" << (config_.bps / 1000) << " Kbps, GOP=" << config_.gop << std::endl;

    valloc_fd_ = open("/dev/mpi/valloc", O_RDWR);
    if (valloc_fd_ < 0) {
        std::cerr << "[MppEncoder] Failed to open /dev/mpi/valloc: " << std::strerror(errno) << std::endl;
        return false;
    }

    MPP_RET ret = mpp_create(&ctx_, &mpi_);
    if (ret != MPP_OK || !ctx_ || !mpi_) {
        std::cerr << "[MppEncoder] Failed to create MPP context (ret=" << ret << ")" << std::endl;
        close(valloc_fd_);
        valloc_fd_ = -1;
        return false;
    }

    vcodec_attr attr;
    std::memset(&attr, 0, sizeof(attr));
    attr.type = MPP_CTX_ENC;
    attr.coding = config_.is_h265 ? MPP_VIDEO_CodingHEVC : MPP_VIDEO_CodingAVC;
    attr.chan_id = g_enc_chan_counter++;

    ret = mpp_init_ext(ctx_, &attr);
    if (ret != MPP_OK) {
        std::cerr << "[MppEncoder] Failed to initialize MPP encoder context via mpp_init_ext (ret=" 
                  << ret << ")" << std::endl;
        deinit();
        return false;
    }

    ret = mpp_enc_cfg_init(&cfg_);
    if (ret != MPP_OK || !cfg_) {
        std::cerr << "[MppEncoder] Failed to init MPP enc cfg (ret=" << ret << ")" << std::endl;
        deinit();
        return false;
    }

    // Configure Pre-processing (Frame geometry and pixel format)
    mpp_enc_cfg_set_s32(cfg_, "prep:width", config_.width);
    mpp_enc_cfg_set_s32(cfg_, "prep:height", config_.height);
    mpp_enc_cfg_set_s32(cfg_, "prep:hor_stride", config_.width);
    mpp_enc_cfg_set_s32(cfg_, "prep:ver_stride", config_.height);
    mpp_enc_cfg_set_s32(cfg_, "prep:format", MPP_FMT_YUV420SP); // NV12

    // Configure Rate Control: CBR (Constant Bit Rate) for stable real-time surveillance streaming
    mpp_enc_cfg_set_s32(cfg_, "rc:mode", MPP_ENC_RC_MODE_CBR);
    mpp_enc_cfg_set_s32(cfg_, "rc:bps_target", config_.bps);
    mpp_enc_cfg_set_s32(cfg_, "rc:bps_max", config_.bps * 6 / 5);
    mpp_enc_cfg_set_s32(cfg_, "rc:bps_min", config_.bps * 4 / 5);
    mpp_enc_cfg_set_s32(cfg_, "rc:fps_in_flex", 0);
    mpp_enc_cfg_set_s32(cfg_, "rc:fps_in_num", config_.fps);
    mpp_enc_cfg_set_s32(cfg_, "rc:fps_in_denorm", 1);
    mpp_enc_cfg_set_s32(cfg_, "rc:fps_out_flex", 0);
    mpp_enc_cfg_set_s32(cfg_, "rc:fps_out_num", config_.fps);
    mpp_enc_cfg_set_s32(cfg_, "rc:fps_out_denorm", 1);
    mpp_enc_cfg_set_s32(cfg_, "rc:gop", config_.gop);

    // Codec-specific Profile & Level settings
    if (!config_.is_h265) {
        mpp_enc_cfg_set_s32(cfg_, "codec:type", MPP_VIDEO_CodingAVC);
        mpp_enc_cfg_set_s32(cfg_, "h264:profile", 100); // High Profile
        mpp_enc_cfg_set_s32(cfg_, "h264:level", 41);    // Level 4.1
        mpp_enc_cfg_set_s32(cfg_, "h264:cabac_en", 1);
    } else {
        mpp_enc_cfg_set_s32(cfg_, "codec:type", MPP_VIDEO_CodingHEVC);
        mpp_enc_cfg_set_s32(cfg_, "h265:profile", 1);   // Main Profile
        mpp_enc_cfg_set_s32(cfg_, "h265:level", 120);   // Level 4.0
    }

    ret = mpi_->control(ctx_, MPP_ENC_SET_CFG, cfg_);
    if (ret != MPP_OK) {
        std::cerr << "[MppEncoder] Failed to apply MPP configuration (ret=" << ret << ")" << std::endl;
        deinit();
        return false;
    }

    // Ensure SPS/PPS headers are prepended to EVERY IDR frame for instant RTSP/VLC sync
    MppEncHeaderMode hdr_mode = MPP_ENC_HEADER_MODE_EACH_IDR;
    ret = mpi_->control(ctx_, MPP_ENC_SET_HEADER_MODE, &hdr_mode);
    if (ret != MPP_OK) {
        std::cerr << "[MppEncoder] Warning: Failed to set MPP_ENC_HEADER_MODE_EACH_IDR (ret=" << ret << ")" << std::endl;
    }

    initialized_ = true;
    std::cout << "[MppEncoder " << config_.width << "x" << config_.height 
              << "] MPP encoder ready for zero-copy DMA streaming (chan=" << attr.chan_id << ")." << std::endl;
    return true;
}

std::vector<uint8_t> MppEncoder::get_header() {
    return header_cache_;
}

bool MppEncoder::encode_dma(int dma_fd, size_t size, uint64_t pts_us, EncodedPacket& out_packet) {
    if (!initialized_ || dma_fd < 0) {
        return false;
    }

    int import_fd = dup(dma_fd);
    if (import_fd < 0) {
        std::cerr << "[MppEncoder] dup(dma_fd=" << dma_fd << ") failed: " << std::strerror(errno) << std::endl;
        return false;
    }

    MppBufferInfo buf_info;
    std::memset(&buf_info, 0, sizeof(buf_info));
    buf_info.type = MPP_BUFFER_TYPE_EXT_DMA;
    buf_info.fd = import_fd;
    buf_info.size = size;

    MppBuffer mpp_buf = nullptr;
    MPP_RET ret = mpp_buffer_import(&mpp_buf, &buf_info);
    if (ret != MPP_OK || !mpp_buf) {
        std::cerr << "[MppEncoder " << config_.width << "x" << config_.height 
                  << "] mpp_buffer_import failed: ret=" << ret << std::endl;
        close(import_fd);
        return false;
    }

    // Prepare MppFrame
    MppFrame frame = nullptr;
    ret = mpp_frame_init(&frame);
    if (ret != MPP_OK || !frame) {
        std::cerr << "[MppEncoder] mpp_frame_init failed: ret=" << ret << std::endl;
        mpp_buffer_put(mpp_buf);
        return false;
    }

    mpp_frame_set_width(frame, config_.width);
    mpp_frame_set_height(frame, config_.height);
    mpp_frame_set_hor_stride(frame, config_.width);
    mpp_frame_set_ver_stride(frame, config_.height);
    mpp_frame_set_fmt(frame, MPP_FMT_YUV420SP);
    mpp_frame_set_buffer(frame, mpp_buf);
    mpp_frame_set_pts(frame, static_cast<RK_S64>(pts_us));

    // Submit frame to VEPU encoder
    ret = mpi_->encode_put_frame(ctx_, frame);
    if (ret != MPP_OK) {
        std::cerr << "[MppEncoder] encode_put_frame failed: ret=" << ret << std::endl;
        mpp_frame_deinit(&frame);
        mpp_buffer_put(mpp_buf);
        return false;
    }

    // Dequeue encoded packet using struct venc_packet as required by RV1106 MPP
    struct venc_packet venc_pkt;
    std::memset(&venc_pkt, 0, sizeof(venc_pkt));
    struct venc_packet* p_pkt = &venc_pkt;

    int retry = 0;
    do {
        ret = mpi_->encode_get_packet(ctx_, reinterpret_cast<MppPacket*>(&p_pkt));
        if (ret == MPP_OK && venc_pkt.len > 0) {
            break;
        }
        usleep(500); // 500us
    } while (++retry < 30); // Up to 15ms wait

    // Release input frame and imported DMA buffer after submission
    mpp_frame_deinit(&frame);
    mpp_buffer_put(mpp_buf);

    if (ret != MPP_OK || venc_pkt.len == 0) {
        static uint32_t s_err_count = 0;
        if (s_err_count++ % 60 == 0) {
            std::cerr << "[MppEncoder " << config_.width << "x" << config_.height 
                      << "] encode_get_packet failed: ret=" << ret << ", len=" << venc_pkt.len 
                      << ", retries=" << retry << std::endl;
        }
        return false;
    }

    // Retrieve DMA buffer of the encoded bitstream packet via /dev/mpi/valloc ioctl
    struct valloc_mb mb;
    std::memset(&mb, 0, sizeof(mb));
    mb.mpi_buf_id = static_cast<int>(venc_pkt.u64priv_data);
    mb.struct_size = sizeof(mb);

    int io_ret = ioctl(valloc_fd_, VALLOC_IOCTL_MB_GET_FD, &mb);
    if (io_ret < 0 || mb.dma_buf_fd < 0) {
        std::cerr << "[MppEncoder] VALLOC_IOCTL_MB_GET_FD failed (ret=" << io_ret << ")" << std::endl;
        mpi_->encode_release_packet(ctx_, reinterpret_cast<MppPacket*>(&p_pkt));
        return false;
    }

    void* mapped_ptr = mmap(nullptr, venc_pkt.buf_size, PROT_READ, MAP_SHARED, mb.dma_buf_fd, 0);
    if (mapped_ptr == MAP_FAILED) {
        std::cerr << "[MppEncoder] mmap of encoded packet failed: " << std::strerror(errno) << std::endl;
        close(mb.dma_buf_fd);
        mpi_->encode_release_packet(ctx_, reinterpret_cast<MppPacket*>(&p_pkt));
        return false;
    }

    const uint8_t* base = reinterpret_cast<const uint8_t*>(mapped_ptr);
    size_t offset = venc_pkt.offset;
    size_t len = venc_pkt.len;
    size_t buf_size = venc_pkt.buf_size;

    out_packet.data.clear();
    out_packet.data.reserve(len);

    if (offset + len <= buf_size) {
        // Contiguous linear region in the kernel ring buffer
        out_packet.data.assign(base + offset, base + offset + len);
    } else {
        // Packet wraps around the circular ring buffer boundary
        size_t part1 = buf_size - offset;
        size_t part2 = len - part1;
        out_packet.data.assign(base + offset, base + offset + part1);
        out_packet.data.insert(out_packet.data.end(), base, base + part2);
    }
    out_packet.pts_us = static_cast<uint64_t>(venc_pkt.u64pts);

    // Determine IDR frame safely from the contiguous reassembled packet
    const uint8_t* p = out_packet.data.data();
    bool is_idr = false;
    if (!config_.is_h265) {
        for (size_t i = 0; i + 4 < len; ++i) {
            if (p[i] == 0x00 && p[i+1] == 0x00 && p[i+2] == 0x01) {
                uint8_t nal_type = p[i+3] & 0x1F;
                if (nal_type == 5 || nal_type == 7) {
                    is_idr = true;
                    if (nal_type == 7 && header_cache_.empty()) {
                        header_cache_ = out_packet.data;
                    }
                    break;
                }
            } else if (p[i] == 0x00 && p[i+1] == 0x00 && p[i+2] == 0x00 && p[i+3] == 0x01) {
                uint8_t nal_type = p[i+4] & 0x1F;
                if (nal_type == 5 || nal_type == 7) {
                    is_idr = true;
                    if (nal_type == 7 && header_cache_.empty()) {
                        header_cache_ = out_packet.data;
                    }
                    break;
                }
            }
        }
    } else {
        // H.265 NAL type is (byte >> 1) & 0x3F
        for (size_t i = 0; i + 4 < len; ++i) {
            if (p[i] == 0x00 && p[i+1] == 0x00 && p[i+2] == 0x01) {
                uint8_t nal_type = (p[i+3] >> 1) & 0x3F;
                if (nal_type >= 16 && nal_type <= 21) {
                    is_idr = true;
                    break;
                }
            } else if (p[i] == 0x00 && p[i+1] == 0x00 && p[i+2] == 0x00 && p[i+3] == 0x01) {
                uint8_t nal_type = (p[i+4] >> 1) & 0x3F;
                if (nal_type >= 16 && nal_type <= 21) {
                    is_idr = true;
                    break;
                }
            }
        }
    }
    out_packet.is_keyframe = is_idr;

    munmap(mapped_ptr, venc_pkt.buf_size);
    close(mb.dma_buf_fd);
    mpi_->encode_release_packet(ctx_, reinterpret_cast<MppPacket*>(&p_pkt));

    return true;
}

bool MppEncoder::encode_buffer(void* vir_addr, size_t size, uint64_t pts_us, EncodedPacket& out_packet) {
    if (!initialized_ || !vir_addr) {
        return false;
    }

    MppBufferInfo buf_info;
    std::memset(&buf_info, 0, sizeof(buf_info));
    buf_info.type = MPP_BUFFER_TYPE_NORMAL;
    buf_info.ptr = vir_addr;
    buf_info.size = size;

    MppBuffer mpp_buf = nullptr;
    MPP_RET ret = mpp_buffer_import(&mpp_buf, &buf_info);
    if (ret != MPP_OK || !mpp_buf) {
        return false;
    }

    MppFrame frame = nullptr;
    ret = mpp_frame_init(&frame);
    if (ret != MPP_OK || !frame) {
        mpp_buffer_put(mpp_buf);
        return false;
    }

    mpp_frame_set_width(frame, config_.width);
    mpp_frame_set_height(frame, config_.height);
    mpp_frame_set_hor_stride(frame, config_.width);
    mpp_frame_set_ver_stride(frame, config_.height);
    mpp_frame_set_fmt(frame, MPP_FMT_YUV420SP);
    mpp_frame_set_buffer(frame, mpp_buf);
    mpp_frame_set_pts(frame, static_cast<RK_S64>(pts_us));

    ret = mpi_->encode_put_frame(ctx_, frame);
    if (ret != MPP_OK) {
        mpp_frame_deinit(&frame);
        mpp_buffer_put(mpp_buf);
        return false;
    }

    struct venc_packet venc_pkt;
    std::memset(&venc_pkt, 0, sizeof(venc_pkt));
    struct venc_packet* p_pkt = &venc_pkt;

    int retry = 0;
    do {
        ret = mpi_->encode_get_packet(ctx_, reinterpret_cast<MppPacket*>(&p_pkt));
        if (ret == MPP_OK && venc_pkt.len > 0) break;
        usleep(500);
    } while (++retry < 30);

    mpp_frame_deinit(&frame);
    mpp_buffer_put(mpp_buf);

    if (ret != MPP_OK || venc_pkt.len == 0) {
        return false;
    }

    struct valloc_mb mb;
    std::memset(&mb, 0, sizeof(mb));
    mb.mpi_buf_id = static_cast<int>(venc_pkt.u64priv_data);
    mb.struct_size = sizeof(mb);

    int io_ret = ioctl(valloc_fd_, VALLOC_IOCTL_MB_GET_FD, &mb);
    if (io_ret < 0 || mb.dma_buf_fd < 0) {
        mpi_->encode_release_packet(ctx_, reinterpret_cast<MppPacket*>(&p_pkt));
        return false;
    }

    void* mapped_ptr = mmap(nullptr, venc_pkt.buf_size, PROT_READ, MAP_SHARED, mb.dma_buf_fd, 0);
    if (mapped_ptr == MAP_FAILED) {
        close(mb.dma_buf_fd);
        mpi_->encode_release_packet(ctx_, reinterpret_cast<MppPacket*>(&p_pkt));
        return false;
    }

    const uint8_t* base = reinterpret_cast<const uint8_t*>(mapped_ptr);
    size_t offset = venc_pkt.offset;
    size_t len = venc_pkt.len;
    size_t buf_size = venc_pkt.buf_size;

    out_packet.data.clear();
    out_packet.data.reserve(len);

    if (offset + len <= buf_size) {
        out_packet.data.assign(base + offset, base + offset + len);
    } else {
        size_t part1 = buf_size - offset;
        size_t part2 = len - part1;
        out_packet.data.assign(base + offset, base + offset + part1);
        out_packet.data.insert(out_packet.data.end(), base, base + part2);
    }
    out_packet.pts_us = static_cast<uint64_t>(venc_pkt.u64pts);
    out_packet.is_keyframe = true;

    munmap(mapped_ptr, venc_pkt.buf_size);
    close(mb.dma_buf_fd);
    mpi_->encode_release_packet(ctx_, reinterpret_cast<MppPacket*>(&p_pkt));

    return true;
}

void MppEncoder::deinit() {
    if (valloc_fd_ >= 0) {
        close(valloc_fd_);
        valloc_fd_ = -1;
    }
    if (cfg_) {
        mpp_enc_cfg_deinit(cfg_);
        cfg_ = nullptr;
    }
    if (ctx_) {
        mpp_destroy(ctx_);
        ctx_ = nullptr;
    }
    mpi_ = nullptr;
    initialized_ = false;
    header_cache_.clear();
}

} // namespace camera
