#!/usr/bin/env python3
"""Builds the .pv preview of every sprite picture the sprite picker shows.

usage: gen_sprite_previews.py <SMZ3CasSprites/Sprites> <out dir>   (writes <out>/link/<name>.pv and <out>/samus/<name>.pv)

Format: u8 width, u8 height, then width*height RGBA bytes (alpha 0 = transparent), at most 56x56. The picture is cropped to its
visible part and scaled (whole-number nearest-neighbour when it grows, box filter when it shrinks) so the pixel art stays sharp.
Copy the folders next to the .rdc files: /sdcard/SMZ3/sprites/{link,samus} on the Thor, sdmc:/SMZ3/sprites/{link,samus} on the 3DS."""
import os, sys
from PIL import Image

BOX = 56

def convert(src, dst):
    im = Image.open(src).convert("RGBA")
    bbox = im.getchannel("A").point(lambda a: 255 if a else 0).getbbox()
    if not bbox:
        return False
    im = im.crop(bbox)
    w, h = im.size
    if w <= BOX and h <= BOX:
        k = max(1, min(BOX // w, BOX // h))
        im = im.resize((w * k, h * k), Image.NEAREST)
    else:
        k = min(BOX / w, BOX / h)
        im = im.resize((max(1, round(w * k)), max(1, round(h * k))), Image.BOX)
    w, h = im.size
    with open(dst, "wb") as f:
        f.write(bytes([w, h]) + im.tobytes())
    return True

def main():
    root, out = sys.argv[1], sys.argv[2]
    total = 0
    for sub in ("Link", "Samus"):
        d = os.path.join(root, sub)
        o = os.path.join(out, sub.lower())
        os.makedirs(o, exist_ok=True)
        for fn in sorted(os.listdir(d)):
            if fn.lower().endswith(".png"):
                if convert(os.path.join(d, fn), os.path.join(o, fn[:-4] + ".pv")):
                    total += 1
    print(total, "previews")

main()
