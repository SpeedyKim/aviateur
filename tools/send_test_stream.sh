#!/bin/bash
# Send a synthetic OpenIPC-like video stream to Aviateur's "Local test" tab (UDP 5600, H.265, 4:2:0).
# Needs GStreamer (gst-launch-1.0) with the x265 plugin.
#
#   tools/send_test_stream.sh            # moving ball on black
#   PATTERN=smpte tools/send_test_stream.sh   # colour bars (good for judging the glass OSD)
#
# Note: keep format=I420. Without it x265 may pick 4:4:4 12-bit, which real OpenIPC cameras never send.
PATTERN=${PATTERN:-ball}
HOST=${HOST:-127.0.0.1}
PORT=${PORT:-5600}
exec gst-launch-1.0 -q videotestsrc is-live=true pattern="$PATTERN" \
  ! video/x-raw,width=1280,height=720,framerate=30/1,format=I420 \
  ! timeoverlay font-desc="Sans 36" valignment=bottom halignment=right \
  ! x265enc tune=zerolatency speed-preset=ultrafast bitrate=4000 key-int-max=30 \
  ! video/x-h265,profile=main \
  ! rtph265pay config-interval=1 pt=96 \
  ! udpsink host="$HOST" port="$PORT"
