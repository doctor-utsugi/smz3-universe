#!/usr/bin/env python3
"""Draws the 32 ALttP seed hash icons as the file select screen shows them, from z3randomizer's own data (MIT): the tiles of
data/fileselectgfx.2bpp (VRAM $3200 = BG3 tile $240), the colours of data/hudpalette.pal and the tilemap words of
hashalphabet.asm. Writes data/icons/z3hash_NN.png (16 x 16, transparent where the game shows the background)."""
import re
import sys
from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parents[1]
z3 = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "vendor/z3randomizer"
gfx = (z3 / "data/fileselectgfx.2bpp").read_bytes()
pal = (z3 / "data/hudpalette.pal").read_bytes()
consts = dict(re.findall(r"^(!FS_\w+) = \$([0-9A-F]+)", (z3 / "fileselect.asm").read_text(), re.M))
asm = (z3 / "hashalphabet.asm").read_text()
table = asm[asm.index("HashAlphabetTiles:"):asm.index("FileSelect_PlayerSelectText_Top:")]
words = []
for line in re.findall(r"^dw (.+)$", table, re.M):
    for w in line.split(","):
        v = 0
        for part in w.strip().split("|"):
            v |= int(consts[part][0:], 16) if part.startswith("!") else int(part.lstrip("#$"), 16)
        words.append(v)
assert len(words) == 128, len(words)


def colour(i):
    c = pal[2 * i] | pal[2 * i + 1] << 8
    return ((c & 31) * 255 // 31, (c >> 5 & 31) * 255 // 31, (c >> 10 & 31) * 255 // 31, 255)


out = root / "data/icons"
for n in range(32):
    img = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    for k in range(4):
        w = words[n * 4 + k]
        tile = (w & 0x3FF) - 0x240
        p = w >> 10 & 7
        for y in range(8):
            sy = 7 - y if w & 0x8000 else y
            lo, hi = gfx[tile * 16 + sy * 2], gfx[tile * 16 + sy * 2 + 1]
            for x in range(8):
                b = 7 - (7 - x if w & 0x4000 else x)
                c = (lo >> b & 1) | (hi >> b & 1) << 1
                if c:
                    img.putpixel((k % 2 * 8 + x, k // 2 * 8 + y), colour(p * 4 + c))
    img.save(out / ("z3hash_%02d.png" % n))
print("32 hash icons written to", out)
