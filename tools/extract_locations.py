#!/usr/bin/env python3
"""Extracts every SMZ3 location (id, name, region, room, game, autotracking address/flag) from the Cas' tracker sources into data/locations.json."""
import json, re, sys
from pathlib import Path

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1] / "vendor/SMZ3Randomizer"
regions_dir = root / "src/TrackerCouncil.Smz3.Data/WorldData/Regions"
out = Path(__file__).resolve().parents[1] / "data/locations.json"

class_re = re.compile(r"public (?:abstract )?class (\w+)")
region_name_re = re.compile(r'override string Name\s*=>\s*"([^"]*)"')
area_re = re.compile(r'override string Area\s*=>\s*"([^"]*)"')
room_re = re.compile(r'base\(region,\s*"([^"]*)"')
id_re = re.compile(r"LocationId\.(\w+),\s*(0x[0-9A-Fa-f]+|\d+),\s*LocationType\.(\w+)")
name_re = re.compile(r'name:\s*"((?:[^"\\]|\\.)*)"')
addr_re = re.compile(r"memoryAddress:\s*(0x[0-9A-Fa-f]+|\d+)")
flag_re = re.compile(r"memoryFlag:\s*(0x[0-9A-Fa-f]+|\d+)")
mtype_re = re.compile(r"memoryType:\s*LocationMemoryType\.(\w+)")
vanilla_re = re.compile(r"vanillaItem:\s*ItemType\.(\w+)")


def call_body(text, start):
    """Returns the text inside the parentheses that open at text[start]."""
    depth = 0
    for i in range(start, len(text)):
        c = text[i]
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return text[start + 1:i]
    return text[start + 1:]


locations = []
for path in sorted(regions_dir.rglob("*.cs")):
    text = path.read_text(encoding="utf-8-sig")
    game = "zelda" if "/Zelda/" in path.as_posix() else "metroid"
    cls = class_re.search(text)
    region_cls = cls.group(1) if cls else path.stem
    rn, ar = region_name_re.search(text), area_re.search(text)
    for m in re.finditer(r"new Location\(", text):
        body = call_body(text, m.end() - 1)
        idm = id_re.search(body)
        if not idm:
            continue
        before = text[:m.start()]
        rooms = list(room_re.finditer(before))
        n, a, f, t, v = (r.search(body) for r in (name_re, addr_re, flag_re, mtype_re, vanilla_re))
        locations.append({
            "id": idm.group(1),
            "game": game,
            "region": region_cls,
            "region_name": rn.group(1) if rn else region_cls,
            "area": ar.group(1) if ar else None,
            "room": rooms[-1].group(1) if rooms else None,
            "address": int(idm.group(2), 0),
            "type": idm.group(3),
            "name": n.group(1) if n else idm.group(1),
            "vanilla": v.group(1) if v else None,
            "mem_addr": int(a.group(1), 0) if a else None,
            "mem_flag": int(f.group(1), 0) if f else None,
            "mem_type": t.group(1) if t else "Default",
        })

out.write_text(json.dumps(locations, indent=1) + "\n")
z = sum(l["game"] == "zelda" for l in locations)
print(f"{len(locations)} locations ({z} zelda, {len(locations) - z} metroid); "
      f"{sum(l['mem_addr'] is not None for l in locations)} with autotracking memory; "
      f"{len({l['id'] for l in locations})} unique ids")
