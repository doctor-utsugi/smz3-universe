#!/usr/bin/env python3
"""Writes core/z3gen/z3_romdata_gen.{h,c}: what alttpr's ROM writer needs that z3_world_gen leaves out.

Every ROM address of each location (Location::$address, keys 0..6 plus the medallion 't0'/'m0' keys), every byte of
each item (Item::$bytes), the shops (Shop constructor arguments, TakeAny extra writes, default inventory), and the
regions' prize music addresses and map reveal bits. Read from app/Item.php and the Standard region classes (Open only
swaps the Escape closures) plus the locations Inverted's classes add; a location Inverted moves keeps its address and
gets it under each of its z3_world_gen ids. Also Rom::setInvertedMode's writes and texts, read off Rom.php.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "vendor/alttp_php_randomizer/app"
OUT_H = ROOT / "core/z3gen/z3_romdata_gen.h"
OUT_C = ROOT / "core/z3gen/z3_romdata_gen.c"


def c_ident(s):
    return re.sub(r"_+", "_", re.sub(r"\W", "_", s.replace("'", "").replace("-", "_"))).strip("_")


def num(s):
    s = s.strip()
    return None if s == "null" else int(s, 16) if s.lower().startswith("0x") else int(s)


def php_list(body):
    """'null, 0x12, 't0' => 0x31' -> {0: None, 1: 0x12, 't0': 0x31}"""
    out, i = {}, 0
    for part in [p.strip() for p in body.split(",") if p.strip()]:
        m = re.match(r"'(\w+)'\s*=>\s*(\S+)", part)
        if m:
            out[m.group(1)] = num(m.group(2))
        else:
            out[i] = num(part)
            i += 1
    return out


# ---------------------------------------------------------------- items
item_src = (APP / "Item.php").read_text()
ITEMS = []
for m in re.finditer(r"new Item(?:\\[\w\\]+)?\('(\w+)', \[([^\]]*)\]", item_src):
    ITEMS.append((m.group(1), php_list(m.group(2))))
ALIASES = re.findall(r"new ItemAlias\('(\w+)', '(\w+)'", item_src)
BYTES = dict(ITEMS)
ITEMS += [(a, BYTES[t]) for a, t in ALIASES]   # an alias writes its target's bytes (ItemAlias::getBytes)

# ---------------------------------------------------------------- regions, locations, shops
world_src = (APP / "World/Standard.php").read_text()
REGIONS = re.findall(r"'([^']+)' => new Region\\([\w\\]+)\(\$this\)", world_src)
INVERTED = re.findall(r"'([^']+)' => new Region\\([\w\\]+)\(\$this\)", (APP / "World/Inverted.php").read_text())
LOCS, SHOPS, REG = [], [], []
for key, cls in REGIONS + [(k, c) for k, c in INVERTED if c.startswith("Inverted")]:
    inverted = cls.startswith("Inverted")
    text = (APP / "Region" / (cls.replace("\\", "/") + ".php")).read_text()
    m = re.search(r"\$music_addresses = \[([^\]]*)\]", text)
    music = [num(x) for x in m.group(1).split(",") if x.strip()] if m else []
    m = re.search(r"\$map_reveal = (0x[0-9A-Fa-f]+)", text)
    if not inverted:   # Inverted's region classes extend Standard's: same music and map bits, no shops of their own
        REG.append((key, music, int(m.group(1), 16) if m else 0))
    for m in re.finditer(r"new (?:Location\\)?([\w\\]+)\(\"([^\"]+)\", \[([^\]]*)\]", text):
        body = m.group(3)
        if "?" in body:   # Blacksmith: swordsInPool ? 0x18002A : 0x3355C, the default (true) branch
            body = body.split("?")[1].split(":")[0]
        LOCS.append((m.group(2), php_list(body)))
    for m in [] if inverted else re.finditer(r"new Shop(?:\\(\w+))?\(\"([^\"]+)\", (0x\w+), (0x\w+), (0x\w+), (0x\w+), \$this(?:, \[(0x\w+) => \[(0x\w+)\]\])?\)", text):
        kind, name = m.group(1) or "", m.group(2)
        inv = []
        sm = re.search(r'\$this->shops\["' + re.escape(name) + r'"\]->clearInventory\(\)([^;]*);', text)
        if sm:
            for a in re.finditer(r"addInventory\((\d+), Item::get\('(\w+)', \$world\), (\d+)(?:, (\d+))?\)", sm.group(1)):
                inv.append((int(a.group(1)), a.group(2), int(a.group(3)), int(a.group(4) or 0)))
        SHOPS.append((name, kind, [num(m.group(i)) for i in range(3, 7)],
                      (num(m.group(7)), num(m.group(8))) if m.group(7) else (-1, 0), inv))

# ---------------------------------------------------------------- config/item.php pools, in file order
cfg_src = (APP.parent / "config/item.php").read_text()
POOLS = {}
for sec in ("advancement", "nice", "junk", "dungeon"):
    body = re.search(r"'" + sec + r"' => \[(.*?)\n    \]", cfg_src, re.S).group(1)
    POOLS[sec] = [(n, int(c)) for n, c in re.findall(r"^\s*'(\w+)' => (\d+),", body, re.M)]

# ---------------------------------------------------------------- Rom::setInvertedMode's own writes and texts
def snes_to_pc(a):
    return ((a & 0x7F0000) >> 1) | (a & 0x7FFF)


rom_src = (APP / "Rom.php").read_text()
inv = rom_src[rom_src.index("public function setInvertedMode("):]
inv = inv[:inv.index("\n    }\n")]
INV_WRITES = []   # (pc address, bytes) in call order; setOpenMode and setFixFakeWorld are z3_rom.c's
for m in re.finditer(r"\$this->write\((.*?)\);[ \t]*(?://[^\n]*)?\n", inv, re.S):
    arg, depth = m.group(1), 0
    for i, ch in enumerate(arg):
        depth += {"(": 1, ")": -1}.get(ch, 0)
        if ch == "," and depth == 0:
            break
    addr = eval(arg[:i].replace("snes_to_pc", "snes_to_pc"), {"snes_to_pc": snes_to_pc})
    data = []
    for pm in re.finditer(r"pack\(\s*'(\w)\*?'\s*,(.*?)\)(?=\s*(?:\.|$))", arg[i + 1:], re.S):
        for v in (int(x, 0) for x in pm.group(2).split(",") if x.strip()):
            data += [v & 0xFF] if pm.group(1) == "C" else [v & 0xFF, v >> 8 & 0xFF]
    assert data, arg
    INV_WRITES.append((addr, data))
assert len(INV_WRITES) == inv.count("$this->write("), "setInvertedMode: a write was not read"
INV_TEXTS = []   # (Text key, string, pause)
for m in re.finditer(r"\$this->text->setString\('(\w+)', (.*?)(, false)?\);\n", inv, re.S):
    parts = re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(2))
    INV_TEXTS.append((m.group(1), "".join(parts).replace("\\n", "\n").replace("\\'", "'"), not m.group(3)))
assert len(INV_TEXTS) == inv.count("setString(")

# ---------------------------------------------------------------- emit
GEN = "/* Generated by tools/z3/gen_z3_romdata.py from alttp_php_randomizer (sporchia, MIT). Do not edit. */"
H = [GEN, "#ifndef Z3_ROMDATA_GEN_H", "#define Z3_ROMDATA_GEN_H", "", "#include <stdbool.h>", '#include "z3_world_gen.h"', "",
     "/* Location::$address: addr[k] for key k (-1 = null), ext[] the medallion 't0'..'t2' or 'm0'..'m2' addresses. */",
     "typedef struct { int32_t addr[7]; uint8_t n; char ext_key; int32_t ext[3]; } Z3LocRom;",
     "/* Item::$bytes: b[k] for key k (-1 = null), t[]/m[] the medallion keys. Aliases carry their default target's. */",
     "typedef struct { int16_t b[7]; uint8_t n; uint8_t t[3], m[3]; } Z3ItemRom;",
     "enum { Z3_SHOP_KIND_SHOP = 0, Z3_SHOP_KIND_TAKEANY = 1, Z3_SHOP_KIND_UPGRADE = 2 };",
     "typedef struct { uint8_t item, max; uint16_t price; } Z3ShopSlot;",
     "/* Shop constructor arguments, TakeAny's extra write (addr -1 = none) and the default inventory by slot. */",
     "typedef struct { uint8_t kind, config, keeper; uint16_t room; uint8_t door; int32_t write_addr; uint8_t write_val;",
     "                 uint8_t n_inv; Z3ShopSlot inv[3]; } Z3ShopRom;",
     "typedef struct { int32_t music[8]; uint8_t n_music; uint16_t map_reveal; } Z3RegionRom;",
     "extern const Z3LocRom Z3_LOC_ROM[Z3_LOC_COUNT];",
     "extern const Z3ItemRom Z3_ITEM_ROM[Z3_ITEM_COUNT];",
     "extern const Z3ShopRom Z3_SHOP_ROM[Z3_SHOP_COUNT];",
     "extern const Z3RegionRom Z3_REGION_ROM[Z3_REG_COUNT];", "",
     "/* config/item.php item.advancement/nice/junk/dungeon in file order; item Z3_ITEM_NONE is BottleWithRandom. */",
     "typedef struct { uint8_t item, count; } Z3PoolEntry;"]
for sec, ents in POOLS.items():
    H.append(f"#define Z3_POOL_{sec.upper()}_LEN {len(ents)}")
    H.append(f"extern const Z3PoolEntry Z3_POOL_{sec.upper()}[Z3_POOL_{sec.upper()}_LEN];")
H += ["", "/* Rom::setInvertedMode: its writes in call order (Z3_INV_DATA[off .. off + n)) and the texts it sets. */",
      "typedef struct { int32_t addr; uint16_t off, n; } Z3RomWrite;",
      f"#define Z3_INV_WRITES_LEN {len(INV_WRITES)}", "extern const Z3RomWrite Z3_INV_WRITES[Z3_INV_WRITES_LEN];",
      "extern const uint8_t Z3_INV_DATA[];", "typedef struct { int key; const char *s; bool pause; } Z3RomText;",
      f"#define Z3_INV_TEXTS_LEN {len(INV_TEXTS)}", "extern const Z3RomText Z3_INV_TEXTS[Z3_INV_TEXTS_LEN];",
      "", "#endif", ""]
C = [GEN, '#include <stdbool.h>', '#include "z3_romdata_gen.h"', '#include "z3_text_gen.h"', ""]


def h(v):
    return "-1" if v is None else hex(v)


# z3_world_gen.c's location ids by name: a location Inverted moves has one id per region it sits in
world_c = (ROOT / "core/z3gen/z3_world_gen.c").read_text()
gen_h = (ROOT / "core/z3gen/z3_world_gen.h").read_text()
loc_names = [n.replace('\\"', '"') for n in re.findall(r'\{ "((?:[^"\\]|\\.)*)", "', re.search(
    r"Z3_LOCS\[Z3_LOC_COUNT\] = \{(.*?)\n\};", world_c, re.S).group(1))]
loc_ids = re.findall(r"(Z3_LOC_\w+) = \d+,", gen_h)
IDS = {}
for n, e in zip(loc_names, loc_ids):
    IDS.setdefault(n, []).append(e)
seen = {}
for name, a in LOCS:
    assert seen.setdefault(name, a) == a, f"{name}: addresses differ between world states"
LOCS = list(seen.items())
assert set(seen) == set(IDS), set(seen) ^ set(IDS)
C.append("const Z3LocRom Z3_LOC_ROM[Z3_LOC_COUNT] = {")
for name, a in LOCS:
    idx = [a.get(i) for i in range(7) if i in a]
    ext = [k for k in a if isinstance(k, str)]
    ek = ext[0][0] if ext else ""
    ea = [a.get(f"{ek}{i}") for i in range(3)] if ext else [None] * 3
    for e in IDS[name]:
        C.append(f"    [{e}] = {{ {{ {', '.join(h(x) for x in idx + [None] * (7 - len(idx)))} }}, {len(idx)}, "
                 f"{repr(ek) if ek else 0}, {{ {', '.join(h(x) for x in ea)} }} }},")
C.append("};\n")
C.append("const Z3ItemRom Z3_ITEM_ROM[Z3_ITEM_COUNT] = {")
for name, b in ITEMS:
    idx = [b.get(i) for i in range(7) if i in b]
    t = [b.get(f"t{i}", 0) for i in range(3)]
    mm = [b.get(f"m{i}", 0) for i in range(3)]
    C.append(f"    [Z3_ITEM_{name}] = {{ {{ {', '.join(h(x) for x in idx + [None] * (7 - len(idx)))} }}, {len(idx)}, "
             f"{{ {', '.join(hex(x) for x in t)} }}, {{ {', '.join(hex(x) for x in mm)} }} }},")
C.append("};\n")
C.append("const Z3ShopRom Z3_SHOP_ROM[Z3_SHOP_COUNT] = {")
KIND = {"": "Z3_SHOP_KIND_SHOP", "TakeAny": "Z3_SHOP_KIND_TAKEANY", "Upgrade": "Z3_SHOP_KIND_UPGRADE"}
for name, kind, (cfg, keeper, room, door), (wa, wv), inv in SHOPS:
    slots = {s: (it, p, mx) for s, it, p, mx in inv}
    n = max(slots) + 1 if slots else 0
    cells = [f"{{ Z3_ITEM_{slots[s][0]}, {slots[s][2]}, {slots[s][1]} }}" if s in slots else "{ Z3_ITEM_NONE, 0, 0 }"
             for s in range(3)]
    C.append(f"    [Z3_SHOP_{c_ident(name)}] = {{ {KIND[kind]}, {hex(cfg)}, {hex(keeper)}, {hex(room)}, {hex(door)}, "
             f"{h(wa) if wa >= 0 else -1}, {hex(wv)}, {n}, {{ {', '.join(cells)} }} }},")
C.append("};\n")
C.append("const Z3RegionRom Z3_REGION_ROM[Z3_REG_COUNT] = {")
for key, music, reveal in REG:
    ms = [hex(x) for x in music] + ["0"] * (8 - len(music))
    C.append(f"    [Z3_REG_{c_ident(key)}] = {{ {{ {', '.join(ms)} }}, {len(music)}, {hex(reveal)} }},")
C.append("};\n")
for sec, ents in POOLS.items():
    C.append(f"const Z3PoolEntry Z3_POOL_{sec.upper()}[Z3_POOL_{sec.upper()}_LEN] = {{")
    for n, c in ents:
        C.append(f"    {{ {'Z3_ITEM_NONE' if n == 'BottleWithRandom' else 'Z3_ITEM_' + n}, {c} }},")
    C.append("};\n")

blob, rows = [], []
for a, d in INV_WRITES:
    rows.append(f"    {{ {hex(a)}, {len(blob)}, {len(d)} }},")
    blob += d
C += ["const Z3RomWrite Z3_INV_WRITES[Z3_INV_WRITES_LEN] = {"] + rows + ["};", ""]
C.append("const uint8_t Z3_INV_DATA[] = {")
C += ["    " + ", ".join(f"0x{b:02X}" for b in blob[i:i + 16]) + "," for i in range(0, len(blob), 16)]
C += ["};", "", "const Z3RomText Z3_INV_TEXTS[Z3_INV_TEXTS_LEN] = {"]


def cstr(t):
    return '"' + t.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


C += [f"    {{ Z3_TXT_{k}, {cstr(t)}, {'true' if p else 'false'} }}," for k, t, p in INV_TEXTS] + ["};", ""]
OUT_H.write_text("\n".join(H))
OUT_C.write_text("\n".join(C))
print(f"{len(LOCS)} locations, {len(ITEMS)} items, {len(SHOPS)} shops, {len(REG)} regions -> {OUT_C}")
