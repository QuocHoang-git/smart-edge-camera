/**
 * @file rknn_engine.cpp
 * @brief Implementation of Zero-Copy Rockchip NPU runtime wrapper.
 */

#include "npu/rknn_engine.hpp"

#include <iostream>
#include <fstream>
#include <cstring>
#include <chrono>

namespace edge_camera {

RknnEngine::~RknnEngine() {
    destroy();
}

RknnEngine::RknnEngine(RknnEngine&& other) noexcept
    : ctx_(other.ctx_),
      io_num_(other.io_num_),
      input_attr_(other.input_attr_),
      input_raw_attr_(other.input_raw_attr_),
      output_attrs_(std::move(other.output_attrs_)),
      input_mem_(other.input_mem_),
      output_mems_(std::move(other.output_mems_)) {
    other.ctx_ = 0;
    other.input_mem_ = nullptr;
}

RknnEngine& RknnEngine::operator=(RknnEngine&& other) noexcept {
    if (this != &other) {
        destroy();
        ctx_ = other.ctx_;
        io_num_ = other.io_num_;
        input_attr_ = other.input_attr_;
        input_raw_attr_ = other.input_raw_attr_;
        output_attrs_ = std::move(other.output_attrs_);
        input_mem_ = other.input_mem_;
        output_mems_ = std::move(other.output_mems_);

        other.ctx_ = 0;
        other.input_mem_ = nullptr;
    }
    return *this;
}

void RknnEngine::destroy() {
    if (ctx_ != 0) {
        if (input_mem_) {
            rknn_destroy_mem(ctx_, input_mem_);
            input_mem_ = nullptr;
        }
        for (auto* out_mem : output_mems_) {
            if (out_mem) {
                rknn_destroy_mem(ctx_, out_mem);
            }
        }
        output_mems_.clear();

        rknn_destroy(ctx_);
        ctx_ = 0;
    }
}

bool RknnEngine::init(const std::string& model_path) {
    destroy();

    std::ifstream file(model_path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "[RKNN ERROR] Failed to open model file: " << model_path << "\n";
        return false;
    }

    std::streamsize file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<char> model_data(static_cast<size_t>(file_size));
    if (!file.read(model_data.data(), file_size)) {
        std::cerr << "[RKNN ERROR] Failed to read model data: " << model_path << "\n";
        return false;
    }
    file.close();

    std::cout << "[RKNN INFO] Loaded model binary: " << file_size << " bytes.\n";

    int ret = rknn_init(&ctx_, model_data.data(), static_cast<uint32_t>(file_size), 0, nullptr);
    if (ret < 0) {
        std::cerr << "[RKNN ERROR] rknn_init failed with code: " << ret << "\n";
        return false;
    }

    if (!query_model_attributes()) {
        destroy();
        return false;
    }

    // Allocate and attach memory for all output tensors
    output_mems_.resize(io_num_.n_output, nullptr);
    for (uint32_t i = 0; i < io_num_.n_output; ++i) {
        uint32_t alloc_size = output_attrs_[i].size_with_stride > 0 ? output_attrs_[i].size_with_stride : output_attrs_[i].size;

        output_mems_[i] = rknn_create_mem(ctx_, alloc_size);
        if (!output_mems_[i]) {
            std::cerr << "[RKNN ERROR] Failed to create mem for output tensor " << i << "\n";
            destroy();
            return false;
        }

        ret = rknn_set_io_mem(ctx_, output_mems_[i], &output_attrs_[i]);
        if (ret < 0) {
            std::cerr << "[RKNN ERROR] rknn_set_io_mem failed for output tensor " << i << ": " << ret << "\n";
            destroy();
            return false;
        }
    }

    std::cout << "[RKNN INFO] RKNN context and output tensor memory initialized successfully.\n";
    return true;
}

bool RknnEngine::query_model_attributes() {
    std::memset(&io_num_, 0, sizeof(io_num_));
    int ret = rknn_query(ctx_, RKNN_QUERY_IN_OUT_NUM, &io_num_, sizeof(io_num_));
    if (ret < 0) {
        std::cerr << "[RKNN ERROR] Query IO num failed: " << ret << "\n";
        return false;
    }

    std::cout << "[RKNN INFO] Model IO Topology -> Inputs: " << io_num_.n_input 
              << ", Outputs: " << io_num_.n_output << "\n";

    if (io_num_.n_input < 1) {
        std::cerr << "[RKNN ERROR] Invalid input count!\n";
        return false;
    }

    // Query Input Attributes using native query
    std::memset(&input_raw_attr_, 0, sizeof(input_raw_attr_));
    input_raw_attr_.index = 0;
    ret = rknn_query(ctx_, RKNN_QUERY_NATIVE_INPUT_ATTR, &input_raw_attr_, sizeof(input_raw_attr_));
    if (ret != RKNN_SUCC) {
        ret = rknn_query(ctx_, RKNN_QUERY_INPUT_ATTR, &input_raw_attr_, sizeof(input_raw_attr_));
    }
    if (ret < 0) {
        std::cerr << "[RKNN ERROR] Query input attr failed: " << ret << "\n";
        return false;
    }

    // Default configuration for zero-copy vision pipeline on RV1106:
    // UINT8 input with NHWC format lets the NPU handle normalization and layout internally
    input_raw_attr_.type = RKNN_TENSOR_UINT8;
    input_raw_attr_.fmt = RKNN_TENSOR_NHWC;

    input_attr_.width = input_raw_attr_.dims[2];
    input_attr_.height = input_raw_attr_.dims[1];
    input_attr_.channels = input_raw_attr_.dims[3];
    input_attr_.size = input_raw_attr_.size_with_stride > 0 ? input_raw_attr_.size_with_stride : input_raw_attr_.size;
    input_attr_.format = input_raw_attr_.fmt;
    input_attr_.type = input_raw_attr_.type;

    std::cout << "[RKNN INFO] Input Tensor [0]: " 
              << input_raw_attr_.name << " (" 
              << input_attr_.width << "x" << input_attr_.height << "x" << input_attr_.channels 
              << ", stride_size=" << input_attr_.size << " bytes)\n";

    // Query Output Attributes using native NHWC query
    output_attrs_.resize(io_num_.n_output);
    for (uint32_t i = 0; i < io_num_.n_output; ++i) {
        std::memset(&output_attrs_[i], 0, sizeof(rknn_tensor_attr));
        output_attrs_[i].index = i;
        ret = rknn_query(ctx_, RKNN_QUERY_NATIVE_NHWC_OUTPUT_ATTR, &output_attrs_[i], sizeof(rknn_tensor_attr));
        if (ret != RKNN_SUCC) {
            ret = rknn_query(ctx_, RKNN_QUERY_OUTPUT_ATTR, &output_attrs_[i], sizeof(rknn_tensor_attr));
        }
        if (ret == 0) {
            std::cout << "  -> Output Tensor [" << i << "]: " 
                      << output_attrs_[i].name << " (" 
                      << output_attrs_[i].n_dims << " dims, size=" 
                      << output_attrs_[i].size << ", stride_size=" 
                      << output_attrs_[i].size_with_stride << " bytes)\n";
        }
    }

    return true;
}

bool RknnEngine::bind_input_dma_buffer(int dma_fd, void* va, size_t size) {
    if (ctx_ == 0 || dma_fd < 0 || !va) {
        return false;
    }

    if (input_mem_) {
        rknn_destroy_mem(ctx_, input_mem_);
        input_mem_ = nullptr;
    }

    // Zero-Copy binding: Map DMA-BUF descriptor directly into RKNN NPU input
    input_mem_ = rknn_create_mem_from_fd(
        ctx_,
        dma_fd,
        va,
        static_cast<uint32_t>(size),
        0
    );

    if (!input_mem_) {
        std::cerr << "[RKNN ERROR] rknn_create_mem_from_fd failed for dma_fd=" << dma_fd << "\n";
        return false;
    }

    int ret = rknn_set_io_mem(ctx_, input_mem_, &input_raw_attr_);
    if (ret < 0) {
        std::cerr << "[RKNN ERROR] rknn_set_io_mem for input failed: " << ret << "\n";
        return false;
    }

    std::cout << "[RKNN INFO] Bound input DMA buffer (fd=" << dma_fd << ") to NPU input tensor.\n";
    return true;
}

bool RknnEngine::run(double& inference_time_ms) {
    if (ctx_ == 0) {
        return false;
    }

    auto t0 = std::chrono::steady_clock::now();

    int ret = rknn_run(ctx_, nullptr);

    auto t1 = std::chrono::steady_clock::now();
    inference_time_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    if (ret < 0) {
        std::cerr << "[RKNN ERROR] rknn_run failed: " << ret << "\n";
        return false;
    }

    return true;
}

} // namespace edge_camera
