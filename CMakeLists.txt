cmake_minimum_required(VERSION 3.16)
project(smart_edge_camera VERSION 0.1.0 LANGUAGES C CXX)

# Enforce C++17 Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for clangd / language servers
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Optimization and Warning Flags
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

# Global Include Directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Phase 1 PoC Executable: System sanity and architecture verification
add_executable(smart_edge_camera_poc
    src/main.cpp
)

target_compile_options(smart_edge_camera_poc PRIVATE ${COMMON_COMPILE_OPTIONS})
target_link_libraries(smart_edge_camera_poc PRIVATE pthread)

# Installation targets
install(TARGETS smart_edge_camera_poc DESTINATION bin)
