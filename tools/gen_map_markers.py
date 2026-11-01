#!/usr/bin/env python3
"""Joins data/locations.json with the tracker's maps.json to place every location on the SMZ3 combined map.

Writes data/map_markers.json: one {index, x, y} per location, in pixels of smz3_fullmap.png (3246x1916)."""
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
locs = json.loads((root / "data/locations.json").read_text())
maps = json.loads((root / "vendor/SMZ3Randomizer/src/TrackerCouncil.Smz3.Data/maps.json").read_text())
combined = next(m for m in maps["Maps"] if m["Image"] == "smz3_fullmap.png")
map_regions = {r["Name"]: r for r in combined["Regions"]}
regions = {r["Name"]: r for r in maps["Regions"]}

markers, missing = [], []
for i, loc in enumerate(locs):
    region = regions.get(loc["region_name"])
    placement = map_regions.get(loc["region_name"])
    room = None
    if region and placement:
        for cand in (loc["name"], loc["room"], loc["region_name"]):
            room = next((r for r in region["Rooms"] if r["Name"] == cand), None)
            if room:
                break
    if not room:
        missing.append((i, loc["game"], loc["region_name"], loc["name"], loc["room"]))
        continue
    scale = placement.get("Scale") or 1
    markers.append({"index": i,
                    "x": int(room["X"] * room.get("Scale", 1) * scale + placement["X"]),
                    "y": int(room["Y"] * room.get("Scale", 1) * scale + placement["Y"])})

(root / "data/map_markers.json").write_text(json.dumps(markers) + "\n")
print(len(markers), "placed,", len(missing), "missing")
for m in missing[:40]:
    print(" ", m)
