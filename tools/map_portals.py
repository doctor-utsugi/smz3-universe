#!/usr/bin/env python3
"""The combined map's portals (ALttP triforces on Zebes, Samus helmets on Hyrule) replaced with what the original games show.
Usage: map_portals.py <smz3_fullmap.png> <SM JU ROM> <ALttP light world PNG> <dark world PNG> (4096 px renders) -> data/map_patches.*"""
import json
import sys
from collections import Counter
from pathlib import Path

import numpy as np
from PIL import Image

# Black-outline boxes of the helmets and the triforces' boxes, in pixels of the full-size map.
HELMETS = [(490, 133, 530, 168), (603, 747, 642, 783), (90, 1705, 130, 1741), (858, 1682, 898, 1717)]
TRIFORCES = [(1649, 292, 1687, 325), (3023, 898, 3061, 931), (2132, 1410, 2171, 1443), (2610, 1763, 2648, 1796)]

BLACK, PURPLE, WALL, ORANGE = (0, 0, 0), (64, 0, 136), (125, 171, 171), (255, 114, 0)
HALF = 958                     # each ALttP world is 958 x 958 on the map, the light world on top
CAVE_MARK, CAVE = (0, 0, 192), (24, 24, 24)   # the renders number entrances in blue boxes; the map draws them dark
# Super Metroid's pause map tiles are 8 px and 32 px on this map; their grid starts at x 22, y 4 (mod 32). Each area's
# 64 x 32 tilemap sits at its origin, Crateria's east part 7 tiles further right (see core/ui/smz3_discovery.c).
SM_ORIGINS = [(982, 36), (886, 612), (1878, 1252), (2070, -284), (1782, 612)]
EAST_CRATERIA = 46


def grid(x, y):
    """The Zebes background: black with purple dotted lines every 32 pixels."""
    return PURPLE if (y % 32 < 4 and (x - 6) % 8 < 4) or ((x - 22) % 32 < 4 and y % 8 < 4) else BLACK


