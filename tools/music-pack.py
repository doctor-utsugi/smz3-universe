#!/usr/bin/env python3
"""Renders a curated MSU-1 music pack (a JSON slot -> game/title map) from a .spc library.

usage: music-pack.py <pack.json> <spc library dir> <output dir> [seconds]

Each library subfolder is a game; a track is found by its ID666 title. Output files are "<pack>-<slot>.pcm"
(MSU1 header, loop point 0, 44.1 kHz 16-bit stereo, 3 s fade), which the app links by slot number.
"""
import glob, json, os, struct, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

pack_file, lib, out = sys.argv[1:4]
seconds = int(sys.argv[4]) if len(sys.argv) > 4 else 150
pack = json.load(open(pack_file))
os.makedirs(out, exist_ok=True)

def title(path):
    with open(path, "rb") as f: b = f.read(0x100)
    return b[0x2E:0x4E].split(b"\0")[0].decode("latin1").strip()

def find(t):
    for p in sorted(glob.glob(os.path.join(lib, t["folder"], "**", "*.spc"), recursive=True)):
        if title(p) == t["title"]: return p

def duration(path):
    r = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", path], capture_output=True, text=True)
    try: return float(r.stdout.strip())
    except ValueError: return 0.0

def render(t):
    src = find(t)
    if not src: return "missing %s / %s" % (t["folder"], t["title"])
    length = min(seconds, max(duration(src), 45))
    p = subprocess.run(["ffmpeg", "-v", "error", "-y", "-stream_loop", "-1", "-i", src, "-t", str(length), "-af", "afade=t=out:st=%f:d=3" % (length - 3),
                        "-ar", "44100", "-ac", "2", "-f", "s16le", "-"], capture_output=True)
    if p.returncode or len(p.stdout) < 44100 * 4 * 20: return "failed %s" % t["title"]
    name = pack["name"].lower().replace(" ", "-")
    with open(os.path.join(out, "%s-%d.pcm" % (name, t["slot"])), "wb") as f: f.write(b"MSU1" + struct.pack("<I", 0) + p.stdout)

with ThreadPoolExecutor(8) as ex: errs = [e for e in ex.map(render, pack["tracks"]) if e]
print("rendered %d of %d tracks into %s" % (len(pack["tracks"]) - len(errs), len(pack["tracks"]), out))
for e in errs: print(e)
