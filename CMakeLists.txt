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
    -Wold-style-cast
    -Wcast-align
    -Wunused
    -Woverloaded-virtual
    -Wconversion
    -Wsign-conversion
    -pthread
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    list(APPEND COMMON_COMPILE_OPTIONS -g -O0 -DDEBUG)
else()
    list(APPEND COMMON_COMPILE_OPTIONS -O2 -DNDEBUG)
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Core Camera Library
add_library(camera_core STATIC
    src/camera/v4l2_camera.cpp
)
target_compile_options(camera_core PRIVATE ${COMMON_COMPILE_OPTIONS})

# Phase 2 Executable: V4L2 Capture & Frame Timing Validator
add_executable(smart_edge_camera_poc
    src/main.cpp
)

target_compile_options(smart_edge_camera_poc PRIVATE ${COMMON_COMPILE_OPTIONS})
target_link_libraries(smart_edge_camera_poc PRIVATE camera_core pthread)

install(TARGETS smart_edge_camera_poc DESTINATION bin)
