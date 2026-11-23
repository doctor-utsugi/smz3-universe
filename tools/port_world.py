#!/usr/bin/env python3
"""Transpiles the Cas' tracker world data (regions, locations, access logic) into core/gen/smz3_world_gen.{h,c}.

Anything the translator cannot handle is taken from tools/world_overrides.py (hand-written C); the script stops with a
list of what is missing. The C# stays the source of truth and the test oracle (core/gen/test_world.c).
"""
import re, sys, glob
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cs_regions import *
import world_overrides as OV

OUT_H = ROOT / "core/gen/smz3_world_gen.h"
OUT_C = ROOT / "core/gen/smz3_world_gen.c"
WORLD_CS = ROOT / "vendor/SMZ3Randomizer/src/TrackerCouncil.Smz3.Data/WorldData/World.cs"

# region order as World.Regions
world_src = WORLD_CS.read_text(encoding="utf-8-sig")
region_order = re.findall(r"(\w+)", re.search(r"Regions = new List<Region> \{(.*?)\};", world_src, re.S).group(1))

files = {}
for f in glob.glob(str(REGIONS) + "/**/*.cs", recursive=True):
    name = Path(f).name
    if name[0] == "I" and name[1].isupper() or name in ("Region.cs", "SMRegion.cs", "Z3Region.cs"):
        continue
    info, text = parse_file(f)
    info["text"] = text
    files[info["class"]] = info
assert set(region_order) == set(files), set(region_order) ^ set(files)

errors = []

# default arguments of the Logic methods (C has none)
LOGIC_DEFAULTS = {}
for m in re.finditer(r"public bool (\w+)\(Progression items((?:, [^,)]+)*)\)", (ROOT / "vendor/SMZ3Randomizer/src/TrackerCouncil.Smz3.Data/Logic/Logic.cs").read_text(encoding="utf-8-sig")):
    params = [x.strip() for x in m.group(2).split(",") if x.strip()]
    LOGIC_DEFAULTS[m.group(1)] = [x.split("=")[1].strip() if "=" in x else None for x in params]


def clean(e):
    e = strip_comments(e)
    return re.sub(r"\s+", " ", e).strip()


