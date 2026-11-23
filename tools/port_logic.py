#!/usr/bin/env python3
"""Transpiles the Cas' tracker logic (Progression.cs properties and Logic.cs methods) into core/gen/smz3_logic.h.

The C# is the source of truth; anything the translator cannot handle is listed at the end so it gets ported by hand.
"""
import re, sys
from pathlib import Path

root = Path(__file__).resolve().parent.parent
src = root / "vendor/SMZ3Randomizer/src"
data = src / "TrackerCouncil.Smz3.Data"
out = root / "core/gen/smz3_logic.h"

prog_cs = (data / "WorldData/Progression.cs").read_text(encoding="utf-8-sig")
logic_cs = (data / "Logic/Logic.cs").read_text(encoding="utf-8-sig")
cfg_cs = (data / "Logic/LogicConfig.cs").read_text(encoding="utf-8-sig")
item_cs = (src / "TrackerCouncil.Smz3.Shared/Enums/ItemType.cs").read_text(encoding="utf-8-sig")

manual = []

# ---- bottles: every ItemType carrying the Bottle category
bottles = re.findall(r"ItemCategory\([^)]*ItemCategory\.Bottle[^)]*\)\]\s*(\w+)\s*=", item_cs)

# ---- config fields
cfg_fields = re.findall(r"public (bool|WallJumpDifficulty) (\w+) \{ get; set; \}", cfg_cs)

def expr(e, in_prog=False):
    e = e.strip()
    e = re.sub(r"\bContains\(ItemType\.(\w+),\s*(\d+)\)", r"(p->items[ITEM_\1] >= \2)", e)
    e = re.sub(r"\bContains\(ItemType\.(\w+)\)", r"(p->items[ITEM_\1] > 0)", e)
    e = re.sub(r"\bGetCount\(ItemType\.(\w+)\)", r"p->items[ITEM_\1]", e)
    e = re.sub(r"Rewards\.Contains\(RewardType\.(\w+)\)", r"(p->rewards[REWARD_\1] > 0)", e)
    e = re.sub(r"DefeatedBosses\.Contains\(BossType\.(\w+)\)", r"(p->bosses[BOSS_\1] > 0)", e)
    def rcount(m):
        names = re.findall(r"RewardType\.(\w+)", m.group(1))
        return "(" + " + ".join(f"p->rewards[REWARD_{n}]" for n in names) + ")"
    e = re.sub(r"Rewards\.Count\(r => (r is [\w\. ]+(?: or [\w\. ]+)*)\)", rcount, e)
    e = re.sub(r"Rewards\.Count\(r => r == RewardType\.(\w+)\)", r"p->rewards[REWARD_\1]", e)
    return e

# ---- Progression properties
props = []
for m in re.finditer(r"public (bool|int) (\w+) => ([^;]+);", prog_cs):
    typ, name, body = m.groups()
    if name in ("IsReadOnly",):
        continue
    c = expr(body)
    if re.search(r"\bItems\b|\bRewards\b|\bDefeatedBosses\b|GetRupeeCount|=>|\bGetCount|\bContains", c):
        manual.append(f"Progression.{name}: {body}")
        continue
    props.append((typ, name, c))
manual_props = {
    "Bottle": ("bool", "(" + " || ".join(f"p->items[ITEM_{b}] > 0" for b in bottles) + ")"),
    "Rupees": ("int", "(p->items[ITEM_OneRupee] + 5 * p->items[ITEM_FiveRupees] + 20 * (p->items[ITEM_TwentyRupees] + p->items[ITEM_TwentyRupees2])"
                      " + 50 * p->items[ITEM_FiftyRupees] + 100 * p->items[ITEM_OneHundredRupees] + 300 * p->items[ITEM_ThreeHundredRupees])"),
    "Count": ("int", "0"),
}
known = {n for _, n, _ in props}
for n, (t, c) in manual_props.items():
    if n not in known:
        props.append((t, n, c))
# fix up ones the generic pass left alone (Bottle used Items.Any, Rupees GetRupeeCount, Count)
props = [(t, n, manual_props[n][1] if n in manual_props else c) for t, n, c in props]
manual = [m for m in manual if not any(m.startswith(f"Progression.{n}:") for n in manual_props)]
# ETank/ReserveTank: same expression; HeartCount handled by the generic GetCount rewrite

# ---- Logic methods
methods = []
for m in re.finditer(r"public bool (\w+)\(([^)]*)\)\s*(=>\s*[^;]+;|\{.*?\n    \})", logic_cs, re.S):
    name, args, body = m.groups()
    methods.append((name, args, body))

enum_params = {"WallJumpDifficulty": "int"}

defaults_by_fn = {}

def convert_args(a, fn=None):
    defaults = {}
    out = []
    for part in [x.strip() for x in a.split(",") if x.strip()]:
        default = None
        if "=" in part:
            part, default = [x.strip() for x in part.split("=")]
        t, n = part.rsplit(" ", 1)
        if default is not None:
            defaults[len(out)] = default
        if t == "Progression":
            continue
        if t == "World":
            out.append("const SmzWorld *world")
            continue
        out.append(f"{ {'bool':'bool','int':'int','WallJumpDifficulty':'int'}.get(t, t) } {n}")
    if fn:
        defaults_by_fn[fn] = (len(out), defaults)
    return out

