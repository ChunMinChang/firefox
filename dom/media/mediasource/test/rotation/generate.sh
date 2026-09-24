#!/usr/bin/env bash

set -euo pipefail

cd "$(dirname "$0")"

segdur=0.6
duration=1.8
gopsize=15

encode=(-c:v libx264 -pix_fmt yuv420p -crf 51 -g "$gopsize"
  -keyint_min "$gopsize" -sc_threshold 0
  -movflags empty_moov+frag_keyframe+dash
  -f dash -seg_duration "$segdur")

ffmpeg -y -f lavfi -i "testsrc=r=25:s=320x240:duration=$duration" "${encode[@]}" -init_seg_name landscape-init.mp4 \
  -media_seg_name "landscape-\$Number\$.m4s" \
  landscape.mp4

ffmpeg -y -f lavfi -noautorotate -display_rotation 90 \
  -i "testsrc=r=25:s=320x240:duration=$duration" "${encode[@]}" -init_seg_name landscape-rot90-init.mp4 \
  -media_seg_name "landscape-rot90-\$Number\$.m4s" \
  landscape-rot90.mp4

ffmpeg -y -f lavfi -i "testsrc=r=25:s=240x320:duration=$duration" "${encode[@]}" -init_seg_name portrait-init.mp4 \
  -media_seg_name "portrait-\$Number\$.m4s" \
  portrait.mp4

rm landscape.mp4 portrait.mp4 landscape-rot90.mp4

for f in ./*.m4s ./*.mp4; do
  echo "Cache-Control: no-store" > "$f^headers^"
done
