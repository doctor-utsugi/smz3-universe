#!/usr/bin/env python3
"""Reads the Super Metroid ROM: room headers (area, map position, size) and which room holds each item PLM."""
import json
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
rom = (root / "rom/supermetroid_ju.sfc").read_bytes()


def off(bank, addr):
    return (bank - 0x80) * 0x8000 + (addr & 0x7FFF)


def u16(bank, addr):
    o = off(bank, addr)
    return rom[o] | rom[o + 1] << 8


rooms = {}
queue = [0x91F8]
while queue:
    ptr = queue.pop()
    if ptr in rooms:
        continue
    o = off(0x8F, ptr)
    h = rom[o:o + 11]
    rooms[ptr] = {"ptr": ptr, "index": h[0], "area": h[1], "map_x": h[2], "map_y": h[3], "w": h[4], "h": h[5]}
    # door list
    dl = h[9] | h[10] << 8
    p = dl
    while True:
        try:
            door = u16(0x8F, p)
        except IndexError:
            break
        if not (0x8000 <= door <= 0xFFFF) or len(rooms) > 400:
            break
        # a door pointer list ends where the next word is no longer a bank-83 pointer
        if door < 0x8800 or door > 0xAC00:
            break
        dest = u16(0x83, door)
        if 0x8000 <= dest < 0xFFFF:
            d = rom[off(0x8F, dest):off(0x8F, dest) + 6]
            if d[1] <= 7 and d[4] and d[5] and d[4] <= 20 and d[5] <= 20 and dest not in rooms:
                queue.append(dest)
        p += 2
        if p - dl > 60:
            break
    # states -> PLM sets
    s = 11
    plm_sets = []
    while True:
        code = rom[o + s] | rom[o + s + 1] << 8
        if code == 0xE5E6:
            state = o + s + 2
            plm_sets.append(rom[state + 20] | rom[state + 21] << 8)
            break
        arg = {0xE612: 1, 0xE629: 1, 0xE5EB: 1}.get(code, 0)
        ptr_o = o + s + 2 + arg
        sp = rom[ptr_o] | rom[ptr_o + 1] << 8
        plm_sets.append(rom[off(0x8F, sp) + 20] | rom[off(0x8F, sp) + 21] << 8)
        s += 2 + arg + 2
        if s > 80:
            break
    rooms[ptr]["plm_sets"] = sorted(set(plm_sets))

print(len(rooms), "rooms", file=sys.stderr)

locs = json.loads((root / "data/locations.json").read_text())
found = {}
for loc in locs:
    if loc["game"] != "metroid":
        continue
    # address comes from the C# region files; read it from the source table written by gen step
    pass
json.dump(list(rooms.values()), open(root / "data/sm_rooms.json", "w"))
