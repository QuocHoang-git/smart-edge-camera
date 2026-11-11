/**
 * @file rknn_engine.cpp
 * @brief Implementation of Rockchip NPU runtime wrapper.
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
      output_attrs_(std::move(other.output_attrs_)) {
    other.ctx_ = 0;
}

RknnEngine& RknnEngine::operator=(RknnEngine&& other) noexcept {
    if (this != &other) {
        destroy();
        ctx_ = other.ctx_;
        io_num_ = other.io_num_;
        input_attr_ = other.input_attr_;
        output_attrs_ = std::move(other.output_attrs_);

        other.ctx_ = 0;
    }
    return *this;
}

void RknnEngine::destroy() {
    if (ctx_ != 0) {
        rknn_destroy(ctx_);
        ctx_ = 0;
    }
}

bool RknnEngine::init(const std::string& model_path) {
    destroy();

    // Read model binary from storage
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

    // Initialize RKNN runtime context
    int ret = rknn_init(&ctx_, model_data.data(), static_cast<uint32_t>(file_size), 0, nullptr);
    if (ret < 0) {
        std::cerr << "[RKNN ERROR] rknn_init failed with code: " << ret << "\n";
        return false;
    }

    if (!query_model_attributes()) {
        destroy();
        return false;
    }

    std::cout << "[RKNN INFO] RKNN context initialized on NPU successfully.\n";
    return true;
}

bool RknnEngine::query_model_attributes() {
    // Query I/O Counts
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

    // Query Input Attributes
    rknn_tensor_attr in_attr;
    std::memset(&in_attr, 0, sizeof(in_attr));
    in_attr.index = 0;
    ret = rknn_query(ctx_, RKNN_QUERY_INPUT_ATTR, &in_attr, sizeof(in_attr));
    if (ret < 0) {
        std::cerr << "[RKNN ERROR] Query input attr failed: " << ret << "\n";
        return false;
    }

    input_attr_.width = in_attr.dims[2];
    input_attr_.height = in_attr.dims[1];
    input_attr_.channels = in_attr.dims[3];
    input_attr_.size = in_attr.size;
    input_attr_.format = in_attr.fmt;
    input_attr_.type = in_attr.type;

    std::cout << "[RKNN INFO] Input Tensor [0]: " 
              << in_attr.name << " (" 
              << input_attr_.width << "x" << input_attr_.height << "x" << input_attr_.channels 
              << ", size=" << input_attr_.size << " bytes)\n";

    // Query Output Attributes
    output_attrs_.resize(io_num_.n_output);
    for (uint32_t i = 0; i < io_num_.n_output; ++i) {
        std::memset(&output_attrs_[i], 0, sizeof(rknn_tensor_attr));
        output_attrs_[i].index = i;
        ret = rknn_query(ctx_, RKNN_QUERY_OUTPUT_ATTR, &output_attrs_[i], sizeof(rknn_tensor_attr));
        if (ret == 0) {
            std::cout << "  -> Output Tensor [" << i << "]: " 
                      << output_attrs_[i].name << " (" 
                      << output_attrs_[i].n_dims << " dims, size=" 
                      << output_attrs_[i].size << " bytes)\n";
        }
    }

    return true;
}

bool RknnEngine::run_inference(const void* input_data, size_t input_size, double& inference_time_ms) {
    if (ctx_ == 0 || !input_data) {
        return false;
    }

    rknn_input input;
    std::memset(&input, 0, sizeof(input));
    input.index = 0;
    input.type = input_attr_.type;
    input.size = static_cast<uint32_t>(input_size);
    input.fmt = input_attr_.format;
    input.buf = const_cast<void*>(input_data);

    int ret = rknn_inputs_set(ctx_, 1, &input);
    if (ret < 0) {
        std::cerr << "[RKNN ERROR] rknn_inputs_set failed: " << ret << "\n";
        return false;
    }

    auto t0 = std::chrono::steady_clock::now();

    ret = rknn_run(ctx_, nullptr);

    auto t1 = std::chrono::steady_clock::now();
    inference_time_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    if (ret < 0) {
        std::cerr << "[RKNN ERROR] rknn_run failed: " << ret << "\n";
        return false;
    }

    return true;
}

} // namespace edge_camera
