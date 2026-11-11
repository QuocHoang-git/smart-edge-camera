/**
 * @file camera_pipeline.cpp
 * @brief Multi-threaded production camera pipeline with Dual-Stream H.264 Hardware Encoding & RTSP.
 */

#include "pipeline/camera_pipeline.hpp"

#include <iostream>
#include <chrono>
#include <cmath>
#include <algorithm>

namespace edge_camera {

CameraPipeline::CameraPipeline(CameraConfig config)
    : config_(config),
      camera_(config.video_device) {
    init_anchors();
}

void CameraPipeline::init_anchors() {
    anchors_.clear();
    anchors_.reserve(896);

    // Layer 1: Stride 8, 16x16 feature map, 2 anchors per cell -> 512 anchors
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            for (int a = 0; a < 2; ++a) {
                Anchor anc;
                anc.x_center = (x + 0.5f) * 8.0f / 128.0f;
                anc.y_center = (y + 0.5f) * 8.0f / 128.0f;
                anc.w = 1.0f;
                anc.h = 1.0f;
                anchors_.push_back(anc);
            }
        }
    }

    // Layer 2: Stride 16, 8x8 feature map, 6 anchors per cell -> 384 anchors
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            for (int a = 0; a < 6; ++a) {
                Anchor anc;
                anc.x_center = (x + 0.5f) * 16.0f / 128.0f;
                anc.y_center = (y + 0.5f) * 16.0f / 128.0f;
                anc.w = 1.0f;
                anc.h = 1.0f;
                anchors_.push_back(anc);
            }
        }
    }
}

CameraPipeline::~CameraPipeline() {
    stop();
}

bool CameraPipeline::init() {
    std::cout << "\n=======================================================\n";
    std::cout << "  DUAL-STREAM HARDWARE VISION PIPELINE INITIALIZATION\n";
    std::cout << "=======================================================\n";

    // 1. Initialize NPU Model and allocate AI DMA buffer
    std::cout << "[INIT 1/6] Loading RKNN Neural Network: " << config_.model_path << "...\n";
    if (!npu_.init(config_.model_path)) {
        std::cerr << "[PIPELINE ERROR] RKNN Engine init failed!\n";
        return false;
    }

    const auto& npu_input = npu_.input_attr();
    std::cout << "[INIT 2/6] Allocating CMA DMA-BUFs for Vision Pipeline...\n";

    // AI input buffer (128x128 Gray8)
    if (!ai_dma_buffer_.allocate(npu_input.size)) {
        std::cerr << "[PIPELINE ERROR] Failed to allocate AI DMA buffer!\n";
        return false;
    }

    if (!npu_.bind_input_dma_buffer(ai_dma_buffer_.fd(), ai_dma_buffer_.data(), npu_input.size)) {
        std::cerr << "[PIPELINE ERROR] Binding DMA buffer to NPU failed!\n";
        return false;
    }

    // Sub-Stream buffer (640x360 NV12 = 640 * 360 * 1.5 bytes)
    size_t sub_stream_bytes = config_.sub_width * config_.sub_height * 3 / 2;
    if (!sub_stream_dma_.allocate(sub_stream_bytes)) {
        std::cerr << "[PIPELINE ERROR] Failed to allocate Sub-Stream DMA buffer!\n";
        return false;
    }
    std::cout << "[INIT] Sub-Stream DMA buffer allocated: " << sub_stream_bytes 
              << " bytes (fd=" << sub_stream_dma_.fd() << ")\n";

    // 2. Initialize RGA 2D Blitter
    std::cout << "[INIT 3/6] Initializing Rockchip RGA 2D Hardware Blitter...\n";
    if (!rga_.init()) {
        std::cerr << "[PIPELINE ERROR] RGA initialization failed!\n";
        return false;
    }

    // 3. Initialize V4L2 Camera (1080p NV12)
    std::cout << "[INIT 4/6] Initializing V4L2 Camera (" 
              << config_.main_width << "x" << config_.main_height << " NV12)...\n";
    if (!camera_.init(config_.main_width, config_.main_height, V4L2_PIX_FMT_NV12, config_.buffer_count)) {
        std::cerr << "[PIPELINE ERROR] V4L2 Camera initialization failed!\n";
        return false;
    }

    // 4. Initialize Dual Rockchip MPP Hardware Encoders
    std::cout << "[INIT 5/6] Initializing Dual Hardware H.264 Encoders...\n";

    // Main Stream Encoder (1080p Clean)
    camera::MppEncoderConfig main_enc_cfg;
    main_enc_cfg.width = config_.main_width;
    main_enc_cfg.height = config_.main_height;
    main_enc_cfg.fps = config_.fps;
    main_enc_cfg.gop = config_.fps * 2;
    main_enc_cfg.bps = config_.main_bitrate;
    main_enc_cfg.is_h265 = false;

    mpp_main_ = std::make_unique<camera::MppEncoder>(main_enc_cfg);
    if (!mpp_main_->init()) {
        std::cerr << "[PIPELINE ERROR] Failed to initialize Main Stream MPP encoder!\n";
        return false;
    }

    // Sub Stream Encoder (640x360 with AI OSD)
    camera::MppEncoderConfig sub_enc_cfg;
    sub_enc_cfg.width = config_.sub_width;
    sub_enc_cfg.height = config_.sub_height;
    sub_enc_cfg.fps = config_.fps;
    sub_enc_cfg.gop = config_.fps * 2;
    sub_enc_cfg.bps = config_.sub_bitrate;
    sub_enc_cfg.is_h265 = false;

    mpp_sub_ = std::make_unique<camera::MppEncoder>(sub_enc_cfg);
    if (!mpp_sub_->init()) {
        std::cerr << "[PIPELINE ERROR] Failed to initialize Sub Stream MPP encoder!\n";
        return false;
    }

    // 5. Initialize Embedded RTSP Server
    std::cout << "[INIT 6/6] Initializing RTSP Server on port " << config_.rtsp_port << "...\n";
    rtsp_server_ = std::make_unique<camera::RtspServer>(config_.rtsp_port);
    rtsp_server_->add_stream("/live/main", config_.main_width, config_.main_height, config_.fps);
    rtsp_server_->add_stream("/live/sub", config_.sub_width, config_.sub_height, config_.fps);

    if (!rtsp_server_->start()) {
        std::cerr << "[PIPELINE ERROR] Failed to start RTSP server!\n";
        return false;
    }

    std::cout << "=======================================================\n";
    std::cout << "  VISION PIPELINE READY: 1080p Main + 640x360 Sub RTSP\n";
    std::cout << "  Main Stream URL: rtsp://<IP>:" << config_.rtsp_port << "/live/main\n";
    std::cout << "  Sub Stream URL:  rtsp://<IP>:" << config_.rtsp_port << "/live/sub\n";
    std::cout << "=======================================================\n\n";
    return true;
}

