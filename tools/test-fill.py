#!/usr/bin/env python3
"""Differential test of the C filler against the C# one over many seeds and settings."""
import json, os, random, subprocess, sys
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
n = int(sys.argv[1]) if len(sys.argv) > 1 else 20
rnd = random.Random(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
KEYS = {"None": 0, "Zelda": 1, "SuperMetroid": 2, "Both": 3}
RULES = {"Anywhere": 0, "DungeonsAndMetroid": 1, "CrystalDungeonsAndMetroid": 2, "SameGame": 3, "OppositeGame": 4}
cases = []
for i in range(n):
    c = {"seed": str(rnd.randint(1, 2**31 - 2)),
         "keysanity": rnd.choice(list(KEYS)),
         "itemPlacement": rnd.choice(["Anywhere", "Anywhere", "DungeonsAndMetroid", "CrystalDungeonsAndMetroid"]),
         "ganonCrystals": rnd.randint(0, 7), "ganonsTowerCrystals": rnd.randint(0, 7),
         "tourianBosses": rnd.randint(0, 4)}
    cases.append(c)
b = os.path.join(root, "build")
with open(b + "/fill_cases.jsonl", "w") as f:
    for c in cases: f.write(json.dumps(c) + "\n")
subprocess.check_call(["dotnet", root + "/tools/gen-cli/bin/Release/net10.0/gen-cli.dll", "placementbatch", b + "/fill_cases.jsonl", b + "/fill_cs.txt"])
blocks = open(b + "/fill_cs.txt").read().split("case\n")[1:]
bad = 0
for c, blk in zip(cases, blocks):
    want = [l for l in blk.split("\n") if l.split(" ")[0] in ("placement", "rewards", "prereq")]
    r = subprocess.run([b + "/test_fill", c["seed"], str(KEYS[c["keysanity"]]), str(RULES[c["itemPlacement"]]),
                        str(c["ganonCrystals"]), str(c["ganonsTowerCrystals"]), str(c["tourianBosses"])],
                       capture_output=True, text=True)
    got = [l for l in r.stdout.split("\n") if l.split(" ")[0] in ("placement", "rewards", "prereq")]
    print("ok " if got==want else "BAD", c["keysanity"], c["itemPlacement"], c["ganonCrystals"], c["ganonsTowerCrystals"], c["tourianBosses"], len(r.stderr))
    if got != want:
        bad += 1
        print("MISMATCH", c, r.stderr.strip())
print(f"{n - bad}/{n} identical")
sys.exit(1 if bad else 0)
