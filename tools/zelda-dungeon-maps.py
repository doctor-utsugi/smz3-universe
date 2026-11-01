#!/usr/bin/env python3
"""Renders the game's own dungeon maps (the screen the pause menu shows) from an ALttP ROM.

Output, next to --out: dungeon_maps.png / dungeon_maps_hint.png (one 80x80 floor per cell, a dungeon per row,
floors left to right: every room fully drawn / the dimmed outline the map item reveals) and dungeon_maps.json
(floors below and above ground and the 5x5 room grid of each floor).

The layout follows the game's map routine as documented by the zelda3 reimplementation (MIT).
Usage: zelda-dungeon-maps.py <alttp rom> <out dir>
"""
import json
import os
import struct
import sys

from PIL import Image

PACKS = {97: 0x189610, 98: 0x189857, 99: 0x189b24, 80: 0x17cd55, 86: 0x17e842, 87: 0x17eb38, 51: 0x16c13d}
PACK_SETS = [[97, 98, 99, 80], [97, 86, 87, 80], [97, 86, 99, 80], [97, 86, 51, 80]]
DUNGEON_SET = [0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 2, 1, 3, 1]
FLOORS = [0x21, 0x23, 0x20, 0x21, 0x70, 0x12, 0x11, 0x12, 0x02, 0x17, 0x60, 0x12, 0x13, 0x71]
SIZES = [75, 125, 50, 75, 175, 75, 50, 75, 50, 200, 150, 75, 100, 200]
ROOM = 16
FLOOR_PX = 5 * ROOM


class Rom:
    def __init__(self, path):
        self.d = open(path, 'rb').read()

    def off(self, ea):
        return ((ea >> 16) & 0x7f) * 0x8000 + (ea & 0x7fff)

    def byte(self, ea):
        return self.d[self.off(ea)]

    def bytes(self, ea, n):
        out = bytearray()
        for _ in range(n):
            out.append(self.byte(ea))
            ea += 1
            if not ea & 0x8000:
                ea += 0x8000
        return bytes(out)

    def word(self, ea):
        return self.byte(ea) | self.byte(ea + 1) << 8


def decompress(rom, ea):
    out = bytearray()

    def take(n=1):
        nonlocal ea
        got = rom.bytes(ea, n)
        ea += n
        if not ea & 0x8000:
            ea += 0x8000
        return got

    while True:
        cmd = take()[0]
        if cmd == 0xff:
            return bytes(out)
        if cmd & 0xe0 != 0xe0:
            n = (cmd & 0x1f) + 1
            cmd &= 0xe0
        else:
            n = take()[0] + ((cmd & 3) << 8) + 1
            cmd = (cmd << 3) & 0xe0
        if cmd == 0:
            out += take(n)
        elif cmd & 0x80:
            lo, hi = take(2)
            o = lo | hi << 8
            for _ in range(n):
                out.append(out[o]); o += 1
        elif not cmd & 0x40:
            out += take()[:1] * n
        elif not cmd & 0x20:
            lo, hi = take(2)
            for i in range(n):
                out.append(lo if i % 2 == 0 else hi)
        else:
            v = take()[0]
            for _ in range(n):
                out.append(v & 0xff); v += 1


def tiles_for(rom, pack_set):
    tiles = []
    for pk in PACK_SETS[pack_set]:
        buf = decompress(rom, PACKS[pk])
        for t in range(64):
            src = buf[t * 24:(t + 1) * 24]
            px = [[0] * 8 for _ in range(8)]
            for r in range(8):
                for x in range(8):
                    bit = 7 - x
                    px[r][x] = ((src[r * 2] >> bit) & 1) | (((src[r * 2 + 1] >> bit) & 1) << 1) | (((src[16 + r] >> bit) & 1) << 2)
            tiles.append(px)
    return tiles


