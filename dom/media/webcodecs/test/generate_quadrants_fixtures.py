#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with
# this file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Generate the quadrants fixtures checked by videoFrameFormatChecks.js.

Each fixture encodes the same 64x64 I420 picture, the QUADRANTS table in
videoFrameFormatChecks.js: VP9 losslessly, H.264 as Constrained Baseline at
qp=1, which is exact on flat blocks where qp=0 would force High 4:4:4. Every
fixture decodes to the picture exactly. Not run by CI; regenerate every
fixture or the named ones:

  ./generate_quadrants_fixtures.py [FIXTURE...]

The checked-in fixtures were generated with FFmpeg 6.1.1 (libvpx-vp9, libx264).
"""

import argparse
import subprocess
from pathlib import Path

WIDTH = 64
HEIGHT = 64
FRAME_RATE = "30"

# (Y, Cb, Cr) of the top-left, top-right, bottom-left and bottom-right quadrants.
QUADRANTS = [(81, 90, 240), (145, 54, 34), (41, 240, 110), (210, 16, 146)]

FIXTURES = {
    "quadrants-vp9.ivf": {
        "frames": 1,
        "args": ["-c:v", "libvpx-vp9", "-lossless", "1", "-f", "ivf"],
    },
    "quadrants-vp9.webm": {
        "frames": 10,
        "args": ["-c:v", "libvpx-vp9", "-lossless", "1", "-g", "5", "-f", "webm"],
    },
    "quadrants-h264.annexb": {
        "frames": 1,
        "args": [
            "-c:v",
            "libx264",
            "-profile:v",
            "baseline",
            "-level",
            "3.0",
            "-qp",
            "1",
            "-x264-params",
            "keyint=1:bframes=0:aud=0",
            "-colorspace",
            "smpte170m",
            "-color_primaries",
            "smpte170m",
            "-color_trc",
            "smpte170m",
            "-color_range",
            "tv",
            "-f",
            "h264",
        ],
    },
}


def quadrant(x, y):
    return QUADRANTS[(2 if y >= HEIGHT // 2 else 0) + (1 if x >= WIDTH // 2 else 0)]


def i420_picture():
    luma = bytes(quadrant(x, y)[0] for y in range(HEIGHT) for x in range(WIDTH))
    chroma = [
        bytes(
            quadrant(2 * x, 2 * y)[plane]
            for y in range(HEIGHT // 2)
            for x in range(WIDTH // 2)
        )
        for plane in (1, 2)
    ]
    return luma + chroma[0] + chroma[1]


def encode(ffmpeg, output, fixture):
    command = [
        ffmpeg,
        "-y",
        "-hide_banner",
        "-loglevel",
        "error",
        "-f",
        "rawvideo",
        "-pix_fmt",
        "yuv420p",
        "-s",
        f"{WIDTH}x{HEIGHT}",
        "-r",
        FRAME_RATE,
        "-i",
        "-",
        "-flags",
        "+bitexact",
        "-fflags",
        "+bitexact",
        "-frames:v",
        str(fixture["frames"]),
        *fixture["args"],
        str(output),
    ]
    subprocess.run(command, input=i420_picture() * fixture["frames"], check=True)


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("fixtures", nargs="*", metavar="FIXTURE")
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--output-dir", type=Path, default=Path(__file__).parent)
    args = parser.parse_args()

    unknown = set(args.fixtures) - FIXTURES.keys()
    if unknown:
        parser.error(f"unknown fixture: {', '.join(sorted(unknown))}")
    for name in args.fixtures or FIXTURES:
        encode(args.ffmpeg, args.output_dir / name, FIXTURES[name])


if __name__ == "__main__":
    main()
