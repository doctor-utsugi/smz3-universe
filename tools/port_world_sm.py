#!/usr/bin/env python3
"""Transpiles tewtal's standalone Super Metroid world (Randomizer.SuperMetroid: Item.cs helpers, World.cs region order,
Regions/**) into core/smgen/sm_world_gen.{h,c}. Same approach as tools/port_world.py; the C# stays the oracle
(tools/gen-cli-sm vectors / worldvectors)."""
import re, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cs_regions import ROOT, match_close, split_args, strip_comments

SM = ROOT / "vendor/tewtal-SMZ3Randomizer/Randomizer.SuperMetroid"
OUT_H = ROOT / "core/smgen/sm_world_gen.h"
OUT_C = ROOT / "core/smgen/sm_world_gen.c"
errors = []

item_src = (SM / "Item.cs").read_text(encoding="utf-8-sig")
item_enum = re.findall(r"(\w+) = (\d+),?", re.search(r"enum ItemType : byte \{(.*?)\}", item_src, re.S).group(1))

world_src = (SM / "World.cs").read_text(encoding="utf-8-sig")
region_classes = re.findall(r"new Regions\.([\w.]+)\(this", world_src)


def cid(name):
    return re.sub(r"[^A-Za-z0-9]", "", name)


regions = []   # dicts: cls, name, area, id, locations, enter, helpers
for rc in region_classes:
    text = strip_comments((SM / "Regions" / (rc.replace(".", "/") + ".cs")).read_text(encoding="utf-8-sig"))
    name = re.search(r'Name => "([^"]+)"', text).group(1)
    r = {"cls": rc, "name": name, "area": re.search(r'Area => "([^"]+)"', text).group(1), "id": cid(name), "locations": [], "helpers": {}, "enter": None}
    for m in re.finditer(r"new Location\(", text):
        op = m.end() - 1
        a = split_args(text[op + 1:match_close(text, op) - 1])
        r["locations"].append({"id": int(a[1]), "name": a[2].strip('"'), "type": a[3].split(".")[-1], "cls": a[4].split(".")[-1], "addr": int(a[5], 16), "access": a[6] if len(a) > 6 else None})
    for m in re.finditer(r"(?:public override|private) bool (\w+)\(List<Item> items\)\s*\{", text):
        body = text[m.end() - 1:match_close(text, m.end() - 1, "{", "}")]
        expr = re.match(r"\{\s*return (.*);\s*\}$", body, re.S).group(1)
        if m.group(1) == "CanEnter":
            r["enter"] = expr
        else:
            r["helpers"][m.group(1)] = expr
    regions.append(r)
by_name = {r["name"]: r for r in regions}

# item helpers (ItemListExtensions): name -> (extra C params, expression)
helpers = {}
for m in re.finditer(r"public static bool (\w+)\(this List<Item> items((?:, \w+ \w+)?)\)\s*\{", item_src):
    name, extra = m.group(1), m.group(2)
    if name in ("Has", "HasEnergyReserves"):
        continue
    body = item_src[m.end() - 1:match_close(item_src, m.end() - 1, "{", "}")]
    expr = re.match(r"\{\s*return (.*);\s*\}$", strip_comments(body), re.S).group(1)
    helpers[name] = (", int logic" if "Logic" in extra else "", expr)


def tx_switch(e, var, where, region=None):
    """`Logic switch { Casual => a, _ => b }` -> nested conditional on the logic parameter."""
    while True:
        m = re.search(r"\b[Ll]ogic switch\s*\{", e)
        if not m:
            return e
        end = match_close(e, m.end() - 1, "{", "}")
        arms = []
        for arm in split_args(e[m.end():end - 1]):
            pat, val = re.match(r"(\w+)\s*=>\s*(.*)$", arm, re.S).groups()
            arms.append((pat, tx(val, where, region)))
        out = arms[-1][1]
        if arms[-1][0] != "_":
            errors.append(f"{where}: switch without default arm")
        for pat, val in reversed(arms[:-1]):
            out = f"({var} == SM_LOGIC_{pat} ? {val} : {out})"
        e = e[:m.start()] + out + e[end:]