def colour(w):
    return ((w & 31) * 255 // 31, ((w >> 5) & 31) * 255 // 31, ((w >> 10) & 31) * 255 // 31, 255)


def room_chests(rom, room):
    """Tile positions (x, y in the room's 64 x 64 tiles) of the chests of a room, in the order the game numbers them.

    The room's object stream is the game's own: floor and layout bytes, then three layers of 3-byte objects ending in 0xFFFF
    (chests are the type 3 objects 0x19 and 0x31), then the doors.
    """
    ptr = rom.byte(0x1F8000 + room * 3) | rom.byte(0x1F8000 + room * 3 + 1) << 8 | rom.byte(0x1F8000 + room * 3 + 2) << 16
    p = ptr + 2
    out, layer = [], 0
    while layer < 3:
        w = rom.byte(p) | rom.byte(p + 1) << 8
        if w == 0xFFFF:
            layer += 1; p += 2
            continue
        if w == 0xFFF0:
            break
        b1, b2, b3 = rom.byte(p), rom.byte(p + 1), rom.byte(p + 2)
        p += 3
        if b1 & 0xfc != 0xfc and b3 >= 0xf8:
            idx = (b3 & 7) << 4 | (b2 & 3) << 2 | (b1 & 3)
            if idx in (0x19, 0x31):
                out.append([b1 >> 2, b2 >> 2])
    return out


def find_pieces(rom):
    key = struct.pack('<8H', 0xb61, 0x5361, 0x8b61, 0x8b62, 0xb60, 0xb63, 0x8b60, 0xb64)
    i = rom.d.find(key)
    if i < 0:
        sys.exit('room piece table not found in this ROM')
    return list(struct.unpack('<744H', rom.d[i:i + 744 * 2]))


def main():
    rom, out = Rom(sys.argv[1]), sys.argv[2]
    os.makedirs(out, exist_ok=True)
    pieces = find_pieces(rom)
    pal = [rom.word(0x9BE544 + 2 * i) for i in range(96)]
    layouts, shapes = [], []
    for d in range(14):
        layout = rom.bytes(0xA0000 + rom.word(0x8AF605 + d * 2), SIZES[d])
        layouts.append(layout)
        shapes.append(rom.bytes(0xA0000 + rom.word(0x8AFBE4 + d * 2), len(layout) - layout.count(0xf)))

    cols = 8
    atlas = [Image.new('RGBA', (cols * FLOOR_PX, 14 * FLOOR_PX), (0, 0, 0, 255)) for _ in range(2)]
    meta = []
    for d in range(14):
        tiles = tiles_for(rom, DUNGEON_SET[d])
        below, above = FLOORS[d] & 0xf, FLOORS[d] >> 4
        if below == 0 and above > 1 and all(v == 0xf for v in layouts[d][:25]):   # Castle Tower's table starts with an empty floor under 1F
            layouts[d] = layouts[d][25:]
            above -= 1
        floors = []
        for f in range(below + above):
            grid = list(layouts[d][f * 25:(f + 1) * 25])
            floors.append(grid)
            for mode in (0, 1):
                img = atlas[mode]
                for cell, v in enumerate(grid):
                    if v == 0xf:
                        yv = 0x51
                    else:
                        count = sum(1 for b in layouts[d][:layouts[d].index(v)] if b != 0xf)
                        yv = shapes[d][count] if count < len(shapes[d]) else 0x51
                    for q in range(4):
                        org = pieces[yv * 4 + q]
                        if mode == 1:
                            if org == 0xb00:
                                continue
                            av = (org & ~0x1c00) | 0xc00 if org & 0x1000 else org + 0x400
                        else:
                            av = org
                        t = (av & 0x3ff) - 0x300
                        p = (av >> 10) & 7
                        if t < 0 or t >= 256 or p < 2:
                            continue
                        x0 = f * FLOOR_PX + (cell % 5) * ROOM + (q & 1) * 8
                        y0 = d * FLOOR_PX + (cell // 5) * ROOM + (q >> 1) * 8
                        for y in range(8):
                            sy = 7 - y if av & 0x8000 else y
                            for x in range(8):
                                sx = 7 - x if av & 0x4000 else x
                                v2 = tiles[t][sy][sx]
                                if v2:
                                    img.putpixel((x0 + x, y0 + y), colour(pal[(p - 2) * 16 + v2]))
        meta.append({'below': below, 'above': above, 'floors': floors})
    atlas[0].save(os.path.join(out, 'dungeon_maps.png'))
    atlas[1].save(os.path.join(out, 'dungeon_maps_hint.png'))
    chests = {}
    for d in range(14):
        for grid in meta[d]['floors']:
            for room in grid:
                if room != 0xF and str(room) not in chests:
                    found = room_chests(rom, room)
                    if found:
                        chests[str(room)] = found
    json.dump({'cell': FLOOR_PX, 'room': ROOM, 'dungeons': meta, 'chests': chests}, open(os.path.join(out, 'dungeon_maps.json'), 'w'))


main()
