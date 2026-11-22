#!/usr/bin/env python3
"""Make the desktop wallpaper image (rootfs/etc/wallpaper.lwp) from a picture.

usage: python tools/gen_wallpaper.py <image> [height_px] [output.lwp]

  <image>      PNG/GIF/JPG of the mascot or logo. A PNG with transparency is
               used as is; a plain or vertically-graded background gets keyed
               out like in gen_logo.py. For animated GIFs the first frame is used.
  height_px    height on screen, default 280
  output.lwp   default rootfs/etc/wallpaper.lwp

File format (little endian): "LWP1", u32 width, u32 height, then
width*height pixels 0xAARRGGBB with straight alpha. The desktop draws the
image centred, blended with a fixed opacity. Needs Pillow.
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image                                   # noqa: E402
from gen_logo import has_transparency, key_out_background   # noqa: E402


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    src = sys.argv[1]
    height = int(sys.argv[2]) if len(sys.argv) > 2 else 280
    dst = sys.argv[3] if len(sys.argv) > 3 else "rootfs/etc/wallpaper.lwp"

    img = Image.open(src)
    img.seek(0)
    img = img.convert("RGBA")
    if not has_transparency(img):
        img = key_out_background(img)

    bbox = img.getchannel("A").point(lambda a: 255 if a > 24 else 0).getbbox()
    if not bbox:
        print("error: nothing visible in the image")
        return 1
    l, t, r, b = bbox
    img = img.crop((max(0, l - 2), max(0, t - 2), min(img.width, r + 2), min(img.height, b + 2)))

    width = max(1, round(img.width * height / img.height))
    img = img.convert("RGBa").resize((width, height), Image.LANCZOS).convert("RGBA")

    data = bytearray(b"LWP1" + struct.pack("<II", width, height))
    px = img.load()
    for y in range(height):
        for x in range(width):
            rr, gg, bb, aa = px[x, y]
            data += struct.pack("<I", (aa << 24) | (rr << 16) | (gg << 8) | bb)
    os.makedirs(os.path.dirname(dst) or ".", exist_ok=True)
    with open(dst, "wb") as f:
        f.write(data)
    print("wrote %s (%dx%d, %d KB)" % (dst, width, height, len(data) // 1024))
    return 0


if __name__ == "__main__":
    sys.exit(main())
