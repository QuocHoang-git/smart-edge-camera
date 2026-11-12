#!/usr/bin/env bash
# ==============================================================================
# Script: scripts/view_streams.sh
# Muc dich: Mo dong thoi 2 cua so VLC nam canh nhau (Side-by-Side) tren Linux
#          Cua so 1 (Trai): rtsp://192.168.8.2:554/live/main (1080p Main Stream)
#          Cua so 2 (Phai): rtsp://192.168.8.2:554/live/sub  (360p Sub Stream + AI Box)
# ==============================================================================

set -euo pipefail

BOARD_IP="${1:-192.168.8.2}"

echo "=============================================================================="
echo "  DANG MO DONG THOI 2 CUA SO VLC SIDE-BY-SIDE"
echo "  1. Trai: rtsp://${BOARD_IP}:554/live/main (1080p Pristine)"
echo "  2. Phai: rtsp://${BOARD_IP}:554/live/sub  (360p + AI Face OSD)"
echo "=============================================================================="

# Cua so 1: Luong Main 1080p ben trai
vlc "rtsp://${BOARD_IP}:554/live/main" --network-caching=100 --video-x=30 --video-y=80 --width=850 --height=520 --video-title="[STREAM 1] Main 1080p (Clean)" --no-video-title-show &

sleep 1

# Cua so 2: Luong Sub 360p co AI OSD ben phai
vlc "rtsp://${BOARD_IP}:554/live/sub" --network-caching=100 --video-x=900 --video-y=80 --width=850 --height=520 --video-title="[STREAM 2] Sub 360p (RetinaFace AI OSD)" --no-video-title-show &

echo "Hai cua so VLC da duoc mo song song."