def convert_body(body):
    b = body
    b = re.sub(r"^=>\s*", "return ", b)
    b = re.sub(r"World\.Config\.LogicConfig\.(\w+)", r"cfg->\1", b)
    b = b.replace("CheckAgahnim(items, World, ", "CheckAgahnim(w, p, ")
    b = re.sub(r"\bitems\.(\w+)", r"P_\1(p)", b)
    b = re.sub(r"WallJumpDifficulty\.(\w+)", lambda m: {"None": "0", "Easy": "1", "Medium": "2", "Hard": "3", "Insane": "4"}[m.group(1)], b)
    # calls to other logic functions: Foo(items, x) / Foo(x)
    def call(mm):
        fn = mm.group(1)
        if fn in names:
            rest = mm.group(2).strip()
            rest = re.sub(r"^items\s*,?\s*", "", rest)
            nargs, dflt = defaults_by_fn.get(fn, (0, {}))
            given = [x for x in rest.split(",") if x.strip()]
            for i in range(len(given), nargs):
                if i in dflt:
                    given.append(dflt[i])
            rest = ", ".join(x.strip() for x in given)
            return f"{fn}(p, cfg" + (", " + rest if rest else "") + ")"
        return mm.group(0)
    b = re.sub(r"\b(\w+)\(([^()]*)\)", call, b)
    b = b.replace("&& ", "&& ")
    return b

names = {n for n, _, _ in methods} - {"CheckAgahnim"}
for n, a, _ in methods:
    convert_args(a, n)
funcs = []
for name, args, body in methods:
    cargs = convert_args(args)
    if "world" in args.lower() and "World world" in args:
        manual.append(f"Logic.{name}: needs the world (reward/region access)")
        continue
    cb = convert_body(body.strip())
    if "CanAquire" in cb or "World." in cb:
        manual.append(f"Logic.{name}: {cb[:80]}")
        continue
    if not cb.rstrip().startswith("{"):
        cb = "{ " + cb.rstrip() + " }"
    lead = ["const SmzWorld *w"] if "CheckAgahnim(" in cb else []
    sig = ", ".join(lead + ["const SmzProg *p", "const SmzCfg *cfg"] + cargs)
    funcs.append(f"static inline bool {name}({sig}) {cb}")

lines = ["/* Generated by tools/port_logic.py from the Cas' tracker (Progression.cs, Logic.cs). Do not edit. */",
         "#ifndef SMZ3_LOGIC_H", "#define SMZ3_LOGIC_H", "", "#include <stdbool.h>", "#include <stdint.h>",
         '#include "smz3_enums.h"', "",
         "typedef struct { uint8_t items[256]; uint8_t rewards[16]; uint8_t bosses[24]; } SmzProg;",
         "typedef struct {"]
for t, n in cfg_fields:
    lines.append(f"    {'bool' if t == 'bool' else 'int'} {n};")
lines += ["} SmzCfg;", "typedef struct SmzWorld SmzWorld;", ""]
pnames = [n for _, n, _ in props]
def xref(c):
    return re.sub(r"\b(" + "|".join(pnames) + r")\b(?!\()", lambda m: f"P_{m.group(1)}(p)", c)
for t, n, c in props:
    lines.append(f"static inline {'bool' if t == 'bool' else 'int'} P_{n}(const SmzProg *p);")
lines.append("/* Needs the world: can the Agahnim reward be reached (implemented with the regions). */")
lines.append("bool CheckAgahnim(const SmzWorld *w, const SmzProg *p, bool requireRewards);")
lines.append("")
for t, n, c in props:
    c = xref(c)
    lines.append(f"static inline {'bool' if t == 'bool' else 'int'} P_{n}(const SmzProg *p) {{ return {c}; }}")
lines.append("")
lines += [re.sub(r"\{.*", ";", f.split("\n")[0]) if False else "" for f in []]
for f in funcs:
    sig = f[:f.index(")") + 1] if False else f.split(") {")[0] + ");"
    lines.append(sig)
lines.append("")
lines += funcs
lines += ["", "#endif", ""]
out.write_text("\n".join(lines))

# Table used by the differential test: name -> wrapper, and config name -> field.
tab = ["/* Generated by tools/port_logic.py. Do not edit. */", '#include <stddef.h>', '#include "smz3_logic.h"', ""]
entries = []
for t, n, c in props:
    tab.append(f"static int w_P_{n}(const SmzProg *p, const SmzCfg *c, const int *a) {{ (void)c; (void)a; return P_{n}(p); }}")
    entries.append((f"P.{n}", f"w_P_{n}"))
for f in funcs:
    m = re.match(r"static inline bool (\w+)\(([^)]*)\)", f)
    name, params = m.groups()
    if params.startswith("const SmzWorld"):
        continue
    extra = [x for x in params.split(",")[2:] if x.strip()]
    args = ", ".join(f"a[{i}]" for i in range(len(extra)))
    tab.append(f"static int w_{name}(const SmzProg *p, const SmzCfg *c, const int *a) {{ (void)a; return {name}(p, c{', ' + args if args else ''}); }}")
    entries.append((name, f"w_{name}"))
tab.append("typedef int (*LogicFn)(const SmzProg *, const SmzCfg *, const int *);")
tab.append("static const struct { const char *name; LogicFn fn; } LOGIC_TABLE[] = {")
tab += [f'    {{ "{n}", {w} }},' for n, w in entries]
tab.append("};")
tab.append("static const struct { const char *name; size_t offset; } CFG_TABLE[] = {")
tab += [f'    {{ "{n}", offsetof(SmzCfg, {n}) }},' for _, n in cfg_fields]
tab.append("};")
(root / "core/gen/smz3_logic_table.h").write_text("\n".join(tab) + "\n")
print(f"{len(props)} properties, {len(funcs)} functions -> {out}")
for m in manual:
    print("MANUAL", m)