def top_split(s, sep):
    """Splits s at sep where no bracket is open."""
    out, depth, start, i = [], 0, 0, 0
    while i < len(s):
        c = s[i]
        if c in "([":
            depth += 1
        elif c in ")]":
            depth -= 1
        elif depth == 0 and s.startswith(sep, i):
            out.append(s[start:i].strip()); i += len(sep); start = i; continue
        i += 1
    return out + [s[start:].strip()]


def parens(e):
    """Brackets every && chain under ||, as -Wparentheses wants; the meaning does not change."""
    e = e.strip()
    q = top_split(e, "?")
    if len(q) > 1:
        a, b = top_split(q[1], ":", )[0], ":".join(top_split(q[1], ":")[1:])
        return f"{parens(q[0])} ? {parens(a)} : {parens(b)}"
    ors = top_split(e, "||")
    if len(ors) > 1:
        return " || ".join(f"({parens(o)})" if len(top_split(o, "&&")) > 1 else parens(o) for o in ors)
    ands = top_split(e, "&&")
    if len(ands) > 1:
        return " && ".join(parens(a) for a in ands)
    if e.startswith("(") and match_close(e, 0) == len(e):
        return "(" + parens(e[1:-1]) + ")"
    return e


def tx(e, where, region=None):
    e = re.sub(r"\s+", " ", strip_comments(e)).strip()
    e = tx_switch(e, "logic", where, region)
    m = re.fullmatch(r"new Requirement\((.*)\)", e)
    if m:
        e = m.group(1).strip()
    e = re.sub(r"^items\s*=>\s*", "", e)
    e = re.sub(r"items\.Has\((\w+), (\d+)\)", r"(p->n[SM_ITEM_\1] >= \2)", e)
    e = re.sub(r"items\.Has\((\w+)\)", r"(p->n[SM_ITEM_\1] > 0)", e)
    e = re.sub(r"items\.HasEnergyReserves\((\w+)\)", r"sm_HasEnergyReserves(p, \1)", e)
    e = re.sub(r"items\.(\w+)\([Ll]ogic\)", r"sm_\1(p, logic)", e)
    e = re.sub(r"items\.(\w+)\(\)", r"sm_\1(p)", e)
    e = re.sub(r'World\.CanEnter\("([^"]+)", items\)', lambda m: f"sm_can_enter_{by_name[m.group(1)]['id']}(logic, p)" if m.group(1) in by_name else m.group(0), e)
    if region:
        for h in region["helpers"]:
            e = re.sub(r"\b" + h + r"\(items\)", f"{region['id']}_{h}(logic, p)", e)
    e = parens(e)
    bad = re.findall(r"\b(items|World|Logic|Requirement|new|switch)\b|=>", e)
    if bad:
        errors.append(f"{where}: unresolved {bad}: {e[:160]}")
    return e


H = ["/* Generated by tools/port_world_sm.py from tewtal's Randomizer.SuperMetroid. Do not edit. */",
     "#ifndef SM_WORLD_GEN_H", "#define SM_WORLD_GEN_H", "", '#include "sm_world.h"', ""]
C = ["/* Generated by tools/port_world_sm.py from tewtal's Randomizer.SuperMetroid. Do not edit. */",
     '#include "sm_world_gen.h"', ""]

H.append("enum {")
H += [f"    SM_ITEM_{n} = {v}," for n, v in item_enum]
H.append(f"    SM_ITEM_COUNT = {max(int(v) for _, v in item_enum) + 1}")
H.append("};\n")
H.append("enum {")
H += [f"    SMREG_{r['id']} = {i}," for i, r in enumerate(regions)]
H.append(f"    SMREG_COUNT = {len(regions)}")
H.append("};\n")
nloc = sum(len(r["locations"]) for r in regions)
H.append(f"#define SM_LOC_COUNT {nloc}")
H.append(f"#define SM_LOC_MAX_ID {max(l['id'] for r in regions for l in r['locations'])}\n")

for n, (extra, _) in helpers.items():
    H.append(f"bool sm_{n}(const SmProg *p{extra});")
for r in regions:
    H.append(f"bool sm_can_enter_{r['id']}(int logic, const SmProg *p);")
    for h in r["helpers"]:
        H.append(f"bool {r['id']}_{h}(int logic, const SmProg *p);")
