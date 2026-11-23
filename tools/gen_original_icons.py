#!/usr/bin/env python3
"""Draws the standing Link and Samus of the base ROMs as .pv pictures (the run list and the sprite screens show them for "Original").

usage: gen_original_icons.py <Super Metroid (JU) rom> <ALttP rom> <sprites dir>   (writes <dir>/link/Original.pv and <dir>/samus/Original.pv)

Format as gen_sprite_previews.py: u8 width, u8 height, RGBA bytes. Only tile numbers and positions live here; the pixels and the
palettes are read from the ROMs you pass, so nothing of the games is stored in this repository. Copy the folders next to the sprites:
/sdcard/SMZ3/sprites/{link,samus} on the Thor, sdmc:/SMZ3/sprites/{link,samus} on the 3DS."""
import os, sys

def rom_bytes(path):
    d = open(path, "rb").read()
    return d[0x200:] if len(d) % 0x8000 == 0x200 else d

def colour(w):
    return ((w & 31) * 8, ((w >> 5) & 31) * 8, ((w >> 10) & 31) * 8)

def tile(rom, off):
    b = rom[off:off + 32]
    px = []
    for y in range(8):
        p0, p1, p2, p3 = b[y * 2], b[y * 2 + 1], b[16 + y * 2], b[17 + y * 2]
        for x in range(8):
            bit = 7 - x
            px.append(((p0 >> bit) & 1) | (((p1 >> bit) & 1) << 1) | (((p2 >> bit) & 1) << 2) | (((p3 >> bit) & 1) << 3))
    return px

def draw(rom, pal, pieces, size):
    """pieces: (rom offset of the tile, flip bits 1 = horizontal 2 = vertical, x, y); an earlier piece keeps the pixels it drew."""
    img = {}
    for off, flip, dx, dy in pieces:
        t = tile(rom, off)
        for y in range(8):
            for x in range(8):
                v = t[(7 - y if flip & 2 else y) * 8 + (7 - x if flip & 1 else x)]
                if v and (x + dx, y + dy) not in img:
                    img[(x + dx, y + dy)] = v
    out = bytearray()
    for y in range(size[1]):
        for x in range(size[0]):
            v = img.get((x, y))
            out += bytes(pal[v] + (255,)) if v else bytes(4)
    return bytes([size[0], size[1]]) + bytes(out)

# Link standing and facing down: a 16x16 head (tiles 2, 3, 18, 19 of the sheet at 0x80000) and the body tiles.
LINK = [(2, 0, 0, 0), (3, 0, 8, 0), (18, 0, 0, 8), (19, 0, 8, 8), (54, 0, 0, 16), (54, 1, 8, 16), (38, 0, 0, 8), (39, 0, 8, 8),
        (317, 0, 1, 16), (46, 3, 8, 17)]
# Samus (power suit) standing: tile offsets into the JU ROM and their positions, 32x53.
SAMUS = [[998048, 0, 13, 12], [954464, 0, 13, 20], [997920, 0, 13, 4], [998112, 0, 5, 12], [998080, 0, 21, 12], [912224, 0, 8, 28],
         [997984, 0, 5, 4], [954592, 0, 5, 44], [997952, 0, 21, 4], [954560, 0, 1, 20], [954496, 0, 21, 20], [912128, 0, 16, 44],
         [911872, 0, 16, 36], [911872, 1, 9, 36], [954720, 0, 13, 28], [911968, 0, 8, 20], [954752, 0, 21, 28], [912192, 0, 0, 28],
         [912160, 0, 24, 44], [912096, 0, 10, 44]]

def main():
    sm, z3, out = rom_bytes(sys.argv[1]), rom_bytes(sys.argv[2]), sys.argv[3]
    zpal = [(0, 0, 0)] + [colour(z3[0xDD308 + i * 2] | z3[0xDD309 + i * 2] << 8) for i in range(15)]
    spal = [(0, 0, 0)] + [colour(sm[0xD9400 + i * 2] | sm[0xD9401 + i * 2] << 8) for i in range(1, 16)]
    link = draw(z3, zpal, [(0x80000 + t * 32, f, x, y) for t, f, x, y in LINK], (16, 24))
    samus = draw(sm, spal, SAMUS, (32, 53))
    for sub, data in (("link", link), ("samus", samus)):
        os.makedirs(os.path.join(out, sub), exist_ok=True)
        open(os.path.join(out, sub, "Original.pv"), "wb").write(data)
    print("wrote Original.pv for link and samus")

main()