def tx(e, cls, where, props, scope=None):
    """C# boolean expression -> C expression using w (world), p (progression), req (requireRewards)."""
    e = clean(e)
    e = re.sub(r"^items\s*=>\s*", "", e)
    if scope:
        for hn in [h.split("__", 1)[1] for h in files[cls]["helpers"] if h.startswith(scope + "__")]:
            e = re.sub(r"(?<![\w.])" + hn + r"(?=\()", f"{scope}__{hn}", e)
            if e == hn:
                e = f"{scope}__{hn}"
    e = re.sub(r"\b\w+ != null\b", "1", e)
    e = re.sub(r"\bnull\b", "0", e)
    e = re.sub(r"\blogic\.", "Logic.", e)
    e = re.sub(r",\s*[Ll]ogic\b(?!\.)", "", e)
    e = re.sub(r"\bInnerMaridia\.CanReachAqueduct", "CanReachAqueduct", e)
    e = re.sub(r"LocationId\.(\w+)", r"LOC_\1", e)
    def waste(m):
        locs = [x.strip() for x in m.group(1).split(",")]
        for l in locs:
            if l not in props:
                errors.append(f"{where}: unknown location property {l}")
        return "IcePalace_CanNotWasteKeys(w, p, " + ", ".join("LOC_" + props.get(l, "Nothing") for l in locs) + ")"
    e = re.sub(r"\bCanNotWasteKeysBeforeAccessible\(items,\s*([^()]*)\)", waste, e)
    # logic helpers
    e = re.sub(r"(?:World\.)?Logic\.CheckAgahnim\(items, World, ", "CheckAgahnim(w, p, ", e)
    e = re.sub(r"(?:World\.)?Logic\.CanAccessMaridiaPortal\(items, ", "CanAccessMaridiaPortal(w, p, &w->cfg, ", e)
    def logic_call(m):
        fn, rest = m.group(1), m.group(2)
        rest = re.sub(r"^\s*items\s*,?\s*", "", rest)
        given = [x.strip() for x in split_args(rest)] if rest.strip() else []
        defaults = LOGIC_DEFAULTS.get(fn, [])
        for d in defaults[len(given):]:
            if d is not None:
                given.append(d)
        return f"{fn}(p, &w->cfg" + "".join(", " + g for g in given) + ")"
    e = re.sub(r"(?:World\.)?Logic\.(\w+)\(((?:[^()]|\([^()]*\))*)\)", logic_call, e)
    e = re.sub(r"WallJumpDifficulty\.(\w+)", lambda m: {"None": "0", "Easy": "1", "Medium": "2", "Hard": "3", "Insane": "4"}[m.group(1)], e)
    # regions
    e = re.sub(r"World\.(\w+)\.CanEnter\(items,\s*([^()]*)\)", lambda m: f"can_enter_{m.group(1)}(w, p, {m.group(2)})", e)
    e = re.sub(r"\bCanEnter\(items,\s*([^()]*)\)", lambda m: f"can_enter_{cls}(w, p, {m.group(1)})", e)
    e = re.sub(r"\b(requireRewards|isTracking):\s*", "", e)
    e = e.replace("requireRewards", "req").replace("isTracking", "0")
    e = re.sub(r"World\.FindLocation\((?:LocationId\.|LOC_)(\w+)\)\.IsAvailable\(items\)", r"loc_available(w, LOC_\1, p)", e)
    # progression
    def pattern(m):
        return "(" + " && ".join(f"P_{n}(p)" if v == "true" else f"!P_{n}(p)" for n, v in re.findall(r"(\w+): (true|false)", m.group(1))) + ")"
    e = re.sub(r"items is \{([^}]*)\}", pattern, e)
    def world_call(m):
        reg, name, args = m.group(1), m.group(2), m.group(3)
        if reg in files and name in files[reg]["helpers"]:
            rest = re.sub(r"^\s*items\s*,?\s*", "", args).replace("requireRewards", "req")
            return f"{reg}_{name}(w, p" + (", " + rest if rest.strip() else "") + ")"
        return m.group(0)
    e = re.sub(r"World\.(\w+)\.(\w+)\(((?:[^()]|\([^()]*\))*)\)", world_call, e)
    e = re.sub(r"World\.CanAquireAll\(items, ([^,]+), RewardType\.(\w+)\)", r"can_aquire_all(w, p, \1, REWARD_\2)", e)
    e = re.sub(r"items\.Contains\(ItemType\.(\w+)\)", r"(p->items[ITEM_\1] > 0)", e)
    e = re.sub(r"items\.Contains\(PrerequisiteState\.RequiredItem\)", f"(p->items[w->prereq[REG_{cls}]] > 0)", e)
    e = re.sub(r"\bitems\.(\w+)", r"P_\1(p)", e)
    # config
    e = re.sub(r"(?:World\.)?Config\.LogicConfig\.(\w+)", r"w->cfg.\1", e)
    e = re.sub(r"\b[Cc]onfig\.ZeldaKeysanity", "w->zelda_keysanity", e)
    e = re.sub(r"\b[Cc]onfig\.MetroidKeysanity", "w->metroid_keysanity", e)
    # item placement tests on locations of this region
    def item_is(m):
        prop, typ = m.group(1), m.group(2)
        if prop not in props:
            errors.append(f"{where}: ItemIs on unknown property {prop}")
            return "0"
        return f"(w->item_at[LOC_{props[prop]}] == ITEM_{typ})"
    e = re.sub(r"\b(\w+)\.ItemIs\(ItemType\.(\w+),\s*World\)", item_is, e)
    # region helper methods
    helpers = files[cls]["helpers"]
    def helper_call(m):
        owner, name, args = m.group(1), m.group(2), m.group(3)
        target = cls
        if owner and owner not in ("region", "tower", "this"):
            target = owner
        if name not in files[target]["helpers"]:
            return m.group(0)
        sig = files[target]["helpers"][name]
        rest = re.sub(r"^\s*items\s*,?\s*", "", args)
        rest = rest.replace("requireRewards", "req")
        return f"{target}_{name}(w, p" + (", " + rest if rest.strip() else "") + ")"
    e = re.sub(r"\b(?:(\w+)\.)?([A-Z]\w+)\(((?:[^()]|\([^()]*\))*)\)", lambda m: helper_call(m) if (m.group(2) in helpers or (m.group(1) in ("region", "tower"))) else m.group(0), e)
    # method groups used as access: TowerAscend / BigKeyRoom
    if re.fullmatch(r"(?:\w+\.)?[A-Z]\w+", e) and e.split(".")[-1] in helpers:
        e = f"{cls}_{e.split('.')[-1]}(w, p)"
    return e


