# Smart Edge Camera with Dual-Stream Architecture & Real-Time Face Detection

An embedded Linux edge vision firmware application developed for Rockchip RV1103/RV1106 SoC (Luckfox Pico series), featuring zero-copy hardware acceleration, deep learning inference on edge NPU, and deterministic multi-threaded pipeline execution under extreme memory constraints (64MB DDR2).

---

## 1. System Overview & Architecture Highlights

```
+---------------------------------------------------------------------------------------------+
|                                    Rockchip RV1103 SoC                                      |
|                                                                                             |
|  +-------------------+        DMA       +-------------------+       DMA       +----------+  |
|  | MIPI-CSI Sensor   | ---------------> | V4L2 Subsystem    | --------------> | CMA Pool |  |
|  | (SC3336 / OV5647) |                  | (Kernel / Driver) |   (Zero-Copy)   | (Phys)   |  |
|  +-------------------+                  +-------------------+                 +----+-----+  |
|                                                                                    |        |
|                                         +------------------------------------------+        |
|                                         |                                                   |
|                                         v                                                   |
|                     +---------------------------------------+                               |
|                     | User-space DMA-BUF / mmap Descriptors |                               |
|                     +-------------------+-------------------+                               |
|                                         |                                                   |
|                    +--------------------+--------------------+                              |
|                    |                                         |                              |
|                    v (Main Stream: 1080p NV12)               v (Sub Stream: 2D HW Blit)     |
|          +--------------------+                    +--------------------+                   |
|          | Frame Queue & Disp |                    | Rockchip RGA 2D    |                   |
|          | (RTSP/MJPEG HTTP)  |                    | (Downscale/RGB888) |                   |
|          +--------------------+                    +---------+----------+                   |
|                                                              |                              |
|                                                              v (Zero-copy Virtual Address)  |
|                                                    +--------------------+                   |
|                                                    | Rockchip RKNN NPU  |                   |
|                                                    | (INT8 Face Detect) |                   |
|                                                    +---------+----------+                   |
|                                                              |                              |
|                                                              v (Inference Metadata)         |
|                                                    +--------------------+                   |
|                                                    | Event/Alert Hub    |                   |
|                                                    | (MQTT / Webhook)   |                   |
|                                                    +--------------------+                   |
+---------------------------------------------------------------------------------------------+
```

### Key Engineering Features
- **Zero-Copy Pipeline**: Inter-process and hardware acceleration memory sharing using DMA-BUF / mmap memory mapping across V4L2, RGA, and RKNN.
- **Strict Memory Envelope**: Engineered for **64MB DDR2 SIP RAM**, avoiding userspace `memcpy` stalls and memory fragmentation.
- **Hardware-Accelerated Color Conversion & Scaling**: Offloading pixel transformation (NV12 -> RGB888) and downscaling (1080p -> 320x240) to Rockchip 2D RGA.
- **Edge NPU Inference**: Quantized INT8 face detection running directly on Rockchip 0.5/1.0 TOPS NPU.
- **C++17 Thread-Safe Ring-Buffer**: Bounded, non-blocking frame dropping queue designed to prevent frame accumulation and latency spikes.

---

## 2. Directory Layout

```text
smart-edge-camera/
├── cmake/                      # Toolchain and cross-compilation configurations
│   └── toolchain-arm-rockchip830-uclibcgnueabihf.cmake
├── docs/                       # Architecture specs, diagrams, and low-level analysis
│   └── diagrams/
├── include/                    # Header declarations (C++17)
│   ├── camera/                 # V4L2 capture abstractions
│   ├── npu/                    # RKNN runtime wrapper
│   ├── pipeline/               # Bounded queues and thread management
│   ├── rga/                    # Rockchip 2D graphics accelerator wrapper
│   └── utils/                  # Logging, timing, and system diagnostics
├── models/                     # RKNN quantized models and export scripts
├── scripts/                    # Deployment, cross-build, and test scripts
├── src/                        # Implementation files
│   ├── camera/
│   ├── npu/
│   ├── pipeline/
│   ├── rga/
│   ├── utils/
│   └── main.cpp
├── CMakeLists.txt              # Top-level build configuration
├── LICENSE
└── README.md
```

---

## 3. Toolchain & Prerequisites

- **Host Environment**: Linux Ubuntu 20.04/22.04 LTS (x86_64)
- **Target Platform**: Luckfox Pico (Rockchip RV1103, ARM Cortex-A7 @ 1.2GHz)
- **Target C Library**: uClibc-ng (`arm-rockchip830-linux-uclibcgnueabihf`)
- **Language Standard**: Modern C++ (C++17)
