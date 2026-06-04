#!/bin/bash
while true; do

#gst-launch-1.0 udpsrc port=50000 caps="application/x-rtp, media=(string)video, clock-rate=(int)90000 , encoding-name=(string)H264 , #payload=(int)96" ! rtph264depay ! ffdec_h264 ! ffmpegcolorspace ! videoscale ! "video/x-raw-yuv, width=320, height=240" ! xvimagesink


D:/gstreamer/1.0/msvc_x86/bin/gst-launch-1.0.exe udpsrc port=56789 ! capsfilter caps="application/x-rtp, media=video, encoding-name=H264, payload=96" ! rtpjitterbuffer latency = 100 ! rtph264depay ! avdec_h264 ! autovideoconvert ! autovideosink
done
