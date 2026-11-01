#!/usr/bin/env python3
"""Compares the C patcher with the C# one: the same offsets must get the same bytes, except where the C# is random."""
import json, os, random, subprocess, sys
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
n = int(sys.argv[1]) if len(sys.argv) > 1 else 10
rnd = random.Random(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
KEYS = {"None": 0, "Zelda": 1, "SuperMetroid": 2, "Both": 3}
RULES = {"Anywhere": 0, "DungeonsAndMetroid": 1, "CrystalDungeonsAndMetroid": 2}
SKIP = ("ZeldaTextsPatch",)                            # texts are a separate stage
GUID_OFFSETS = (0xFF60, 0xFF80)                        # random guids (the C port derives them from the seed)
JINGLES = json.load(open(root + "/build/jingle_names.json"))
cases = [{"seed": str(rnd.randint(1, 2**31 - 2)), "keysanity": rnd.choice(list(KEYS)),
          "itemPlacement": rnd.choice(list(RULES)), "ganonCrystals": rnd.randint(0, 7),
          "ganonsTowerCrystals": rnd.randint(0, 7), "tourianBosses": rnd.randint(0, 4),
          "etecoonsJingle": rnd.choice(JINGLES)} for _ in range(n)]
b = os.path.join(root, "build")
with open(b + "/patch_cases.jsonl", "w") as f:
    for c in cases: f.write(json.dumps(c) + "\n")
subprocess.check_call(["dotnet", root + "/tools/gen-cli/bin/Release/net10.0/gen-cli.dll", "placementbatch", b + "/patch_cases.jsonl", b + "/patch_cs.txt"])
blocks = open(b + "/patch_cs.txt").read().split("case\n")[1:]
bad = 0
for c, blk in zip(cases, blocks):
    want = {}
    for l in blk.split("\n"):
        if l.startswith("P\t"):
            _, cls, off, hexs = l.split("\t")
            if cls in SKIP or int(off, 16) in GUID_OFFSETS: continue
            want[int(off, 16)] = hexs
    r = subprocess.run([b + "/test_patch", c["seed"], str(KEYS[c["keysanity"]]), str(RULES[c["itemPlacement"]]),
                        str(c["ganonCrystals"]), str(c["ganonsTowerCrystals"]), str(c["tourianBosses"]), "jingle=%d" % (JINGLES.index(c["etecoonsJingle"]) + 1)], capture_output=True, text=True)
    got = {}
    for l in r.stdout.split("\n"):
        if l.startswith("P\t"):
            _, cls, off, hexs = l.split("\t")
            if cls in SKIP or int(off, 16) in GUID_OFFSETS: continue
            got[int(off, 16)] = hexs
    miss = [o for o in want if got.get(o) != want[o]]
    extra = [o for o in got if o not in want]
    if miss or extra:
        bad += 1
        print("MISMATCH", c, "missing/different:", [hex(o) for o in miss[:8]], "extra:", [hex(o) for o in extra[:8]], r.stderr.strip())
        for o in miss[:3]: print("  want", hex(o), want[o][:60], "got", (got.get(o) or "none")[:60])
print(f"{n - bad}/{n} identical")
sys.exit(1 if bad else 0)
