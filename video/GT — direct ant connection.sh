#!/bin/bash
# GT - direct ant connection

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
#FILE=OUTPUT.ts

#echo dir: $SCRIPT_DIR/$FILE

while true; do

echo "VIDEO SEND > VIDEO SEND > VIDEO SEND > VIDEO SEND > VIDEO SEND > VIDEO SEND"

  D:/gstreamer/1.0/msvc_x86/bin/gst-launch-1.0.exe filesrc location=sample_1280x720_surfing_with_audio.mp4 ! decodebin ! x264enc bitrate=7000 vbv-buf-capacity=50 key-int-max=50 \
  ! rtph264pay  mtu=1212 ! udpsink host=192.168.250.21 port= 5007 #sync=true # default: 16
done


#while true; do
#  gst-launch-1.0 filesrc location=$SCRIPT_DIR/$FILE ! decodebin ! x264enc bitrate=7000 vbv-buf-capacity=1000 key-int-max=50 \
#  ! rtph264pay  mtu=1260 ! udpsink host=localhost port=18 # default: 16
#done