def leftover(e, where):
    bad = re.findall(r"(?<!->)\b(items|World|Logic|Config|config|Any|new|Item|world)\b", e)
    if bad:
        errors.append(f"{where}: unresolved {sorted(set(bad))}: {e[:160]}")
    return not bad


# ---- gather helpers and methods per class (including nested room classes)
for cls, info in files.items():
    text = info["text"]
    helpers = {}
    methods = {}
    classes = [(m.start(), m.group(1), m.group(2)) for m in re.finditer(r"class (\w+)\s*:\s*(\w+)", text)]
    for m in re.finditer(r"\n\s+(?:public|private|protected|internal)(?: override| static)? bool (\w+)\(((?:[^()])*)\)\s*(=>|\{)", text):
        name, params = m.group(1), m.group(2)
        encl = [c for c in classes if c[0] < m.start()][-1]
        scope = encl[1] if encl[2] == "Room" else None
        start = m.end(0) - len(m.group(3))
        if m.group(3) == "=>":
            end = text.index(";", start)
            body = "return " + text[start + 2:end] + ";"
        else:
            end = match_close(text, start, "{", "}")
            body = text[start + 1:end - 1]
        plist = [x.strip() for x in params.split(",") if x.strip() and not x.strip().startswith("ILogic")]
        if name == "CanNotWasteKeysBeforeAccessible":
            continue
        if scope:
            name = f"{scope}__{name}"
        methods.setdefault(name, []).append((plist, body))
        if scope or name not in ("CanEnter", "CanFill", "CanRetrieveReward", "CanSeeReward", "CanBeatBoss"):
            helpers[name] = plist
    info["helpers"] = helpers
    info["methods"] = methods


def method_body(cls, name, nparams):
    for plist, body in files[cls]["methods"].get(name, []):
        if len(plist) == nparams:
            return plist, body
    return None


def translate_body(body, cls, where, props, scope=None):
    r = _translate_body(body, cls, where, props, scope)
    if r is not None and not leftover(r, where):
        return None
    return r


def _translate_body(body, cls, where, props, scope=None):
    """{ ... } statements to C; supports var declarations and returns, nothing else."""
    out = []
    body = strip_comments(body)
    for stmt in [s.strip() for s in body.split(";") if s.strip()]:
        stmt = re.sub(r"\s+", " ", stmt)
        m = re.match(r"var (\w+) = (.*)$", stmt)
        if m:
            out.append(f"    int {m.group(1)} = {tx(m.group(2), cls, where, props, scope)};")
        elif stmt.startswith("return "):
            out.append(f"    return {tx(stmt[7:], cls, where, props, scope)};")
        elif stmt.startswith("if ") or "{" in stmt or "}" in stmt:
            errors.append(f"{where}: control flow in body")
            return None
        else:
            errors.append(f"{where}: unsupported statement {stmt[:80]}")
            return None
    return "\n".join(out)


header = ["/* Generated by tools/port_world.py from the Cas' tracker region files. Do not edit. */",
          "#ifndef SMZ3_WORLD_GEN_H", "#define SMZ3_WORLD_GEN_H", "", '#include "smz3_world.h"', ""]
src = ["/* Generated by tools/port_world.py from the Cas' tracker region files. Do not edit. */",
       '#include "smz3_world.h"', '#include "smz3_world_gen.h"', '#include "smz3_world_extra.h"', ""]

regions = region_order
header.append("enum {")
for i, r in enumerate(regions):
    header.append(f"    REG_{r} = {i},")
header.append(f"    REG_COUNT = {len(regions)}")
header.append("};")
header.append("")

# location table
all_locs = []
for ri, r in enumerate(regions):
    for l in files[r]["locations"]:
        all_locs.append((ri, r, l))

order = {int(x): i for i, x in enumerate((ROOT / "data/loc_order.txt").read_text().split())}
enum_ids = {m[0]: int(m[1]) for m in re.findall(r"LOC_(\w+) = (\d+)", (ROOT / "core/gen/smz3_enums.h").read_text())}
all_locs.sort(key=lambda t: order[enum_ids[t[2]["id"]]])
assert [enum_ids[t[2]["id"]] for t in all_locs] == sorted(order, key=order.get), "region order differs from the C# world"

