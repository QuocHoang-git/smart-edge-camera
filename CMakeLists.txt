cmake_minimum_required(VERSION 3.16)
project(smart_edge_camera VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(COMMON_COMPILE_OPTIONS
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wnon-virtual-dtor
    -Wcast-align
    -Wunused
    -Woverloaded-virtual
    -pthread
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    list(APPEND COMMON_COMPILE_OPTIONS -g -O0 -DDEBUG)
else()
    list(APPEND COMMON_COMPILE_OPTIONS -O2 -DNDEBUG)
endif()

# SDK Path Definitions
set(LUCKFOX_SDK_PATH "/home/userver/1_Workspace/97_LinuxCustom/luckfox-pico" CACHE PATH "Path to Luckfox Pico SDK")
set(RGA_ROOT "${LUCKFOX_SDK_PATH}/media/rga/release_rga_rv1106_arm-rockchip830-linux-uclibcgnueabihf")
set(RKNN_LIB_DIR "${LUCKFOX_SDK_PATH}/media/out/lib")

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${RGA_ROOT}/include
)

# 1. Camera V4L2 Module
add_library(camera_core STATIC
    src/camera/v4l2_camera.cpp
)
target_compile_options(camera_core PRIVATE ${COMMON_COMPILE_OPTIONS})

# 2. DMA Allocator Module
add_library(dma_core STATIC
    src/utils/dma_allocator.cpp
)
target_compile_options(dma_core PRIVATE ${COMMON_COMPILE_OPTIONS})

# 3. RGA 2D Blitter Module
add_library(rga_core STATIC
    src/rga/rga_converter.cpp
)
target_compile_options(rga_core PRIVATE ${COMMON_COMPILE_OPTIONS})

# 4. NPU RKNN Runtime Module
add_library(npu_core STATIC
    src/npu/rknn_engine.cpp
)
target_compile_options(npu_core PRIVATE ${COMMON_COMPILE_OPTIONS})

# 5. Multi-Threaded Pipeline Coordinator Module
add_library(pipeline_core STATIC
    src/pipeline/camera_pipeline.cpp
)
target_compile_options(pipeline_core PRIVATE ${COMMON_COMPILE_OPTIONS})

# Production Application Executable
add_executable(smart_edge_camera
    src/main.cpp
)

target_compile_options(smart_edge_camera PRIVATE ${COMMON_COMPILE_OPTIONS})

target_link_directories(smart_edge_camera PRIVATE 
    ${RGA_ROOT}/lib
    ${RKNN_LIB_DIR}
)

target_link_libraries(smart_edge_camera PRIVATE 
    pipeline_core
    camera_core 
    dma_core
    rga_core 
    npu_core
    rga 
    rknnmrt
    pthread
)

set_target_properties(smart_edge_camera PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "/oem/usr/lib:/usr/lib:/lib"
)

install(TARGETS smart_edge_camera DESTINATION bin)
