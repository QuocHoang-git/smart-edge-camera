# Smart Edge Camera Firmware: Architecture & Systems Engineering Specification

**Target Platform**: Rockchip RV1103 (ARM Cortex-A7 @ 1.2GHz, NPU 0.5 TOPS, 64MB DDR2 SIP)  
**Target Organization**: Motorola Solutions - Specialty Cameras & Embedded Vision Firmware  
**Standard**: C++17, POSIX Real-Time, Embedded Linux (Buildroot)  

---

## 1. System Engineering Context & Memory Budget

On resource-constrained system-on-chips like the Rockchip RV1103 with integrated **64MB DDR2**, memory allocation is physically divided:
- **CMA Reserved Pool (Contiguous Memory Allocator)**: ~32MB reserved strictly for hardware DMA engines (ISP, RGA, NPU).
- **Linux Kernel Space**: ~16MB (page tables, slab cache, network stack, V4L2/DRM driver state).
- **Userspace Application Envelope**: **<= 16MB Available RAM**.

### First Principles Memory Analysis (Zero-Copy Justification)
- 1 Frame 1080p (1920x1080) NV12 = $1920 \times 1080 \times 1.5 = 3.11\text{ MB}$.
- Standard 4-buffer V4L2 ring: $4 \times 3.11\text{ MB} = 12.44\text{ MB}$ (pinned in CMA).
- **Zero-Copy Imperative**: If userspace performed even a single `memcpy` per frame, memory bandwidth would collapse (DDR2 bandwidth limit ~1.6 GB/s), leading to cache pollution and triggering the Linux Kernel Out-Of-Memory (OOM) Killer.

```
       RV1103 Physical RAM (64MB)
+------------------------------------+------------------------------------+
|         CMA Reserved Pool          |       Linux Normal Memory          |
|              (32 MB)               |             (32 MB)                |
+------------------------------------+------------------------------------+
| - 4x 1080p V4L2 Buffers (12.4 MB)  | - Kernel Core & Slab (~16 MB)      |
| - Sub-Stream Tensor Buffer (16 KB) | - System Daemons / uClibc (~8 MB)  |
| - NPU Weights & Output Tensors     | - Smart Camera Application (<= 8MB)|
+------------------------------------+------------------------------------+
```

---

## 2. End-to-End Zero-Copy Hardware Pipeline

Every stage of the vision pipeline exchanges hardware memory descriptors (**DMA-BUF file descriptors**) without CPU memory copying:

```
[SC3336 MIPI Sensor]
         | (MIPI-CSI2 2-lane raw Bayer)
         v
[Rockchip ISP (rkisp_v7)]
         | (Hardware 3A: AE, AWB, Denoise)
         | DMA write into CMA physical pages
         v
[V4L2 Video Subsystem (/dev/video11)]
         | Exported via VIDIOC_EXPBUF -> src_dma_fd (fd=5..8)
         v
[Rockchip RGA 2D Blitter Engine]
         | Hardware DMA Crop, Downscale (1080p -> 128x128), and CSC
         | Latency: ~3.37 ms (0% CPU load)
         v
[Linux CMA Heap (/dev/rk_dma_heap/rk-dma-heap-cma)]
         | Dedicated Sub-stream buffer -> dst_dma_fd (fd=8, 16384 bytes)
         v
[Rockchip NPU (RKNN Micro Runtime / librknnmrt.so)]
         | Direct DMA input binding via rknn_create_mem_from_fd()
         | Native NC1HWC2 output tensor execution
         | BlazeFace Inference Latency: ~3.30 ms
         v
[Security Event Hub]
         | Real-time Face Alert Trigger (JSON/MQTT/Webhook)
```

---

## 3. Concurrency Architecture & Bounded Ring-Buffer

The firmware implements an asynchronous, multi-threaded **Producer-Consumer** architecture using modern C++17 primitives:

1. **Producer Thread (`capture_worker_loop`)**:
   - Executes V4L2 `VIDIOC_DQBUF` with `select()` event waiting.
   - Dispatches zero-copy hardware blit to RGA.
   - Re-enqueues 1080p V4L2 frame immediately (`VIDIOC_QBUF`) to prevent camera hardware frame drops.
   - Pushes frame tokens into the `BoundedQueue`.

2. **Consumer Thread (`ai_worker_loop`)**:
   - Dequeues frame tokens using condition variables.
   - Fires NPU inference asynchronously.
   - Extracts bounding boxes and facial landmark coordinates.

3. **Lowest Latency Strategy (`Drop-Oldest Policy`)**:
   - If the AI Consumer is delayed, the `BoundedQueue<T>` automatically drops the oldest item rather than blocking the camera capture.
   - Preserves real-time camera ingestion at full **25.0 FPS** with **0 accumulated latency**.

---

## 4. Benchmark & Hardware Performance Metrics

Measured directly on hardware (Rockchip RV1103 @ 1.2GHz, SC3336 3MP sensor):

| Stage | Mechanism | Format / Resolution | Hardware Latency |
| :--- | :--- | :--- | :--- |
| **Ingestion** | MIPI-CSI -> V4L2 ISP | 1920x1080 NV12 | 25.0 FPS (Deterministic, $\Delta t = 40.0\text{ ms}$) |
| **Rescaling** | Rockchip RGA 2D Blit | 1080p NV12 -> 128x128 Gray8 | **3.37 ms** |
| **Edge AI** | Rockchip NPU INT8 | Google BlazeFace (INT8 Quantized) | **3.30 ms** |
| **Total AI Vision** | Zero-Copy DMA Chain | 1080p Camera -> Face Coordinates | **6.67 ms** |
| **Max AI Capacity** | Hardware Pipeline Limit | Continuous Detection | **~150 FPS** |

---

## 5. Build, Cross-Compilation & Deployment

### Toolchain Specs
- **Architecture**: ARMv7-A Cortex-A7 (`armhf`)
- **FPU Engine**: NEON-VFPv4 with Hard-Float ABI (`-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard`)
- **C Library**: uClibc-ng (`arm-rockchip830-linux-uclibcgnueabihf`)

### Build Commands
```bash
# On Linux Host (Ubuntu x86_64)
./scripts/build.sh

# Deploy and launch 24/7 on target board via SSH
./scripts/deploy.sh ssh 192.168.8.2
```
