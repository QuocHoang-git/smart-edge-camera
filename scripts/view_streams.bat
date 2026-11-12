@echo off
REM ==============================================================================
REM Script: view_streams.bat
REM Muc dich: Mo dong thoi 2 cua so VLC nam canh nhau (Side-by-Side)
REM          Cua so 1 (Trai): rtsp://192.168.8.2:554/live/main (1080p Main Stream)
REM          Cua so 2 (Phai): rtsp://192.168.8.2:554/live/sub  (360p Sub Stream + AI Box)
REM ==============================================================================

set BOARD_IP=192.168.8.2
set VLC_PATH="C:\Program Files\VideoLAN\VLC\vlc.exe"

if not exist %VLC_PATH% (
    set VLC_PATH="C:\Program Files (x86)\VideoLAN\VLC\vlc.exe"
)

if not exist %VLC_PATH% (
    echo [ERROR] Khong tim thay VLC Media Player tai C:\Program Files\VideoLAN\VLC\vlc.exe!
    echo Vui long cai dat VLC hoac sua bien VLC_PATH trong file nay.
    pause
    exit /b 1
)

echo ==============================================================================
echo   DANG MO DONG THOI 2 CUA SO VLC SIDE-BY-SIDE
echo   1. Trai: rtsp://%BOARD_IP%:554/live/main (1080p Pristine)
echo   2. Phai: rtsp://%BOARD_IP%:554/live/sub  (360p + AI Face OSD)
echo ==============================================================================

REM Cua so 1: Luong Main 1080p (Dat ben trai man hinh: x=30, y=80, rong=850, cao=520)
start "" %VLC_PATH% rtsp://%BOARD_IP%:554/live/main --network-caching=100 --video-x=30 --video-y=80 --width=850 --height=520 --video-title="[STREAM 1] Main 1080p (Clean)" --no-video-title-show

REM Nghi 1 giay de cua so 1 khoi dong
timeout /t 1 /nobreak >nul

REM Cua so 2: Luong Sub 360p co AI OSD (Dat ben phai man hinh: x=900, y=80, rong=850, cao=520)
start "" %VLC_PATH% rtsp://%BOARD_IP%:554/live/sub --network-caching=100 --video-x=900 --video-y=80 --width=850 --height=520 --video-title="[STREAM 2] Sub 360p (RetinaFace AI OSD)" --no-video-title-show

echo.
echo Hai cua so VLC da duoc mo san sang!
echo Ban co the quay lai man hinh va luu thanh docs/assets/demo_dual_stream.gif.