def sm_pause_map(rom, size):
    """Vanilla Super Metroid's pause map tilemaps drawn 4x onto a map-sized RGBA canvas, transparent where empty."""
    if len(rom) % 0x8000 == 512:
        rom = rom[512:]
    pc = lambda s: (s >> 1 & 0x3F8000) | (s & 0x7FFF)
    r16 = lambda s: rom[pc(s)] | rom[pc(s) + 1] << 8
    pal = [tuple((c >> s & 31) * 255 // 31 for s in (0, 5, 10)) for c in (r16(0xB6F000 + 2 * i) for i in range(256))]
    out = np.zeros((size[1], size[0], 4), np.uint8)
    for area, (ox, oy) in enumerate(SM_ORIGINS):
        p = pc(0x82964A) + 3 * area
        base = rom[p] | rom[p + 1] << 8 | rom[p + 2] << 16
        for i in range(2048):
            w = r16(base + 2 * i)
            tx, ty = i % 32 + (32 if i >= 1024 else 0), i % 1024 // 32
            gx = ox + 32 * (tx + (7 if area == 0 and tx >= EAST_CRATERIA else 0))
            gy = oy + 32 * ty
            b = pc(0xB68000) + (w & 0x3FF) * 32
            for y in range(8):
                sy = 7 - y if w & 0x8000 else y
                for x in range(8):
                    bit = 7 - (7 - x if w & 0x4000 else x)
                    c = sum((rom[b + o + sy * 2] >> bit & 1) << k for k, o in enumerate((0, 1, 16, 17)))
                    X, Y = gx + 4 * x, gy + 4 * y
                    if c and 0 <= X and 0 <= Y and X + 4 <= size[0] and Y + 4 <= size[1]:
                        out[Y:Y + 4, X:X + 4] = pal[(w >> 10 & 7) * 16 + c] + (255,)
    return out


def clean_triforce(F, V, b):
    """Redraws the map tiles the portal took (the triforce, the portal room and its dotted link) from the vanilla pause map
    in the map's own colours, with doors where two vanilla rooms meet; what is left of the icon round them likewise."""
    near = F[b[1] - 60:b[3] + 60, b[0] - 70:b[2] + 70]
    fill = next(c for c, _ in Counter(map(tuple, near.reshape(-1, 3))).most_common() if c not in (BLACK, PURPLE, WALL))
    room_c, wall_c = V["fill"], V["wall"]

    def sty(x, y):
        c = V["img"][y, x]
        if not c[3]:
            return grid(x, y)
        c = tuple(int(v) for v in c[:3])
        return WALL if c == wall_c else fill if c == room_c else c

    def tile(tx, ty):
        return np.array([[sty(x, y) for x in range(22 + 32 * tx, 54 + 32 * tx)] for y in range(4 + 32 * ty, 36 + 32 * ty)])

    def differs(tx, ty):
        return (np.abs(tile(tx, ty) - F[4 + 32 * ty:36 + 32 * ty, 22 + 32 * tx:54 + 32 * tx]).max(-1) > 0).mean() > 0.3

    out = F.copy()
    ty = ((b[1] + b[3]) // 2 - 4) // 32
    span = [tx for tx in range((b[0] - 22) // 32, (b[2] - 22) // 32 + 1) if min(b[2], 54 + 32 * tx) - max(b[0], 22 + 32 * tx) >= 8]
    ends = span[0], span[-1]
    for d in (-1, 1):   # the portal room beside the triforce differs from the vanilla tiles
        tx = ends[d > 0] + d
        while differs(tx, ty):
            span.append(tx)
            tx += d
    span.sort()
    x0, x1, y0 = 22 + 32 * span[0], 54 + 32 * span[-1], 4 + 32 * ty
    out[y0:y0 + 32, x0:x1] = np.concatenate([tile(tx, ty) for tx in span], 1)
    for e in [x0, x1] + [22 + 32 * tx for tx in span[1:]]:
        if sty(e - 6, y0 + 16) == fill and sty(e + 6, y0 + 16) == fill and WALL in (sty(e - 1, y0 + 16), sty(e, y0 + 16)):
            out[y0 + 6:y0 + 26, e - 3:e + 3] = ORANGE
    doors = np.zeros(F.shape[:2], bool)
    o = (F == ORANGE).all(-1)
    for y in range(b[1] - 3, b[3] + 4):
        for x in range(b[0] - 3, b[2] + 4):
            if o[y, x] and o[y - 6:y + 7, x].all() or o[y, x] and o[y, x - 6:x + 7].all():
                doors[y, x] = True
    for y in range(b[1] - 3, b[3] + 4):
        for x in range(b[0] - 3, b[2] + 4):
            inside = x0 <= x < x1 and y0 <= y < y0 + 32
            if not inside and not doors[y, x]:
                out[y, x] = sty(x, y)
    return out


def unlabel(a, x0, y0, x1, y1):
    """Paints the render's entrance labels (white numbers in a blue box) in the entrance's dark, within the box given."""
    blue = (a[y0:y1, x0:x1] == CAVE_MARK).all(-1)
    seen = np.zeros_like(blue)
    for sy, sx in zip(*np.nonzero(blue)):
        if seen[sy, sx]:
            continue
        stack, pts = [(sy, sx)], []
        seen[sy, sx] = True
        while stack:
            y, x = stack.pop()
            pts.append((y, x))
            for ny, nx in ((y + 1, x), (y - 1, x), (y, x + 1), (y, x - 1)):
                if 0 <= ny < blue.shape[0] and 0 <= nx < blue.shape[1] and blue[ny, nx] and not seen[ny, nx]:
                    seen[ny, nx] = True
                    stack.append((ny, nx))
        ys, xs = zip(*pts)
        box = a[y0 + min(ys):y0 + max(ys) + 1, x0 + min(xs):x0 + max(xs) + 1]
        box[(box == CAVE_MARK).all(-1) | (box.min(-1) >= 240)] = CAVE


def clean_helmet(F, world, b, yo):
    """The overworld render under the helmet, scaled to the map and its colours fitted to the map's round it."""
    x0, y0, x1, y1 = b[0] - 16, b[1] - 16, b[2] + 17, b[3] + 17
    sc = HALF / 4096
    a = np.array(world)
    unlabel(a, int(x0 / sc) - 8, int((y0 - yo) / sc) - 8, int(x1 / sc) + 8, int((y1 - yo) / sc) + 8)
    ref = np.array(Image.fromarray(a).resize((x1 - x0, y1 - y0), Image.BICUBIC,
                                box=((x0 + .5) / sc, (y0 - yo + .5) / sc, (x1 + .5) / sc, (y1 - yo + .5) / sc))).astype(float)
    t = F[y0:y1, x0:x1].astype(float)
    cx, cy = (b[0] + b[2]) / 2 - x0, (b[1] + b[3]) / 2 - y0
    rx, ry = (b[2] - b[0]) / 2, (b[3] - b[1]) / 2
    yy, xx = np.mgrid[0:y1 - y0, 0:x1 - x0]
    r = np.sqrt(((xx - cx) / rx) ** 2 + ((yy - cy) / ry) ** 2)   # 1 on the helmet's outline box
    ring = r > 1.35
    fit = np.empty_like(ref)
    for ch in range(3):
        A = np.stack([ref[..., ch][ring], np.ones(ring.sum())], 1)
        k, c = np.linalg.lstsq(A, t[..., ch][ring], rcond=None)[0]
        fit[..., ch] = ref[..., ch] * k + c
    alpha = np.clip((1.3 - r) / 0.2, 0, 1)[..., None]
    out = F.copy()
    out[y0:y1, x0:x1] = np.clip(np.rint(alpha * fit + (1 - alpha) * t), 0, 255)
    return out


def build(fullmap, sm_rom, light, dark):
    img = Image.open(fullmap).convert("RGB")
    F = np.array(img).astype(int)
    vimg = sm_pause_map(Path(sm_rom).read_bytes(), img.size)
    colours = Counter(tuple(int(v) for v in p[:3]) for p in vimg.reshape(-1, 4)[::7] if p[3])
    room, wall = [c for c, _ in colours.most_common(2)]
    V = {"img": vimg, "fill": room, "wall": wall}
    worlds = [Image.open(path).convert("RGB") for path in (light, dark)]
    out = []
    for b in HELMETS + TRIFORCES:
        if b in HELMETS:
            dark_half = b[1] >= HALF
            new = clean_helmet(F, worlds[dark_half], b, HALF if dark_half else 0)
        else:
            new = clean_triforce(F, V, b)
        ys, xs = np.nonzero((new != F).any(-1))
        x0, y0, x1, y1 = xs.min(), ys.min(), xs.max() + 1, ys.max() + 1
        out.append((int(x0), int(y0), int(x1 - x0), int(y1 - y0), Image.fromarray(new[y0:y1, x0:x1].astype(np.uint8))))
    return out


if __name__ == "__main__":
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    crops = build(*sys.argv[1:])
    data = Path(__file__).resolve().parents[1] / "data"
    strip = Image.new("RGB", (sum(c[2] for c in crops), max(c[3] for c in crops)))
    rects, sx = [], 0
    for x, y, w, h, crop in crops:
        strip.paste(crop, (sx, 0))
        rects.append([x, y, w, h, sx])
        sx += w
    strip.save(data / "map_patches.png", optimize=True)
    (data / "map_patches.json").write_text(json.dumps(rects) + "\n")
    print(len(rects), "map patches written to", data)
