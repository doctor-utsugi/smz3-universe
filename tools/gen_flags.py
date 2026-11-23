#!/usr/bin/env python3
"""Renders the country flags of the profile list (core/ui/smz3_ui.c COUNTRIES) from Noto Color Emoji into assets/flags/<CODE>.png.

gen_flags.py --pv <out dir>   writes the flags as 12x9 previews (u8 w, u8 h, RGBA) for the 3DS romfs instead."""
import os, re, sys
from PIL import Image, ImageDraw, ImageFont
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if len(sys.argv) > 2 and sys.argv[1] == "--pv":
    os.makedirs(sys.argv[2], exist_ok=True)
    src = os.path.join(root, "assets/flags")
    for n in sorted(os.listdir(src)):
        if not n.endswith(".png"): continue
        im = Image.open(os.path.join(src, n)).convert("RGBA").resize((12, 9), Image.LANCZOS)
        open(os.path.join(sys.argv[2], n[:-4] + ".pv"), "wb").write(bytes([12, 9]) + im.tobytes())
    sys.exit(0)
font = sys.argv[1] if len(sys.argv) > 1 else "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf"
codes = re.findall(r'\{"([A-Z]{2})",', open(os.path.join(root, "core/ui/smz3_ui.c")).read())
out = os.path.join(root, "assets/flags")
os.makedirs(out, exist_ok=True)
f = ImageFont.truetype(font, 109)
for c in codes:
    im = Image.new("RGBA", (160, 160), (0, 0, 0, 0))
    ImageDraw.Draw(im).text((0, 0), "".join(chr(0x1F1E6 + ord(x) - 65) for x in c), font=f, embedded_color=True)
    im = im.crop(im.getbbox())
    im.resize((32, round(32 * im.height / im.width)), Image.LANCZOS).save(os.path.join(out, c + ".png"))
print(len(codes), "flags")
