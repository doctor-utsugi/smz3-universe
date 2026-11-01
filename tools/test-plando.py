#!/usr/bin/env python3
"""Plando: the C# exports a plando config for random seeds and generates from it; the C port must read the same YAML and write the same patches."""
import json, os, random, subprocess, sys
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
n = int(sys.argv[1]) if len(sys.argv) > 1 else 10
rnd = random.Random(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
KEYS = ["None", "Zelda", "SuperMetroid", "Both"]
RULES = ["Anywhere", "DungeonsAndMetroid", "CrystalDungeonsAndMetroid"]
SKIP = ("ZeldaTextsPatch",)                  # texts are a separate stage
GUID_OFFSETS = (0xFF60, 0xFF80)
JINGLES = json.load(open(root + "/build/jingle_names.json"))
TEXT_KEYS = ["GanonIntro", "GanonIntroGoalsNotMet", "BlindIntro", "TriforceRoom", "KingZora", "BottleMerchant", "GanonSilversHint",
             "GanonsTowerGoalSign", "GanonGoalSign", "SahasrahlaReveal", "BombShopReveal", "TavernMan", "MasterSwordPedestal", "EtherTablet",
             "BombosTablet"] + ["HintTile" + n for n in ("EasternPalace", "TowerOfHeraFloor4", "SpectacleRock", "SwampEntrance", "ThievesTownUpstairs",
             "MiseryMire", "PalaceOfDarkness", "DesertBonkTorchRoom", "CastleTower", "IceLargeRoom", "TurtleRock", "IceEntrance",
             "IceStalfosKnightsRoom", "TowerOfHeraEntrance", "SouthEastDarkworldCave")]
WORDS = "the hero finds a sword near old woman, secret? cave! lake 12 boss it's your key-ring dark; north east".split()
def some_text():
    return " ".join(rnd.choice(WORDS) for _ in range(rnd.randint(2, 12))).capitalize() + rnd.choice([".", "!", "?", ""])
cases = [{"seed": str(rnd.randint(1, 2**31 - 2)), "keysanity": rnd.choice(KEYS), "itemPlacement": rnd.choice(RULES),
          "ganonCrystals": rnd.randint(0, 7), "ganonsTowerCrystals": rnd.randint(0, 7), "tourianBosses": rnd.randint(0, 4),
          "gameMode": rnd.choice(["Vanilla", "Vanilla", "AllDungeons", "SpazerHunt"]),
          "pyramidHole": rnd.choice(["Open", "Closed"]), "tourianBossDoor": rnd.choice(["Open", "Closed"]),
          "etecoonsJingle": rnd.choice(JINGLES)} for _ in range(n)]
# every second case overrides all the texts that the generator would otherwise pick at random, so the whole text patch is comparable
for i, c in enumerate(cases):
    if i % 2: c["plandoText"] = {k: some_text() for k in TEXT_KEYS}
b = os.path.join(root, "build")
with open(b + "/plando_cases.jsonl", "w") as f:
    for c in cases: f.write(json.dumps(c) + "\n")
subprocess.check_call(["dotnet", root + "/tools/gen-cli/bin/Release/net10.0/gen-cli.dll", "plandobatch", b + "/plando_cases.jsonl",
                       b + "/plando_cs.txt", b + "/plando"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
blocks = [x for x in open(b + "/plando_cs.txt").read().split("case\n")[1:] if x]
bad = 0
for i, (c, blk) in enumerate(zip(cases, blocks)):
    if blk.startswith("error\t"): continue      # the seed itself cannot be generated, so there is no plando to compare
    texts = "plandoText" in c
    def patches(lines):
        d = {}
        for l in lines:
            if l.startswith("P\t"):
                _, cls, off, hexs = l.split("\t")
                if (cls in SKIP and not texts) or int(off, 16) in GUID_OFFSETS: continue
                d[int(off, 16)] = hexs
        return d
    want = patches(blk.split("\n"))
    r = subprocess.run([b + "/test_plando", "%s/plando/%d.yml" % (b, i), "jingle=%d" % (JINGLES.index(c["etecoonsJingle"]) + 1)], capture_output=True, text=True)
    got = patches(r.stdout.split("\n"))
    if texts:
        # the string table: entry by entry (entries start with 0xFB). The two merchant choices keep random Yes/No wordings, so they are skipped (and the padding that follows).
        TABLE = 0x5C8000 - 0x400000 + 0x400000
        ofs = 0x5C8000
        r2 = subprocess.run([b + "/test_plando", "%s/plando/%d.yml" % (b, i), "jingle=1"], capture_output=True, text=True, env=dict(os.environ, SMZ_TEXT_DEBUG="1"))
        skip = {int(l.split()[1]) for l in r2.stderr.split("\n") if l.startswith("E ") and l.split()[2] in ("bottle_vendor_choice", "zora_tells_cost", "end_pad_data")}
        def entries(h):
            raw = bytes.fromhex(h); parts = []; cur = 0
            while True:
                nx = raw.find(b"\xfb", cur + 1)
                if nx < 0: parts.append(raw[cur:]); break
                parts.append(raw[cur:nx]); cur = nx
            return parts
        if ofs in want and ofs in got:
            ew, eg = entries(want[ofs]), entries(got[ofs])
            if len(ew) == len(eg) and all(ew[k] == eg[k] for k in range(len(ew)) if k not in skip):
                del want[ofs]; del got[ofs]
    miss = [o for o in want if got.get(o) != want[o]]
    extra = [o for o in got if o not in want]
    if miss or extra or r.returncode:
        bad += 1
        print("MISMATCH", c, "missing/different:", [hex(o) for o in miss[:8]], "extra:", [hex(o) for o in extra[:8]], r.stderr.strip())
        for o in miss[:3]: print("  want", hex(o), want[o][:60], "got", (got.get(o) or "none")[:60])
print(f"{n - bad}/{n} identical")
sys.exit(1 if bad else 0)
