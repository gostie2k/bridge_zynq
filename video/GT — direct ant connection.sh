#!/bin/bash
# GT - direct ant connection (Linux version)

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &> /dev/null && pwd)
VIDEO_FILE="$SCRIPT_DIR/sample_1280x720_surfing_with_audio.mp4"

# Проверка существования файла
if [[ ! -f "$VIDEO_FILE" ]]; then
    echo "ОШИБКА: Файл не найден: $VIDEO_FILE"
    echo "Текущая папка: $(pwd)"
    echo "Содержимое папки:"
    ls -la "$SCRIPT_DIR"
    exit 1
fi

echo "Файл найден: $VIDEO_FILE"

while true; do
    echo "VIDEO SEND > VIDEO SEND > VIDEO SEND > VIDEO SEND > VIDEO SEND > VIDEO SEND"
    
    gst-launch-1.0 filesrc location="$VIDEO_FILE" ! \
        decodebin ! \
        x264enc bitrate=7000 vbv-buf-capacity=50 key-int-max=50 ! \
        rtph264pay mtu=1212 ! \
        udpsink host=192.168.250.21 port=5007

    echo "Stream ended, restarting in 2 seconds..."
    sleep 2
done
