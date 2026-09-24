#!/usr/bin/env bash

set -euo pipefail

segdur=0.5
duration=1.5
gopsize=3

encode="-c:v libx264 -crf 51 -g $gopsize \
  -movflags empty_moov+frag_keyframe+dash \
  -f dash -seg_duration $segdur"

ffmpeg -f lavfi -i testsrc=r=25:s=320x240:duration=$duration $encode -init_seg_name landscape-init.mp4  \
  -media_seg_name "landscape-\$Number\$.m4s" \
  landscape.mp4

ffmpeg -f lavfi -noautorotate -display_rotation 90 \
  -i testsrc=r=25:s=320x240:duration=$duration $encode -init_seg_name landscape-rot90-init.mp4  \
  -media_seg_name "landscape-rot90-\$Number\$.m4s" \
  landscape-rot90.mp4

ffmpeg -f lavfi -i testsrc=r=25:s=240x320:duration=$duration $encode -init_seg_name portrait-init.mp4  \
  -media_seg_name "portrait-\$Number\$.m4s" \
  portrait.mp4

rm landscape.mp4 portrait.mp4 landscape-rot90.mp4

for f in $(find . -path '*.m4s' -or -path '*.mp4'); do
  echo "Cache-Control: no-store" > "$f^headers^"
done