# prototypes
for r in regions:
    header.append(f"bool can_enter_{r}(const SmzWorld *w, const SmzProg *p, bool req);")
    for name, plist in files[r]["helpers"].items():
        extra = []
        for pl in plist[1:]:
            t, n = pl.rsplit(" ", 1)
            extra.append("int " + n if t != "bool" else "bool " + n)
        header.append(f"bool {r}_{name}(const SmzWorld *w, const SmzProg *p{', ' if extra else ''}{', '.join(extra)});")
header.append("")

for pr in OV.PROTOS:
    header.append(pr)
header.append("bool region_can_enter(const SmzWorld *w, int r, const SmzProg *p, bool req);")
header.append("")

# bodies
for r in regions:
    info = files[r]
    props = {l["prop"]: l["id"] for l in info["locations"] if l["prop"]}
    for name, plist in info["helpers"].items():
        key = f"{r}_{name}"
        if key in OV.BODIES:
            src.append(OV.BODIES[key]); continue
        got = method_body(r, name, len(plist))
        extra = []
        for pl in plist[1:]:
            t, n = pl.rsplit(" ", 1)
            extra.append(("bool " if t == "bool" else "int ") + n)
        sig = f"bool {key}(const SmzWorld *w, const SmzProg *p{', ' if extra else ''}{', '.join(extra)})"
        if "bool requireRewards" in plist[-1] and not extra:
            pass
        reqparam = [pl for pl in plist if pl.endswith("requireRewards")]
        if reqparam:
            sig = sig.replace("requireRewards", "req")
        b = translate_body(got[1], r, key, props, name.split('__')[0] if '__' in name else None) if got else None
        if b is None:
            errors.append(f"{key}: needs a hand-written body (world_overrides.BODIES)")
            continue
        src.append(f"{sig}\n{{\n{b}\n}}\n")
    # CanEnter
    key = f"can_enter_{r}"
    if key in OV.BODIES:
        src.append(OV.BODIES[key]); continue
    got = method_body(r, "CanEnter", 2)
    if got is None:
        src.append(f"bool {key}(const SmzWorld *w, const SmzProg *p, bool req)\n{{\n    (void)w; (void)p; (void)req;\n    return true;\n}}\n")
    else:
        b = translate_body(got[1], r, key, props)
        if b is None:
            errors.append(f"{key}: needs a hand-written body"); continue
        src.append(f"bool {key}(const SmzWorld *w, const SmzProg *p, bool req)\n{{\n    (void)w; (void)p; (void)req;\n{b}\n}}\n")

for k, body in OV.BODIES.items():
    if body not in src:
        src.append(body)
src.append("bool region_can_enter(const SmzWorld *w, int r, const SmzProg *p, bool req)\n{\n    switch (r) {")
for r in regions:
    src.append(f"    case REG_{r}: return can_enter_{r}(w, p, req);")
src.append("    default: return true;\n    }\n}\n")

# location access
src.append("bool loc_access(const SmzWorld *w, int loc, const SmzProg *p, bool req)\n{\n    (void)w; (void)p; (void)req;\n    switch (loc) {")
for ri, r, l in all_locs:
    props = {x["prop"]: x["id"] for x in files[r]["locations"] if x["prop"]}
    acc = l["named"].get("access")
    key = "LOC_" + l["id"]
    if key in OV.ACCESS:
        src.append(f"    case {key}: return {OV.ACCESS[key]};"); continue
    if acc is None or clean(acc) in ("null", "true"):
        continue
    c = tx(acc, r, key, props, l['room'])
    if not leftover(c, key):
        continue
    src.append(f"    case {key}: return {c};")
src.append("    default: return true;\n    }\n}\n")

