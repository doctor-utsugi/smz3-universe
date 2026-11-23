#!/usr/bin/env python3
"""Renders a random bank of SNES music (.spc files) to MSU-1 PCM tracks.

usage: music-bank.py <spc library dir> <output dir> [count] [seconds]

Each output file is "MSU1" + a 4-byte loop point (0 = loop the whole track) + 44.1 kHz 16-bit stereo PCM, with a short
fade at the end. Needs ffmpeg built with libgme. Tracks shorter than 45 s (jingles, fanfares) are skipped.
"""
import os, random, subprocess, sys, struct
from concurrent.futures import ThreadPoolExecutor

lib, out = sys.argv[1], sys.argv[2]
count = int(sys.argv[3]) if len(sys.argv) > 3 else 120
seconds = int(sys.argv[4]) if len(sys.argv) > 4 else 150
os.makedirs(out, exist_ok=True)

def duration(path):
    r = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", path],
                       capture_output=True, text=True)
    try: return float(r.stdout.strip())
    except ValueError: return 0.0

def render(item):
    n, path = item
    dest = os.path.join(out, "%04d.pcm" % n)
    length = min(seconds, duration(path))
    p = subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", path, "-t", str(length), "-af", "afade=t=out:st=%f:d=3" % max(0, length - 3),
                        "-ar", "44100", "-ac", "2", "-f", "s16le", "-"], capture_output=True)
    if p.returncode or len(p.stdout) < 44100 * 4 * 20: return None
    with open(dest, "wb") as f: f.write(b"MSU1" + struct.pack("<I", 0) + p.stdout)
    return dest

files = [os.path.join(d, f) for d, _, fs in os.walk(lib) for f in fs if f.lower().endswith(".spc")]
random.shuffle(files)
good = []
with ThreadPoolExecutor(8) as ex:
    for path, d in zip(files[:count * 3], ex.map(duration, files[:count * 3])):
        if d >= 45: good.append(path)
        if len(good) >= count: break
    done = [r for r in ex.map(render, enumerate(good)) if r]
print("rendered %d tracks into %s" % (len(done), out))
