#!/usr/bin/env python3
"""Compares the hint tiles of the C generator (core/gen/smz3_hints.c) with the C# GameHintService for random seeds."""
import json, os, random, subprocess, sys, yaml
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
n = int(sys.argv[1]) if len(sys.argv) > 1 else 10
rnd = random.Random(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
KEYS = {"None": 0, "Zelda": 1, "SuperMetroid": 2, "Both": 3}
RULES = {"Anywhere": 0, "DungeonsAndMetroid": 1, "CrystalDungeonsAndMetroid": 2}
cases = [{"seed": str(rnd.randint(1, 2**31 - 2)), "keysanity": rnd.choice(list(KEYS)),
          "itemPlacement": rnd.choice(list(RULES)), "ganonCrystals": rnd.randint(0, 7),
          "ganonsTowerCrystals": rnd.randint(0, 7), "tourianBosses": rnd.randint(0, 4),
          "gameMode": rnd.choice(["Vanilla", "Vanilla", "AllDungeons", "SpazerHunt"])} for _ in range(n)]
groups = yaml.safe_load(open(root + "/vendor/SMZ3Randomizer/src/TrackerCouncil.Smz3.Data/Options/ItemSettingOptions.yml"))
for c in cases:
    picks = sorted(rnd.sample(range(len(groups)), rnd.choice([0, 0, 1, 3, 6])))
    c["itemOptions"] = {groups[g]["Item"]: rnd.randrange(1, len(groups[g]["Options"])) for g in picks}
MODES = {"Vanilla": 0, "AllDungeons": 1, "SpazerHunt": 2}
b = os.path.join(root, "build")
with open(b + "/hint_cases.jsonl", "w") as f:
    for c in cases: f.write(json.dumps(c) + "\n")
subprocess.check_call(["dotnet", root + "/tools/gen-cli/bin/Release/net10.0/gen-cli.dll", "placementbatch", b + "/hint_cases.jsonl", b + "/hint_cs.txt"])
blocks = open(b + "/hint_cs.txt").read().split("case\n")[1:]
bad = 0
for c, blk in zip(cases, blocks):
    if blk.startswith("error\t"):      # unfillable in C#: the C filler must fail too
        r = subprocess.run([b + "/test_hints", c["seed"], str(KEYS[c["keysanity"]]), str(RULES[c["itemPlacement"]]),
                            str(c["ganonCrystals"]), str(c["ganonsTowerCrystals"]), str(c["tourianBosses"]), "mode=%d" % MODES[c["gameMode"]]]
                           + ["opt:%s=%d" % kv for kv in c["itemOptions"].items()], capture_output=True, text=True)
        if r.returncode == 0:
            bad += 1
            print("MISMATCH C# could not fill", c)
        continue
    want = sorted(l for l in blk.split("\n") if l.startswith("H\t"))
    r = subprocess.run([b + "/test_hints", c["seed"], str(KEYS[c["keysanity"]]), str(RULES[c["itemPlacement"]]),
                        str(c["ganonCrystals"]), str(c["ganonsTowerCrystals"]), str(c["tourianBosses"]), "mode=%d" % MODES[c["gameMode"]]]
                       + ["opt:%s=%d" % kv for kv in c["itemOptions"].items()], capture_output=True, text=True)
    got = sorted(l for l in r.stdout.split("\n") if l.startswith("H\t"))
    if got != want:
        bad += 1
        print("MISMATCH", c, r.stderr.strip())
        for w, g in zip(want, got):
            if w != g: print("  want", w, "\n  got ", g)
        if len(want) != len(got): print("  counts", len(want), len(got))
print(f"{n - bad}/{n} identical")
sys.exit(1 if bad else 0)