def allow_tx(e, key):
    e = clean(e)
    e = re.sub(r"^\(item, items\)\s*=>\s*", "", e)
    def all_not(m):
        types = re.findall(r"ItemType\.(\w+)", m.group(1))
        return "(" + " && ".join(f"item != ITEM_{t}" for t in types) + ")"
    e = re.sub(r"new\[\] \{([^}]*)\}\.All\(type => item\.IsNot\(type, World\)\)", all_not, e)
    e = re.sub(r"item\.Is\(ItemType\.(\w+), World\)", r"(item == ITEM_\1)", e)
    e = re.sub(r"item\.IsNot\(ItemType\.(\w+), World\)", r"(item != ITEM_\1)", e)
    e = e.replace("item.IsDungeonItem", "item_is_dungeon(item)")
    e = re.sub(r"\bConfig\.ZeldaKeysanity", "w->zelda_keysanity", e)
    e = re.sub(r"\bitems\.(\w+)", r"P_\1(p)", e)
    if re.search(r"\b(items|World|new)\b", e.replace("->", "")):
        errors.append(f"{key}: allow rule not translated: {e}")
    return e

for fn, chain_name, default in (("loc_allow", "Allow", "true"), ("loc_always_allow", "AlwaysAllow", "false")):
    src.append(f"bool {fn}(const SmzWorld *w, int loc, int item, const SmzProg *p)\n{{\n    (void)w; (void)item; (void)p;\n    switch (loc) {{")
    for ri, r, l in all_locs:
        for cn, ca in l["chain"]:
            if cn == chain_name:
                src.append(f"    case LOC_{l['id']}: return {allow_tx(ca, l['id'])};")
    src.append(f"    default: return {default};\n    }}\n}}\n")

# location table
src.append("const SmzLocDef SMZ_LOCS[] = {")
for ri, r, l in all_locs:
    w = "0"
    for cn, ca in l["chain"]:
        if cn == "Weighted":
            cm = re.search(r"const int " + ca + r" = (-?\d+);", files[r]["text"])
            w = cm.group(1) if cm else str(int(ca))
    van = l["named"].get("vanillaItem", "ItemType.Nothing").split(".")[-1]
    src.append(f"    {{ LOC_{l['id']}, 0x{int(l['rom'], 16):X}, LTYPE_{l['type']}, ITEM_{van}, REG_{r}, {l['named']['memoryAddress']}, {l['named']['memoryFlag']}, {w} }},")
src.append("};")
src.append(f"const int SMZ_LOC_COUNT = {len(all_locs)};\n")

# region table: reward/boss defaults
src.append("const SmzRegionDef SMZ_REGIONS[REG_COUNT] = {")
for r in regions:
    t = files[r]["text"]
    def g(nm, default):
        m = re.search(nm + r"\s*=>\s*([^;]+);", t)
        return m.group(1).strip() if m else default
    rew = g("DefaultRewardType", "RewardType.None").split(".")[-1]
    boss = g("DefaultBossType", "BossType.None").split(".")[-1]
    prereq = g("DefaultRequiredItem", "ItemType.Nothing").split(".")[-1]
    wm = re.search(r"\bWeight = (-?\d+);", t)
    weight = wm.group(1) if wm else "0"
    overworld = "1" if "IsOverworld = true" in t else "0"
    kind = "RK_SM" if "SMRegion" in files[r]["bases"] else "RK_Z3"
    shuffled = g("IsShuffledReward", "false")
    src.append(f'    {{ "{r}", {kind}, REWARD_{rew}, BOSS_{boss}, ITEM_{prereq}, {"1" if "IHasReward" in files[r]["bases"] else "0"}, {"1" if shuffled == "true" else ("2" if "ShuffleMetroidBossTokens" in shuffled else "0")}, {weight}, {overworld} }},')
src.append("};\n")

src.append("bool region_has_item(int region, int item)\n{\n    switch (region) {")
for r in regions:
    t = files[r]["text"]
    m = re.search(r"RegionItems = new Dictionary<ItemType, ItemType>(?:\(\))?\s*\{(.*?)\};", t, re.S)
    if m:
        vals = re.findall(r"\{\s*ItemType\.\w+,\s*ItemType\.(\w+)\s*\}", m.group(1))
        src.append(f"    case REG_{r}: return " + " || ".join(f"item == ITEM_{v}" for v in vals) + ";")
src.append("    default: return false;\n    }\n}\n")

OUT_H.write_text("\n".join(header) + "\n#endif\n")
OUT_C.write_text("\n".join(src))
print(f"{len(all_locs)} locations in {len(regions)} regions")
for e in errors:
    print("TODO", e)
sys.exit(1 if errors else 0)
