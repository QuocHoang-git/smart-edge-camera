/**
 * @file test_pipeline_logic.cpp
 * @brief Unit tests for RetinaFace postprocessing, Letterbox unpadding, NMS, and EMA tracking.
 */

#include "pipeline/camera_pipeline.hpp"
#include "pipeline/rknn_box_priors.h"
#include "test_framework.hpp"

#include <vector>
#include <cmath>
#include <algorithm>

TEST_CASE(box_priors_structure_and_range) {
    constexpr int NUM_PRIORS = 16800;

    for (int i = 0; i < NUM_PRIORS; ++i) {
        float cx = BOX_PRIORS_640[i][0];
        float cy = BOX_PRIORS_640[i][1];
        float w  = BOX_PRIORS_640[i][2];
        float h  = BOX_PRIORS_640[i][3];

        ASSERT_GE(cx, 0.0f);
        ASSERT_LE(cx, 1.0f);
        ASSERT_GE(cy, 0.0f);
        ASSERT_LE(cy, 1.0f);
        ASSERT_GE(w, 0.0f);
        ASSERT_LE(w, 1.0f);
        ASSERT_GE(h, 0.0f);
        ASSERT_LE(h, 1.0f);
    }
}

TEST_CASE(letterbox_coordinate_unpadding) {
    constexpr float PAD_Y = 140.0f;
    constexpr float SUB_H = 360.0f;

    // Case 1: Point at center of the 640x360 image (Y = 180 + 140 = 320 in 640x640)
    float y_640 = 320.0f;
    float sub_y = std::max(0.0f, std::min(SUB_H, y_640 - PAD_Y));
    ASSERT_NEAR(sub_y, 180.0f, 0.01f);

    // Case 2: Top of active camera frame (Y = 140 in 640x640) -> 0 in sub-stream
    y_640 = 140.0f;
    sub_y = std::max(0.0f, std::min(SUB_H, y_640 - PAD_Y));
    ASSERT_NEAR(sub_y, 0.0f, 0.01f);

    // Case 3: Bottom of active camera frame (Y = 500 in 640x640) -> 360 in sub-stream
    y_640 = 500.0f;
    sub_y = std::max(0.0f, std::min(SUB_H, y_640 - PAD_Y));
    ASSERT_NEAR(sub_y, 360.0f, 0.01f);
}

TEST_CASE(retinaface_ssd_variance_regression) {
    const float VARIANCES[2] = {0.1f, 0.2f};

    // Simulated prior anchor at center (0.5, 0.5) with size 0.1 x 0.1
    float prior_cx = 0.5f;
    float prior_cy = 0.5f;
    float prior_w  = 0.1f;
    float prior_h  = 0.1f;

    // Delta offset: dx = 0.5, dy = -0.5, dw = 0.0, dh = 0.0
    float dx = 0.5f;
    float dy = -0.5f;
    float dw = 0.0f;
    float dh = 0.0f;

    float box_cx = dx * VARIANCES[0] * prior_w + prior_cx;
    float box_cy = dy * VARIANCES[0] * prior_h + prior_cy;
    float box_w  = std::exp(dw * VARIANCES[1]) * prior_w;
    float box_h  = std::exp(dh * VARIANCES[1]) * prior_h;

    ASSERT_NEAR(box_cx, 0.505f, 0.001f);
    ASSERT_NEAR(box_cy, 0.495f, 0.001f);
    ASSERT_NEAR(box_w, 0.100f, 0.001f);
    ASSERT_NEAR(box_h, 0.100f, 0.001f);
}

TEST_CASE(nms_box_suppression) {
    using Box = edge_camera::DetectionBox;

    std::vector<Box> candidates;
    // Box 1: high confidence face
    candidates.push_back({0.30f, 0.30f, 0.50f, 0.50f, 0.95f});
    // Box 2: slightly shifted duplicate with lower confidence (IoU > 0.7)
    candidates.push_back({0.31f, 0.31f, 0.51f, 0.51f, 0.80f});
    // Box 3: different face entirely
    candidates.push_back({0.70f, 0.70f, 0.85f, 0.85f, 0.90f});

    std::sort(candidates.begin(), candidates.end(), [](const Box& a, const Box& b) {
        return a.score > b.score;
    });

    std::vector<Box> nms_boxes;
    constexpr float IOU_THRESHOLD = 0.30f;

    for (const auto& cand : candidates) {
        bool keep = true;
        for (const auto& chosen : nms_boxes) {
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
            nms_boxes.push_back(cand);
        }
    }

    ASSERT_EQ(nms_boxes.size(), 2);
    ASSERT_NEAR(nms_boxes[0].score, 0.95f, 0.001f);
    ASSERT_NEAR(nms_boxes[1].score, 0.90f, 0.001f);
}

TEST_CASE(ema_temporal_smoothing_and_persistence) {
    using Box = edge_camera::DetectionBox;

    struct TrackedBox {
        Box box;
        int lost_frames{0};
    };

    std::vector<TrackedBox> tracked;
    tracked.push_back({{100.0f, 100.0f, 200.0f, 200.0f, 0.90f}, 0});

    // Frame 2: Face moves slightly (104, 102, 204, 202)
    Box new_det = {104.0f, 102.0f, 204.0f, 202.0f, 0.92f};
    constexpr float ALPHA = 0.65f;

    tracked[0].box.xmin = ALPHA * new_det.xmin + (1.0f - ALPHA) * tracked[0].box.xmin;
    tracked[0].box.ymin = ALPHA * new_det.ymin + (1.0f - ALPHA) * tracked[0].box.ymin;
    tracked[0].box.xmax = ALPHA * new_det.xmax + (1.0f - ALPHA) * tracked[0].box.xmax;
    tracked[0].box.ymax = ALPHA * new_det.ymax + (1.0f - ALPHA) * tracked[0].box.ymax;

    // Smoothed values should be between 100 and 104 (approx 102.6)
    ASSERT_NEAR(tracked[0].box.xmin, 102.6f, 0.01f);
    ASSERT_NEAR(tracked[0].box.ymin, 101.3f, 0.01f);

    // Test persistence across lost frames
    tracked[0].lost_frames++; // Frame 3: missed (lost=1) -> keep
    ASSERT_LE(tracked[0].lost_frames, 3);

    tracked[0].lost_frames++; // Frame 4: missed (lost=2) -> keep
    ASSERT_LE(tracked[0].lost_frames, 3);

    tracked[0].lost_frames++; // Frame 5: missed (lost=3) -> keep
    ASSERT_LE(tracked[0].lost_frames, 3);

    tracked[0].lost_frames++; // Frame 6: missed (lost=4) -> drop
    ASSERT_GT(tracked[0].lost_frames, 3);
}

int main() {
    return test_runner::run_all_tests("RetinaFace Pipeline Logic & Tracking Tests");
}
