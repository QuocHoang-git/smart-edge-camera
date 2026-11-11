/**
 * @file rknn_engine.hpp
 * @brief Zero-Copy RAII C++ wrapper for Rockchip NPU (RKNN Micro Runtime).
 */

#pragma once

#include "npu/rknn_api.h"

#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

namespace edge_camera {

struct NpuTensorAttr {
    uint32_t width{0};
    uint32_t height{0};
    uint32_t channels{0};
    uint32_t size{0};
    rknn_tensor_format format{RKNN_TENSOR_NHWC};
    rknn_tensor_type type{RKNN_TENSOR_UINT8};
};

class RknnEngine {
public:
    RknnEngine() = default;
    ~RknnEngine();

    RknnEngine(const RknnEngine&) = delete;
    RknnEngine& operator=(const RknnEngine&) = delete;

    RknnEngine(RknnEngine&& other) noexcept;
    RknnEngine& operator=(RknnEngine&& other) noexcept;

    /**
     * @brief Load RKNN model file and initialize NPU context.
     * @param model_path Path to .rknn model file on target filesystem.
     * @return true on success.
     */
    bool init(const std::string& model_path);

    /**
     * @brief Bind external DMA-BUF memory directly to NPU input tensor (Zero-Copy).
     * @param dma_fd DMA-BUF file descriptor of input buffer.
     * @param va Virtual address of input buffer.
     * @param size Size in bytes.
     * @return true on binding success.
     */
    bool bind_input_dma_buffer(int dma_fd, void* va, size_t size);

    /**
     * @brief Run synchronous inference on hardware NPU.
     * @param[out] inference_time_ms Execution time on NPU hardware.
     * @return true on inference success.
     */
    bool run(double& inference_time_ms);

    void destroy();

    [[nodiscard]] const NpuTensorAttr& input_attr() const noexcept { return input_attr_; }
    [[nodiscard]] uint32_t output_count() const noexcept { return io_num_.n_output; }
    [[nodiscard]] bool is_initialized() const noexcept { return ctx_ != 0; }

private:
    bool query_model_attributes();

    rknn_context ctx_{0};
    rknn_input_output_num io_num_{};
    NpuTensorAttr input_attr_{};
    rknn_tensor_attr input_raw_attr_{};
    std::vector<rknn_tensor_attr> output_attrs_{};

    rknn_tensor_mem* input_mem_{nullptr};
    std::vector<rknn_tensor_mem*> output_mems_{};
};

} // namespace edge_camera
