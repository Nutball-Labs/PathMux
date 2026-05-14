#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Nutball Labs / Stephen Berg
#
# Generates logo_morph.mp4 — 6-second CamClops <-> Nutball-Labs cross-fade.
# Called by CMake at build time:
#   gen_logo_morph.py <ffmpeg> <output.mp4> <camclops_256.png> <Nutball-Labs_logo.png>
#
# Output: 960x540 H.264 CRF23 yuv420p, 30fps, 6s (180 frames), no audio.
# Sine ease-in-out: 3s fade in, 3s fade back.
import sys
import math
import subprocess
from PIL import Image


def main():
    if len(sys.argv) != 5:
        print(f"Usage: {sys.argv[0]} <ffmpeg> <output.mp4> <pmx_logo> <ntb_logo>",
              file=sys.stderr)
        sys.exit(1)

    ffmpeg, output, pmx_path, ntb_path = sys.argv[1:]

    W, H   = 960, 540
    FPS    = 30
    FRAMES = FPS * 6   # 180 frames = 6 s

    BG = (0x1e, 0x1e, 0x2e, 255)  # dark background matching UI tile colour

    def load_logo(path):
        img = Image.open(path).convert("RGBA")
        img.thumbnail((W, H), Image.LANCZOS)
        canvas = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        canvas.paste(img, ((W - img.width) // 2, (H - img.height) // 2), img)
        return canvas

    pmx = load_logo(pmx_path)
    ntb = load_logo(ntb_path)

    cmd = [
        ffmpeg, "-y",
        "-f", "rawvideo", "-vcodec", "rawvideo",
        "-s", f"{W}x{H}", "-pix_fmt", "rgba",
        "-r", str(FPS), "-i", "-",
        "-an",
        "-vcodec", "libx264", "-pix_fmt", "yuv420p",
        "-crf", "23", "-preset", "medium",
        "-movflags", "+faststart",
        output,
    ]

    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE)

    try:
        for i in range(FRAMES):
            # phase: 0→1 over first half-cycle, 1→0 over second half
            raw = (i / FRAMES) * 2.0
            phase = raw if raw <= 1.0 else 2.0 - raw
            # sine ease-in-out: slow at endpoints, fast through midpoint
            t = 0.5 * (1.0 - math.cos(math.pi * phase))

            frame = Image.new("RGBA", (W, H), BG)

            if t < 1.0:
                pmx_f = pmx.copy()
                pmx_f.putalpha(pmx_f.split()[3].point(lambda a: int(a * (1.0 - t))))
                frame.paste(pmx_f, (0, 0), pmx_f)

            if t > 0.0:
                ntb_f = ntb.copy()
                ntb_f.putalpha(ntb_f.split()[3].point(lambda a: int(a * t)))
                frame.paste(ntb_f, (0, 0), ntb_f)

            proc.stdin.write(frame.tobytes())
    finally:
        proc.stdin.close()
        proc.wait()

    if proc.returncode != 0:
        print(f"ERROR: ffmpeg exited {proc.returncode}", file=sys.stderr)
        sys.exit(1)

    print(f"  Generated {output}")


if __name__ == "__main__":
    main()
# SN: 00101
