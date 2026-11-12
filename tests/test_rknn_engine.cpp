/**
 * @file test_rknn_engine.cpp
 * @brief Integration tests for Rockchip NPU RKNN Runtime and Model Inference.
 */

#include "npu/rknn_engine.hpp"
#include "utils/dma_allocator.hpp"
#include "test_framework.hpp"

#include <cstring>
#include <unistd.h>

TEST_CASE(rknn_model_loading_and_attributes) {
    const char* model_path = "/userdata/retinaface.rknn";
    if (access(model_path, R_OK) != 0) {
        model_path = "models/retinaface.rknn";
        if (access(model_path, R_OK) != 0) {
            std::cout << " [SKIP: Model file not found at /userdata/retinaface.rknn] ";
            return;
        }
    }

    edge_camera::RknnEngine npu;
    ASSERT_TRUE(npu.init(model_path));

    const auto& in_attr = npu.input_attr();
    ASSERT_EQ(in_attr.width, 640);
    ASSERT_EQ(in_attr.height, 640);
    ASSERT_EQ(in_attr.channels, 3);
    ASSERT_GE(in_attr.size, 640 * 640 * 3);

    ASSERT_EQ(npu.output_count(), 3);
}

TEST_CASE(rknn_inference_and_latency) {
    const char* model_path = "/userdata/retinaface.rknn";
    if (access(model_path, R_OK) != 0) {
        model_path = "models/retinaface.rknn";
        if (access(model_path, R_OK) != 0) {
            std::cout << " [SKIP: Model file not found at /userdata/retinaface.rknn] ";
            return;
        }
    }

    edge_camera::RknnEngine npu;
    ASSERT_TRUE(npu.init(model_path));

    const auto& in_attr = npu.input_attr();
    edge_camera::DmaBuffer dma;
    ASSERT_TRUE(dma.allocate(in_attr.size));
    std::memset(dma.data(), 0, in_attr.size);

    ASSERT_TRUE(npu.bind_input_dma_buffer(dma.fd(), dma.data(), in_attr.size));

    double latency_ms = 0.0;
    bool ok = npu.run(latency_ms);
    ASSERT_TRUE(ok);
    ASSERT_GE(latency_ms, 5.0);
    ASSERT_LE(latency_ms, 60.0); // Normal RetinaFace latency is ~30ms on RV1103

    // Verify output pointers are valid
    for (uint32_t i = 0; i < npu.output_count(); ++i) {
        ASSERT_NE(npu.output_data(i), nullptr);
        ASSERT_NE(npu.output_attr(i), nullptr);
    }
}

int main() {
    return test_runner::run_all_tests("Rockchip NPU RKNN Hardware Tests");
}
