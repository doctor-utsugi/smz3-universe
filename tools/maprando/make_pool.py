#!/usr/bin/env python3
"""Builds the Map Rando seeds that ship inside the game, for devices that cannot build one and are offline.

make_pool.py <vanilla SM ROM> <out folder> [per preset]

Each seed is a BPS patch from the headerless vanilla ROM, made by the same tool the server runs
(core/maprando, binary smz3-maprando). The folder holds pool.txt (one line per seed: preset, seed, info JSON) and
<line>.zip, one deflated patch each, so a console reads only the seed it plays (core/ui/smz3_sm.c).
"""
import json
import os
import random
import subprocess
import sys
import tempfile
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOOL = os.path.join(ROOT, 'build', 'maprando-target', 'release', 'smz3-maprando')
DATA = os.path.join(ROOT, 'build', 'maprando-data')
# upstream's preset names, as core/ui/smz3_ui_opts.c MR_PRESETS sends them
PRESETS = ['Default', 'Community Race Season 5', 'Mentor Tournament', 'Summer Series Expert Challenge']


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    rom, out = sys.argv[1], sys.argv[2]
    per = int(sys.argv[3]) if len(sys.argv) > 3 else 8
    lines, patches = [], []
    rng = random.SystemRandom()
    with tempfile.TemporaryDirectory() as tmp:
        for preset in PRESETS:
            settings = json.dumps({'preset': preset}, separators=(',', ':'))
            made = 0
            while made < per:
                seed = rng.randrange(1, 10**12)
                path = os.path.join(tmp, 'seed.bps')
                r = subprocess.run([TOOL, DATA, rom, settings, str(seed), path, '--bps'], capture_output=True, text=True)
                if r.returncode:
                    print(f'{preset} {seed}: no seed, another one', file=sys.stderr)
                    continue
                info = r.stdout.strip().splitlines()[-1]
                json.loads(info)
                with open(path, 'rb') as f:
                    patches.append(f.read())
                lines.append(f'{preset}\t{seed}\t{info}')
                made += 1
                print(f'{preset}: {made}/{per}', file=sys.stderr)
    os.makedirs(out, exist_ok=True)
    for i, p in enumerate(patches):
        with zipfile.ZipFile(os.path.join(out, f'{i}.zip'), 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            z.writestr('seed.bps', p)
    with open(os.path.join(out, 'pool.txt'), 'w') as f:
        f.write('\n'.join(lines) + '\n')
    print(f'{out}: {len(patches)} seeds', file=sys.stderr)


if __name__ == '__main__':
    main()