bool CameraPipeline::start() {
    if (is_running_) return true;

    if (!camera_.start_stream()) {
        std::cerr << "[PIPELINE ERROR] Failed to start camera stream!\n";
        return false;
    }

    is_running_ = true;

    // Launch worker threads
    ai_thread_ = std::thread(&CameraPipeline::ai_worker_loop, this);
    capture_thread_ = std::thread(&CameraPipeline::capture_and_stream_loop, this);

    std::cout << "[PIPELINE INFO] All threads active: V4L2 Ingestion -> Dual MPP -> RTSP Streamer.\n";
    return true;
}

void CameraPipeline::stop() {
    if (!is_running_) return;

    std::cout << "[PIPELINE INFO] Stopping camera pipeline...\n";
    is_running_ = false;

    ai_trigger_queue_.stop();

    if (capture_thread_.joinable()) capture_thread_.join();
    if (ai_thread_.joinable()) ai_thread_.join();

    if (rtsp_server_) {
        rtsp_server_->stop();
    }

    camera_.stop_stream();
    std::cout << "[PIPELINE INFO] Camera pipeline stopped cleanly.\n";
}

void CameraPipeline::wait_until_stopped() {
    if (capture_thread_.joinable()) capture_thread_.join();
    if (ai_thread_.joinable()) ai_thread_.join();
}

