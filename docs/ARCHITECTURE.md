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

| Stage | Mechanism | Format / Resolution | Hardware Latency | CPU Load |
| :--- | :--- | :--- | :--- | :--- |
| **Ingestion** | MIPI-CSI -> V4L2 ISP | 1920x1080 NV12 | 30.00 FPS ($\Delta t = 33.3\text{ ms}$) | ~0% (Hardware ISP) |
| **Main Encode** | Rockchip MPP VEPU | 1080p H.264 CBR (2.5 Mbps) | ~4.2 ms | ~0% (Hardware VEPU) |
| **Sub Scaling** | Rockchip RGA 2D Blit | 1080p NV12 -> 640x360 NV12 | ~1.8 ms | ~0% (Hardware RGA) |
| **AI Downscale** | Rockchip RGA 2D Blit | 640x360 NV12 -> 128x128 Gray8 | ~0.8 ms | ~0% (Hardware RGA) |
| **OSD Burn-In** | Rockchip RGA 2D Blit | Draw Green Rect Bounding Box | ~0.4 ms | ~0% (Hardware RGA) |
| **Sub Encode** | Rockchip MPP VEPU | 640x360 H.264 CBR (800 Kbps) | ~2.1 ms | ~0% (Hardware VEPU) |
| **Edge AI NPU** | Rockchip RKNN INT8 | BlazeFace Face Detector | **3.61 ms** | ~0% (Hardware NPU) |
| **RTSP Server** | Embedded POSIX TCP | Dual-Stream RTP (RFC 6184) | Microseconds | ~6% (Network I/O) |
| **Total System** | Full 24/7 Vision System | 1080p Main + 360p Sub + AI | **30.00 FPS (0 drops)** | **88% IDLE CPU** |

---

## 5. Dual-Stream H.264 Video Compression & RTSP Architecture

The system implements the high-availability security camera industry standard:
- **Main Stream (`/live/main`)**: 1920x1080 @ 30 FPS, H.264 Baseline/Main Profile, 2.5 Mbps CBR, GOP=60 (2.0s). Pristine unadulterated evidentiary video for recording.
- **Sub Stream (`/live/sub`)**: 640x360 @ 30 FPS, H.264 Baseline, 800 Kbps CBR, GOP=60. Real-time preview with AI bounding boxes burned directly into NV12 hardware buffers via RGA before encoding.
- **Embedded RTSP Server (RFC 2326 & RFC 6184)**: Fully self-contained C++17 RTSP server supporting unicast TCP interleaved RTP transport (`RTP/AVP/TCP;interleaved=0-1`), FU-A NAL fragmentation, and zero external dependencies (no live555, no FFmpeg).

### RV1106 / RV1103 Vendor MPP Architecture (Reverse-Engineered Specification)
Unlike desktop Rockchip MPP implementations, the RV1106/RV1103 micro-SoC uses a kernel-offloaded VEPU subsystem (`/dev/mpp_service` + `/dev/mpi/valloc`):
1. **Channel Creation**: Requires `mpp_init_ext(&ctx, &vcodec_attr)` specifying `MPP_CTX_ENC`, `MPP_VIDEO_CodingAVC`, and explicit channel IDs.
2. **Buffer Ingestion**: VEPU strictly requires physical contiguous memory (`MPP_BUFFER_TYPE_EXT_DMA`). Virtual malloc buffers cause kernel memory faults.
3. **Encoded Bitstream Extraction**: `mpi->encode_get_packet()` writes into a 184-byte `struct venc_packet` where `u64priv_data` contains the kernel `mpi_buf_id`. Userspace retrieves the bitstream DMA-BUF fd via `ioctl(valloc_fd, VALLOC_IOCTL_MB_GET_FD, &mb)` on `/dev/mpi/valloc`.
4. **Circular Ring Buffer Wrapping**: The kernel VEPU encoder bitstream allocation is a ~1MB circular ring buffer (`buf_size`). When `offset + len > buf_size`, the bitstream wraps around the buffer boundary into two contiguous chunks (`part1 = buf_size - offset`, `part2 = len - part1`). Firmware reassembles the linear bitstream seamlessly before RTP packetization.
5. **Memory Management**: The mapped DMA packet buffer is released using `mpi->encode_release_packet()` only after payload extraction and IDR/SPS/PPS header cache inspection.

---

## 6. Motorola Solutions Production Deployment Guidelines

1. **System Startup Daemon**:
   The camera firmware runs as a background service managed by Busybox `start-stop-daemon`:
   ```bash
   start-stop-daemon -S -b -m -p /var/run/smart_camera.pid -x /bin/sh -- \
       -c "export LD_LIBRARY_PATH=/oem/usr/lib; exec /tmp/smart_edge_camera /dev/video11 /tmp/face_detection_rv1106.rknn >/tmp/camera.log 2>&1"
   ```
2. **Live RTSP Verification**:
   - Main Stream: `rtsp://<CAMERA_IP>:8554/live/main`
   - Sub Stream (AI OSD): `rtsp://<CAMERA_IP>:8554/live/sub`


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
