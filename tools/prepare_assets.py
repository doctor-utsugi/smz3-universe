#!/usr/bin/env python3
"""Copies the tracker icons the bottom screen uses from vendor/SMZ3CasSprites into the Lemuroid assets.

The sprite repository has no license, so the result stays out of git (vendor/ is ignored)."""
import re
import shutil
from pathlib import Path

root = Path(__file__).resolve().parents[1]
src = root / "vendor/SMZ3CasSprites/Sprites/Items"
dst = root / "vendor/Lemuroid/lemuroid-app/src/main/assets/smz3/items"
dst.mkdir(parents=True, exist_ok=True)

icons = set(re.findall(r'"([^"]+\.png)"', (root / "core/smz3_inventory.c").read_text()))
for icon in sorted(icons):
    shutil.copyfile(src / icon, dst / icon.replace(" ", "_"))
print(len(icons), "icons copied to", dst)
for own in sorted((root / "data/icons").glob("*.png")):   # icons drawn for this project (the sprite set has no ALttP bomb)
    shutil.copyfile(own, dst / own.name)

# Combined SMZ3 map at half size plus the marker positions (halved to match).
import json
from PIL import Image

maps = root / "vendor/SMZ3CasSprites/Sprites/Maps/smz3_fullmap.png"
assets = root / "vendor/Lemuroid/lemuroid-app/src/main/assets/smz3"
img = Image.open(maps).convert("RGB")
# Full size and lossless: the thin Super Metroid lines blur when the map is halved and compressed with loss. The tracker still
# works in half-size coordinates and scales the picture into them (Smz3MapView.MAP_W/MAP_H).
(assets / "map.webp").unlink(missing_ok=True)
img.save(assets / "map.png", optimize=True)
markers = json.loads((root / "data/map_markers.json").read_text())
(assets / "map_markers.json").write_text(json.dumps(
    [[m["index"], m["x"] // 2, m["y"] // 2] for m in markers]))
print("map", img.size, "(half-size coordinates)", len(markers), "markers")


# Super Metroid room rectangles (from the user's own ROM via tools/sm_rooms.py) and ALttP dungeon centres,
# both in pixels of the half-size map, for the discovered-area view and the position marker.
sm_rooms = json.loads((root / "data/sm_rooms.json").read_text())
(assets / "sm_rooms.json").write_text(json.dumps(
    [[r["ptr"], r["area"], r["map_x"], r["map_y"], r["w"], r["h"]] for r in sm_rooms]))
locs = json.loads((root / "data/locations.json").read_text())
centres = {}
for m in markers:
    loc = locs[m["index"]]
    if loc["game"] == "zelda":
        centres.setdefault(loc["region_name"], []).append((m["x"] // 2, m["y"] // 2))
(assets / "z3_regions.json").write_text(json.dumps(
    {name: [min(x for x, _ in pts), min(y for _, y in pts), max(x for x, _ in pts), max(y for _, y in pts)]
     for name, pts in centres.items()}))
print("rooms", len(sm_rooms), "z3 regions", len(centres))

# Boss and reward icons for the map
rewards_src = root / "vendor/SMZ3CasSprites/Sprites/BossRewards"
rewards_dst = assets / "rewards"
rewards_dst.mkdir(exist_ok=True)
for name in ("unknown", "kraid boss token", "phantoon boss token", "draygon boss token", "ridley boss token"):
    shutil.copyfile(rewards_src / (name + ".png"), rewards_dst / (name.replace(" ", "_") + ".png"))
print("reward icons copied")

# World goal icons that the sprite set has; the other goals draw a sprite cut from the player's own ROM
items_src = root / "vendor/SMZ3CasSprites/Sprites/Items"
goals_dst = assets / "goals"
goals_dst.mkdir(exist_ok=True)
GOAL_ICONS = {"boss_kraid": "kraid", "boss_phantoon": "phantoon", "boss_draygon": "draygon", "boss_ridley": "ridley",
              "boss_sporespawn": "spore spawn", "boss_crocomire": "crocomire", "boss_botwoon": "botwoon", "boss_motherbrain": "mother brain",
              "boss_agahnim": "../Dungeons/agahnim", "boss_goldentorizo": "bomb torizo",
              "zelda_rupee": "twenty rupees", "shared_items": "chest", "shared_portal": "other game item", "shared_daily": "important other game item"}
for key, name in GOAL_ICONS.items():
    shutil.copyfile(items_src / (name + ".png"), goals_dst / (key + ".png"))
print(len(GOAL_ICONS), "goal icons copied")

# IPS patches the on-device generator applies, and the options panel description (gen-cli schema)
ips_src = root / "vendor/SMZ3Randomizer/src/TrackerCouncil.Smz3.SeedGenerator/FileData/IpsPatches"
ips_dst = assets / "ips"
ips_dst.mkdir(exist_ok=True)
rom_c = (root / "core/gen/smz3_rom.c").read_text()
names = ["zsm.ips"] + re.findall(r'"([^"]+\.ips)"', rom_c)
for name in dict.fromkeys(names):
    shutil.copyfile(ips_src / name, ips_dst / name)
print(len(set(names)), "IPS patches copied")
schema = root / "build/schema.json"
if schema.exists():
    shutil.copyfile(schema, assets / "schema.json")
    print("schema copied")

# Country flags of the profile and the ladder (assets/flags, made by tools/gen_flags.py)
flags_dst = assets / "flags"
flags_dst.mkdir(exist_ok=True)
for f in sorted((root / "assets/flags").glob("*.png")):
    shutil.copyfile(f, flags_dst / f.name)
print("flags copied")

# The app logo beside the menu titles
import subprocess, sys
subprocess.run([sys.executable, str(root / "tools/gen_pc_logo.py"), str(root / "assets/logo.png"), str(assets / "logo.png"), "96"], check=True)
print("logo written")

# The menu font the PC build uses too (Chakra Petch, OFL)
shutil.copyfile(root / "tools/fonts/ChakraPetch-Bold.ttf", assets / "menu_font.ttf")

# The community sprites (assets/sprites), so a phone has them without copying a folder to the SD card
sprites_dst = assets / "sprites"
shutil.rmtree(sprites_dst, ignore_errors=True)
shutil.copytree(root / "assets/sprites", sprites_dst)
print("sprites copied")