void CameraPipeline::capture_and_stream_loop() {
    try {
        const auto& npu_input = npu_.input_attr();
        uint64_t frame_index = 0;
        auto last_fps_tp = std::chrono::steady_clock::now();
        uint32_t fps_counter = 0;

        while (is_running_) {
            FrameBuffer main_frame;
            if (!camera_.dequeue_frame(main_frame, 1000)) {
                continue;
            }

            uint64_t pts_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count();

            // 1. RGA Hardware Blit #1: Downscale 1080p -> 640x360 NV12 into Sub-Stream buffer
            bool blit_sub_ok = rga_.blit_dma_to_dma(
                main_frame.dma_fd, main_frame.length, config_.main_width, config_.main_height,
                sub_stream_dma_.fd(), sub_stream_dma_.size(), config_.sub_width, config_.sub_height,
                RK_FORMAT_YCbCr_420_SP
            );

            // 2. Hardware MPP Main Stream Encode (1080p Clean, Evidence Quality)
            camera::EncodedPacket main_pkt;
            bool main_encoded = mpp_main_->encode_dma(
                main_frame.dma_fd, main_frame.length, pts_us, main_pkt
            );

            if (main_encoded && rtsp_server_) {
                rtsp_server_->push_frame("/live/main", main_pkt.data.data(), main_pkt.data.size(), main_pkt.is_keyframe);
            }

            // 3. Immediately return V4L2 1080p buffer back to driver pool
            camera_.enqueue_frame(main_frame);

            // 4. Sub-Stream Vision Processing
            if (blit_sub_ok) {
                // RGA Hardware Blit #2: Downscale 640x360 NV12 -> 128x128 Gray8 for NPU
                rga_.blit_dma_to_dma(
                    sub_stream_dma_.fd(), sub_stream_dma_.size(), config_.sub_width, config_.sub_height,
                    ai_dma_buffer_.fd(), ai_dma_buffer_.size(), npu_input.width, npu_input.height,
                    RK_FORMAT_YCbCr_400
                );

                // Signal AI worker to process 128x128 frame non-blockingly
                ai_trigger_queue_.push(frame_index);

                // OSD Burn-In: Draw Face Detection Bounding Boxes onto Sub Stream NV12
                {
                    std::lock_guard<std::mutex> lock(detections_mutex_);
                    for (const auto& box : active_boxes_) {
                        int bx = static_cast<int>(box.xmin * config_.sub_width);
                        int by = static_cast<int>(box.ymin * config_.sub_height);
                        int bw = static_cast<int>((box.xmax - box.xmin) * config_.sub_width);
                        int bh = static_cast<int>((box.ymax - box.ymin) * config_.sub_height);

                        // Clamp coordinates to safe frame boundaries
                        if (bx < 0) bx = 0;
                        if (by < 0) by = 0;
                        if (bx + bw > static_cast<int>(config_.sub_width)) {
                            bw = static_cast<int>(config_.sub_width) - bx;
                        }
                        if (by + bh > static_cast<int>(config_.sub_height)) {
                            bh = static_cast<int>(config_.sub_height) - by;
                        }

                        if (bw > 6 && bh > 6) {
                            rga_.draw_rect_dma(
                                sub_stream_dma_.fd(), sub_stream_dma_.size(),
                                config_.sub_width, config_.sub_height,
                                bx, by, bw, bh, 2, 0x0000FF00
                            );
                        }
                    }
                }

                // Hardware MPP Sub Stream Encode (640x360 with AI Bounding Box)
                camera::EncodedPacket sub_pkt;
                bool sub_encoded = mpp_sub_->encode_dma(
                    sub_stream_dma_.fd(), sub_stream_dma_.size(), pts_us, sub_pkt
                );

                if (sub_encoded && rtsp_server_) {
                    rtsp_server_->push_frame("/live/sub", sub_pkt.data.data(), sub_pkt.data.size(), sub_pkt.is_keyframe);
                }
            }

            frame_index++;
            fps_counter++;

            auto now = std::chrono::steady_clock::now();
            double elapsed_sec = std::chrono::duration<double>(now - last_fps_tp).count();
            if (elapsed_sec >= 2.0) {
                double current_fps = static_cast<double>(fps_counter) / elapsed_sec;
                std::cout << "[STREAMER] Ingestion: " << current_fps << " FPS"
                          << " | Frame: #" << frame_index 
                          << " | AI Drops: " << ai_trigger_queue_.drop_count() << std::endl;
                fps_counter = 0;
                last_fps_tp = now;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[PIPELINE EXCEPTION] In capture_and_stream_loop: " << e.what() << std::endl;
    }
}

void CameraPipeline::ai_worker_loop() {
    try {
        uint64_t processed_frames = 0;
        auto last_ai_tp = std::chrono::steady_clock::now();
        uint32_t ai_counter = 0;

        while (is_running_) {
            uint64_t frame_seq = 0;
            if (!ai_trigger_queue_.pop(frame_seq, 100)) {
                continue;
            }

            double inference_ms = 0.0;
            bool ok = npu_.run(inference_ms);

            if (ok) {
                processed_frames++;
                ai_counter++;

                std::vector<DetectionBox> detected_boxes;
                detect_faces(detected_boxes);

                {
                    std::lock_guard<std::mutex> lock(detections_mutex_);
                    active_boxes_ = std::move(detected_boxes);
                }

                auto now = std::chrono::steady_clock::now();
                double elapsed_sec = std::chrono::duration<double>(now - last_ai_tp).count();
                if (elapsed_sec >= 2.0) {
                    double current_ai_fps = static_cast<double>(ai_counter) / elapsed_sec;
                    std::cout << "  [AI NPU] BlazeFace Tracking: " << current_ai_fps << " FPS"
                              << " | Latency: " << inference_ms << " ms"
                              << " | Active Boxes: " << active_boxes_.size() << std::endl;
                    ai_counter = 0;
                    last_ai_tp = now;
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[PIPELINE EXCEPTION] In ai_worker_loop: " << e.what() << "\n";
    }
}

void CameraPipeline::detect_faces(std::vector<DetectionBox>& boxes) {
    boxes.clear();

    if (npu_.output_count() < 2 || anchors_.size() != 896) return;

    const void* loc_ptr = npu_.output_data(0);
    const rknn_tensor_attr* loc_attr = npu_.output_attr(0);
    const void* score_ptr = npu_.output_data(1);
    const rknn_tensor_attr* score_attr = npu_.output_attr(1);

    if (!loc_ptr || !loc_attr || !score_ptr || !score_attr) return;

    const int8_t* loc_data = reinterpret_cast<const int8_t*>(loc_ptr);
    const int8_t* score_data = reinterpret_cast<const int8_t*>(score_ptr);

    float loc_scale = loc_attr->scale;
    int32_t loc_zp = loc_attr->zp;
    float score_scale = score_attr->scale;
    int32_t score_zp = score_attr->zp;

    constexpr float SCORE_THRESHOLD = 0.50f;
    constexpr float IOU_THRESHOLD = 0.30f;

    std::vector<DetectionBox> candidates;

    for (size_t i = 0; i < 896; ++i) {
        float logit = (static_cast<float>(score_data[i]) - score_zp) * score_scale;
        float prob = 1.0f / (1.0f + std::exp(-logit));

        if (prob < SCORE_THRESHOLD) continue;

        const auto& anc = anchors_[i];
        size_t loc_idx = i * 14;

        float dx = (static_cast<float>(loc_data[loc_idx + 0]) - loc_zp) * loc_scale;
        float dy = (static_cast<float>(loc_data[loc_idx + 1]) - loc_zp) * loc_scale;
        float dw = (static_cast<float>(loc_data[loc_idx + 2]) - loc_zp) * loc_scale;
        float dh = (static_cast<float>(loc_data[loc_idx + 3]) - loc_zp) * loc_scale;

        // BlazeFace center/scale regression mapping
        float cx = (dx / 128.0f) * anc.w + anc.x_center;
        float cy = (dy / 128.0f) * anc.h + anc.y_center;
        float w  = (dw / 128.0f) * anc.w;
        float h  = (dh / 128.0f) * anc.h;

        DetectionBox box;
        box.score = prob;
        box.xmin = std::max(0.0f, std::min(1.0f, cx - w * 0.5f));
        box.ymin = std::max(0.0f, std::min(1.0f, cy - h * 0.5f));
        box.xmax = std::max(0.0f, std::min(1.0f, cx + w * 0.5f));
        box.ymax = std::max(0.0f, std::min(1.0f, cy + h * 0.5f));

        if (box.xmax > box.xmin && box.ymax > box.ymin) {
            candidates.push_back(box);
        }
    }

    if (candidates.empty()) return;

    // Sort by confidence descending
    std::sort(candidates.begin(), candidates.end(), [](const DetectionBox& a, const DetectionBox& b) {
        return a.score > b.score;
    });

    // Non-Maximum Suppression (NMS)
    for (const auto& cand : candidates) {
        bool keep = true;
        for (const auto& chosen : boxes) {
            float x1 = std::max(cand.xmin, chosen.xmin);
            float y1 = std::max(cand.ymin, chosen.ymin);
            float x2 = std::min(cand.xmax, chosen.xmax);
            float y2 = std::min(cand.ymax, chosen.ymax);

            float inter_w = std::max(0.0f, x2 - x1);
            float inter_h = std::max(0.0f, y2 - y1);
            float inter_area = inter_w * inter_h;

            float area_cand = (cand.xmax - cand.xmin) * (cand.ymax - cand.ymin);
            float area_chosen = (chosen.xmax - chosen.xmin) * (chosen.ymax - chosen.ymin);
            float union_area = area_cand + area_chosen - inter_area;

            if (union_area > 0.0f && (inter_area / union_area) >= IOU_THRESHOLD) {
                keep = false;
                break;
            }
        }
        if (keep) {
            boxes.push_back(cand);
            if (boxes.size() >= 4) break; // Keep at most top 4 detected faces
        }
    }
}

} // namespace edge_camera
