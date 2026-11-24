#!/usr/bin/env python3
"""The combined map without its portals, for the single-game modes: the four ALttP triforces of the Zebes half and the four
Samus helmets of the Hyrule half are painted over with what is around them.

patches(img) -> [(x, y, w, h, crop)]: the changed rectangles of the full-size map (smz3_fullmap.png) and their clean pixels.
tools/prepare_assets.py writes them as map_patches.png (the crops side by side) and map_patches.json ([x, y, w, h, sx] each,
sx the crop's place in the strip); the hosts paint them over the map in A Link to the Past and Super Metroid runs."""
from collections import Counter

# Black-outline boxes of the helmets and the triforces' boxes, in pixels of the full-size map.
HELMETS = [(490, 133, 530, 168), (603, 747, 642, 783), (90, 1705, 130, 1741), (858, 1682, 898, 1717)]
TRIFORCES = [(1649, 292, 1687, 325), (3023, 898, 3061, 931), (2132, 1410, 2171, 1443), (2610, 1763, 2648, 1796)]

PURPLE, BLACK, CYAN, RED = (64, 0, 136), (0, 0, 0), (125, 171, 171), (212, 32, 32)
ROOM = {BLACK, PURPLE, CYAN, RED, (216, 56, 144), (40, 56, 136), (41, 57, 140), (18, 199, 4)}


def grid(x, y):
    """The Zebes background: black with purple dotted lines every 32 pixels."""
    return PURPLE if (y % 32 < 4 and (x - 6) % 8 < 4) or ((x - 22) % 32 < 4 and y % 8 < 4) else BLACK


def peel(px, mask):
    """Fills the mask from its edge inwards, each pixel taking the commonest of its known neighbours (no new colours)."""
    mask = set(mask)
    while mask:
        nxt = {}
        for (x, y) in sorted(mask):
            nb = [px[x + dx, y + dy] for dy in (-1, 0, 1) for dx in (-1, 0, 1) if (dx or dy) and (x + dx, y + dy) not in mask]
            if len(nb) >= 2:
                nxt[(x, y)] = Counter(nb).most_common(1)[0][0]
        if not nxt:
            nxt = {p: px[p[0] - 1, p[1]] for p in mask}
        for p, c in nxt.items():
            px[p] = c
        mask -= set(nxt)


def helmet(px, b, pad=2.5):
    cx, cy, rx, ry = (b[0] + b[2]) / 2, (b[1] + b[3]) / 2, (b[2] - b[0]) / 2 + pad, (b[3] - b[1]) / 2 + pad
    peel(px, {(x, y) for x in range(b[0] - 4, b[2] + 5) for y in range(b[1] - 4, b[3] + 5) if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1})


def triforce(px, b):
    """Each row from the first to the last pixel that is no map colour; then every 4 x 4 cell of the map (cells start at x 2
    mod 4) takes the commonest colour of its uncovered pixels, or the background grid when it is all covered."""
    x0, y0, x1, y1 = b
    m = set()
    for y in range(y0 - 1, y1 + 2):
        xs = [x for x in range(x0 - 2, x1 + 3) if px[x, y] not in ROOM]
        if not xs:
            continue
        a, z = min(xs), max(xs)
        if px[a - 1, y] == RED and px[a - 2, y] in (BLACK, PURPLE): a -= 1   # the triforce's red rim, not a Norfair room
        if px[z + 1, y] == RED and px[z + 2, y] in (BLACK, PURPLE): z += 1
        m |= {(x, y) for x in range(a, z + 1)}
    cells = {}
    for (x, y) in m:
        cells.setdefault(((x - 2) // 4, y // 4), []).append((x, y))
    for (cx, cy), pts in sorted(cells.items()):
        known = [px[x, y] for y in range(cy * 4, cy * 4 + 4) for x in range(cx * 4 + 2, cx * 4 + 6) if (x, y) not in m]
        for (x, y) in pts:
            px[x, y] = Counter(known).most_common(1)[0][0] if known else grid(x, y)


def patches(img):
    img = img.convert("RGB")
    clean = img.copy()
    px = clean.load()
    for b in HELMETS:
        helmet(px, b)
    for b in TRIFORCES:
        triforce(px, b)
    out = []
    for b in HELMETS + TRIFORCES:   # each icon's changed pixels, boxed
        x0, y0, x1, y1 = b[0] - 6, b[1] - 6, b[2] + 7, b[3] + 7
        a, o = clean.crop((x0, y0, x1, y1)), img.crop((x0, y0, x1, y1))
        diff = [(x, y) for y in range(a.height) for x in range(a.width) if a.getpixel((x, y)) != o.getpixel((x, y))]
        if not diff:
            continue
        l, t = min(x for x, _ in diff), min(y for _, y in diff)
        r, bt = max(x for x, _ in diff) + 1, max(y for _, y in diff) + 1
        out.append((x0 + l, y0 + t, r - l, bt - t, clean.crop((x0 + l, y0 + t, x0 + r, y0 + bt))))
    return out
