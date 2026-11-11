cmake_minimum_required(VERSION 3.16)
project(smart_edge_camera VERSION 0.1.0 LANGUAGES C CXX)

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

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${RGA_ROOT}/include
)

# Core Camera V4L2 Library
add_library(camera_core STATIC
    src/camera/v4l2_camera.cpp
)
target_compile_options(camera_core PRIVATE ${COMMON_COMPILE_OPTIONS})

# DMA Allocator Library
add_library(dma_core STATIC
    src/utils/dma_allocator.cpp
)
target_compile_options(dma_core PRIVATE ${COMMON_COMPILE_OPTIONS})

# Core RGA Hardware Accelerator Library
add_library(rga_core STATIC
    src/rga/rga_converter.cpp
)
target_compile_options(rga_core PRIVATE ${COMMON_COMPILE_OPTIONS})

# Phase 3 Executable: Dual-Stream Pipeline PoC
add_executable(smart_edge_camera_poc
    src/main.cpp
)

target_compile_options(smart_edge_camera_poc PRIVATE ${COMMON_COMPILE_OPTIONS})

target_link_directories(smart_edge_camera_poc PRIVATE ${RGA_ROOT}/lib)
target_link_libraries(smart_edge_camera_poc PRIVATE 
    camera_core 
    dma_core
    rga_core 
    rga 
    pthread
)

set_target_properties(smart_edge_camera_poc PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "/oem/usr/lib:/usr/lib:/lib"
)

install(TARGETS smart_edge_camera_poc DESTINATION bin)