H.append("bool sm_region_can_enter(int region, int logic, const SmProg *p);")
H.append("bool sm_loc_access(int loc_id, int logic, const SmProg *p);\n")

for n, (extra, expr) in helpers.items():
    body = tx(expr, n)
    unused = "" if "logic" not in extra or "logic" in body else "\n    (void)logic;"
    C.append(f"bool sm_{n}(const SmProg *p{extra})\n{{{unused}\n    return {body};\n}}\n")
for r in regions:
    for h, expr in r["helpers"].items():
        C.append(f"bool {r['id']}_{h}(int logic, const SmProg *p)\n{{\n    (void)logic; (void)p;\n    return {tx(expr, r['id'] + '.' + h, r)};\n}}\n")
    body = tx(r["enter"], r["id"] + ".CanEnter", r) if r["enter"] else "true"
    C.append(f"bool sm_can_enter_{r['id']}(int logic, const SmProg *p)\n{{\n    (void)logic; (void)p;\n    return {body};\n}}\n")

C.append("bool sm_region_can_enter(int region, int logic, const SmProg *p)\n{\n    switch (region) {")
C += [f"    case SMREG_{r['id']}: return sm_can_enter_{r['id']}(logic, p);" for r in regions]
C.append("    default: return false;\n    }\n}\n")

C.append("bool sm_loc_access(int loc_id, int logic, const SmProg *p)\n{\n    (void)logic; (void)p;\n    switch (loc_id) {")
for r in regions:
    for l in r["locations"]:
        if l["access"]:
            C.append(f"    case {l['id']}: return {tx(l['access'], r['id'] + ':' + l['name'], r)};")
C.append("    default: return true;\n    }\n}\n")

C.append("const SmRegionDef SM_REGIONS[SMREG_COUNT] = {")
C += [f'    {{ "{r["name"]}", "{r["area"]}" }},' for r in regions]
C.append("};\n")
C.append("/* World.Locations order: regions as World.cs lists them, then each region's own order */")
C.append("const SmLocDef SM_LOCS[SM_LOC_COUNT] = {")
for i, r in enumerate(regions):
    for l in r["locations"]:
        C.append(f'    {{ {l["id"]}, "{l["name"]}", SM_LTYPE_{l["type"]}, SM_CLASS_{l["cls"]}, SMREG_{r["id"]}, 0x{l["addr"]:X} }},')
C.append("};")

# Patch.WriteItemLocations walks the locations OrderBy(Region.Name).ThenBy(Name) with .NET's culture comparer (ICU
# root collation): letters before case, punctuation and spaces non-ignorable and below digits and letters.
PRIMARY = " _-,;:!?.'\"()[]{}@*/\\&#%`^+<=>|~$0123456789abcdefghijklmnopqrstuvwxyz"


def icu_key(t):
    bad = [c for c in t if c.lower() not in PRIMARY]
    if bad:
        errors.append(f"credits order: no collation weight for {bad} in {t}")
    return ([PRIMARY.index(c.lower()) for c in t if c.lower() in PRIMARY], [c.isupper() for c in t])


flat = [(r, l) for r in regions for l in r["locations"]]
order = sorted(range(len(flat)), key=lambda i: (icu_key(flat[i][0]["name"]), icu_key(flat[i][1]["name"])))
H.append("extern const uint8_t SM_CREDITS_ORDER[SM_LOC_COUNT];\n")
C.append("\n/* SM_LOCS indices in Patch.WriteItemLocations order (region name, then location name, ICU root collation) */")
C.append("const uint8_t SM_CREDITS_ORDER[SM_LOC_COUNT] = {")
for i in range(0, len(order), 16):
    C.append("    " + ", ".join(str(x) for x in order[i:i + 16]) + ",")
C.append("};")

OUT_H.write_text("\n".join(H) + "\n#endif\n")
OUT_C.write_text("\n".join(C) + "\n")
print(f"{nloc} locations in {len(regions)} regions, {len(helpers)} item helpers")
for e in errors:
    print("TODO", e)
sys.exit(1 if errors else 0)
