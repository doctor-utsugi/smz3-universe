#!/usr/bin/env python3
"""Differential test of game modes (All Dungeons, Spazer Hunt) and start-with / early items: the C filler and patcher must
give the same placements and the same ROM changes as the C# generator. Usage: test-modes.py [cases] [rng seed]"""
import json, os, random, subprocess, sys, yaml
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
n = int(sys.argv[1]) if len(sys.argv) > 1 else 20
rnd = random.Random(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
KEYS = {"None": 0, "Zelda": 1, "SuperMetroid": 2, "Both": 3}
RULES = {"Anywhere": 0, "DungeonsAndMetroid": 1, "CrystalDungeonsAndMetroid": 2}
MODES = {"Vanilla": 0, "AllDungeons": 1, "SpazerHunt": 2}
SKIP = ("EtecoonsJinglePatch", "ZeldaTextsPatch")
GUID_OFFSETS = (0xFF60, 0xFF80)
groups = yaml.safe_load(open(root + "/vendor/SMZ3Randomizer/src/TrackerCouncil.Smz3.Data/Options/ItemSettingOptions.yml"))
cases = []
for _ in range(n):
    c = {"seed": str(rnd.randint(1, 2**31 - 2)), "keysanity": rnd.choice(list(KEYS)),
         "itemPlacement": rnd.choice(list(RULES)), "ganonCrystals": rnd.randint(0, 7),
         "ganonsTowerCrystals": rnd.randint(0, 7), "tourianBosses": rnd.randint(0, 4),
         "gameMode": rnd.choice(list(MODES))}
    if c["gameMode"] == "SpazerHunt":
        c["spazersInPool"] = rnd.choice([40, 40, rnd.randint(20, 60)])
        c["spazersRequired"] = rnd.randint(1, 30)
    picks = sorted(rnd.sample(range(len(groups)), rnd.choice([0, 0, 1, 2, 4, 8])))
    c["itemOptions"] = {groups[g]["Item"]: rnd.randrange(1, len(groups[g]["Options"])) for g in picks}
    cases.append(c)
b = os.path.join(root, "build")
with open(b + "/modes_cases.jsonl", "w") as f:
    for c in cases: f.write(json.dumps(c) + "\n")
subprocess.check_call(["dotnet", root + "/tools/gen-cli/bin/Release/net10.0/gen-cli.dll", "placementbatch", b + "/modes_cases.jsonl", b + "/modes_cs.txt"])
blocks = open(b + "/modes_cs.txt").read().split("case\n")[1:]

def c_args(c):
    a = [c["seed"], str(KEYS[c["keysanity"]]), str(RULES[c["itemPlacement"]]), str(c["ganonCrystals"]),
         str(c["ganonsTowerCrystals"]), str(c["tourianBosses"]), "mode=%d" % MODES[c["gameMode"]]]
    if "spazersInPool" in c: a += ["spazers=%d" % c["spazersInPool"], "required=%d" % c["spazersRequired"]]
    a += ["opt:%s=%d" % kv for kv in c["itemOptions"].items()]
    return a

def patches(text):
    out = {}
    for l in text.split("\n"):
        if l.startswith("P\t"):
            _, cls, off, hexs = l.split("\t")
            if cls in SKIP or int(off, 16) in GUID_OFFSETS: continue
            out[int(off, 16)] = hexs
    return out

bad = 0
failed_both = 0
for c, blk in zip(cases, blocks):
    if blk.startswith("error\t"):      # the C# cannot fill this combination: the C filler must fail too
        r = subprocess.run([b + "/test_fill"] + c_args(c), capture_output=True, text=True)
        ok = r.returncode != 0
        print("ok " if ok else "BAD", "both fail" if ok else "C# failed but C filled", json.dumps(c)[:100])
        if ok: failed_both += 1
        else: bad += 1
        continue
    want_pl = [l for l in blk.split("\n") if l.split(" ")[0] in ("placement", "rewards", "prereq")]
    r = subprocess.run([b + "/test_fill"] + c_args(c), capture_output=True, text=True)
    got_pl = [l for l in r.stdout.split("\n") if l.split(" ")[0] in ("placement", "rewards", "prereq")]
    ok_pl = got_pl == want_pl
    want, r2 = patches(blk), subprocess.run([b + "/test_patch"] + c_args(c), capture_output=True, text=True)
    got = patches(r2.stdout)
    miss = [o for o in want if got.get(o) != want[o]]
    extra = [o for o in got if o not in want]
    ok_pa = not miss and not extra
    tag = "%s %s %s opts=%d" % (c["gameMode"], c["keysanity"], c["itemPlacement"], len(c["itemOptions"]))
    print("ok " if ok_pl and ok_pa else "BAD", tag, "" if ok_pl else "PLACEMENT", "" if ok_pa else "PATCH")
    if not (ok_pl and ok_pa):
        bad += 1
        print("  case", json.dumps(c), (r.stderr + r2.stderr).strip()[:200])
        for o in miss[:4]: print("  want", hex(o), want[o][:60], "got", (got.get(o) or "none")[:60])
        if extra: print("  extra", [hex(o) for o in extra[:6]])
print(f"{n - bad}/{n} identical ({failed_both} unfillable in both)")
sys.exit(1 if bad else 0)
