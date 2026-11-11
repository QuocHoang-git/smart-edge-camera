#!/usr/bin/env bash
# ==============================================================================
# Script: scripts/deploy.sh
# Purpose: Push and run binary on Luckfox Pico via ADB or SSH
# ==============================================================================

set -euo pipefail

TARGET_BIN="build/smart_edge_camera_poc"
TARGET_DIR="/tmp"

if [[ ! -f "${TARGET_BIN}" ]]; then
    echo "[ERROR] Binary ${TARGET_BIN} does not exist. Run scripts/build.sh first!"
    exit 1
fi

MODE="${1:-adb}"

if [[ "${MODE}" == "adb" ]]; then
    echo "[DEPLOY] Pushing binary via ADB to ${TARGET_DIR}..."
    adb push "${TARGET_BIN}" "${TARGET_DIR}/"
    adb shell "chmod +x ${TARGET_DIR}/smart_edge_camera_poc"
    echo "[EXECUTE] Running binary on target..."
    adb shell "${TARGET_DIR}/smart_edge_camera_poc"
elif [[ "${MODE}" == "ssh" ]]; then
    BOARD_IP="${2:-172.32.0.93}"
    echo "[DEPLOY] Pushing binary via SSH to root@${BOARD_IP}:${TARGET_DIR}..."
    scp "${TARGET_BIN}" "root@${BOARD_IP}:${TARGET_DIR}/"
    MODEL_FILE="models/face_detection_rv1106.rknn"
    if [[ -f "${MODEL_FILE}" ]]; then
        echo "[DEPLOY] Pushing model ${MODEL_FILE} to ${TARGET_DIR}..."
        scp "${MODEL_FILE}" "root@${BOARD_IP}:${TARGET_DIR}/"
    fi
    ssh "root@${BOARD_IP}" "chmod +x ${TARGET_DIR}/smart_edge_camera_poc && LD_LIBRARY_PATH=/oem/usr/lib:/usr/lib:/lib ${TARGET_DIR}/smart_edge_camera_poc"
else
    echo "Usage: $0 [adb | ssh <BOARD_IP>]"
    exit 1
fi
