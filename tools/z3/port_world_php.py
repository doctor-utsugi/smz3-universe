#!/usr/bin/env python3
"""Transpiles alttpr's PHP world (app/Region/**, app/Boss.php, app/Item.php, config/logic.php) into
core/z3gen/z3_world_gen.{h,c}: items, locations, regions, the config the closures read, and every access closure.

The PHP twin of tools/port_world.py. It parses the closures into an expression tree instead of rewriting them with
regexes, since alttpr's closures are long nested PHP. World states: Open and Standard (World/Open.php, World/Standard.php);
the logic is the No Glitches closures, whose glitch switches (canOWYBA, canBootsClip, ...) become config flags.
What it cannot translate is listed as TODO and the script exits 1; the hand-written side is core/z3gen/z3_world.c.
"""
import re, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "vendor/alttp_php_randomizer"
APP = SRC / "app"
OUT_H = ROOT / "core/z3gen/z3_world_gen.h"
OUT_C = ROOT / "core/z3gen/z3_world_gen.c"
STATES = ["standard", "open"]
errors = []

# String-valued config keys: the first value is the default (RandomizerController::prepSeed's, World::factory's).
ENUMS = {
    "mode.state": ["standard", "open", "inverted", "retro"],
    "logic": ["NoGlitches", "OverworldGlitches", "HybridMajorGlitches", "MajorGlitches", "NoLogic"],
    "itemPlacement": ["basic", "advanced"],
    "dungeonItems": ["standard", "mc", "mcs", "full"],
    "accessibility": ["items", "locations", "none"],
    "goal": ["ganon", "fast_ganon", "dungeons", "pedestal", "triforce-hunt", "ganonhunt", "completionist"],
    "mode.weapons": ["randomized", "assured", "vanilla", "swordless"],
    "item.pool": ["normal", "hard", "expert", "superexpert", "crowd_control"],
    "item.functionality": ["normal", "hard", "expert", "superexpert"],
    "enemizer.enemyHealth": ["default", "easy", "hard", "expert"],
}
# Keys the hand-written ItemCollection helpers and World::__construct read, with their PHP defaults.
EXTRA_CFG = {"crystals.ganon": 7, "crystals.tower": 7, "item.require.Lamp": 1, "item.Goal.Required": 0,
             "rom.BottleFill.Magic": 0x80, "rom.CatchableFairies": 1, "rom.rupeeBow": 0,
             "ignoreCanKillEscapeThings": 0, "region.swordsInPool": 1, "item.overflow.count.Shield": 3}
# ItemCollection methods -> hand-written C helpers (z3_world.c); world arguments are dropped, numbers kept.
HELPERS = {"hasABottle", "canBunnyRevive", "canSpinSpeed", "canLiftRocks", "hasSword", "canBombThings", "canShootArrows",
           "canKillMostThings", "canLiftDarkRocks", "hasHealth", "hasBottle", "canExtendMagic", "canLightTorches", "canFly",
           "canKillEscapeThings", "hasArmor", "canBlockLasers", "canMeltThings", "canGetGoodBee", "canAcquireFairy",
           "glitchedLinkInDarkWorld"}
HELPER_DEFAULTS = {"hasSword": ["1"], "canShootArrows": ["1"], "hasArmor": ["1"], "hasBottle": ["1"], "canExtendMagic": ["2"],
                   "canKillMostThings": ["5"]}


# ---------------------------------------------------------------- tokenizer and parser (the PHP subset alttpr uses)
TOK = re.compile(r"""(?P<ws>\s+|//[^\n]*|\#[^\n]*|/\*.*?\*/)
 |(?P<var>\$[A-Za-z_]\w*)
 |(?P<num>0x[0-9A-Fa-f]+|\d+\.\d*|\d+)
 |(?P<str>'(?:[^'\\]|\\.)*'|"(?:[^"\\]|\\.)*")
 |(?P<id>\\?[A-Za-z_]\w*(?:\\[A-Za-z_]\w*)*)
 |(?P<op>===|!==|==|!=|>=|<=|&&|\|\||->|::|=>|\?\?|[-+*/<>!?:()\[\]{},;=.&|%^])""", re.S | re.X)


def tokenize(text):
    out, i = [], 0
    while i < len(text):
        m = TOK.match(text, i)
        if not m:
            raise SyntaxError(f"bad char {text[i:i+20]!r}")
        i = m.end()
        k = m.lastgroup
        if k == "ws":
            continue
        v = m.group(k)
        if k == "str":
            v = v[1:-1].replace("\\'", "'")
        out.append((k, v))
    out.append(("eof", None))
    return out


class Parser:
    def __init__(self, toks, pos=0):
        self.t, self.p = toks, pos

    def peek(self, o=0):
        return self.t[self.p + o]

    def at(self, v, o=0):
        k, x = self.t[self.p + o]
        return x == v and k in ("op", "id")

    def take(self, v=None):
        tok = self.t[self.p]
        if v is not None and not (tok[1] == v and tok[0] in ("op", "id")):
            raise SyntaxError(f"expected {v!r}, got {tok}")
        self.p += 1
        return tok

    def block(self):
        self.take("{")
        out = []
        while not self.at("}"):
            out.append(self.stmt())
        self.take("}")
        return out

    def stmt(self):
        if self.at("return"):
            self.take()
            e = None if self.at(";") else self.expr()
            self.take(";")
            return ("ret", e)
        if self.at("if"):
            self.take()
            self.take("(")
            c = self.expr()
            self.take(")")
            then = self.block() if self.at("{") else [self.stmt()]
            els = []
            if self.at("else"):
                self.take()
                els = self.block() if self.at("{") else [self.stmt()]
            return ("if", c, then, els)
        if self.at("{"):
            return ("blk", self.block())
        e = self.expr()
        if self.at("="):
            self.take()
            v = self.expr()
            self.take(";")
            return ("assign", e, v)
        self.take(";")
        return ("expr", e)

    def expr(self):
        c = self.binary(0)
        if self.at("?"):
            self.take()
            a = self.expr()
            self.take(":")
            b = self.expr()
            return ("tern", c, a, b)
        return c

    LEVELS = [["??"], ["||"], ["&&"], ["|"], ["&"], ["==", "!=", "===", "!=="], ["<", ">", "<=", ">="], ["+", "-", "."], ["*", "/", "%"]]

    def binary(self, lvl):
        if lvl == len(self.LEVELS):
            return self.unary()
        a = self.binary(lvl + 1)
        while self.peek()[0] == "op" and self.peek()[1] in self.LEVELS[lvl]:
            op = self.take()[1]
            a = ("bin", op, a, self.binary(lvl + 1))
        return a

    def unary(self):
        if self.at("!"):
            self.take()
            return ("un", "!", self.unary())
        if self.at("-"):
            self.take()
            return ("un", "-", self.unary())
        e = self.postfix()
        if self.at("instanceof"):
            self.take()
            return ("inst", e, self.take()[1])
        return e

    def args(self):
        self.take("(")
        out = []
        while not self.at(")"):
            out.append(self.expr())
            if self.at(","):
                self.take()
        self.take(")")
        return out

    def postfix(self):
        e = self.primary()
        while True:
            if self.at("->"):
                self.take()
                name = self.take()[1]
                e = ("mcall", e, name, self.args()) if self.at("(") else ("prop", e, name)
            elif self.at("::"):
                self.take()
                name = self.take()[1]
                e = ("scall", e[1], name, self.args()) if self.at("(") else ("sconst", e[1], name)
            elif self.at("["):
                self.take()
                i = self.expr()
                self.take("]")
                e = ("idx", e, i)
            elif self.at("(") and e[0] == "var":
                e = ("call", e[1], self.args())
            else:
                return e

    def primary(self):
        k, v = self.peek()
        if k == "op" and v == "(":
            self.take()
            e = self.expr()
            self.take(")")
            return e
        if k == "op" and v == "[":
            self.take()
            items = []
            while not self.at("]"):
                x = self.expr()
                if self.at("=>"):
                    self.take()
                    x = ("pair", x, self.expr())
                items.append(x)
                if self.at(","):
                    self.take()
            self.take("]")
            return ("arr", items)
        if k == "id" and v == "function":
            self.take()
            params = self.params()
            uses = []
            if self.at("use"):
                self.take()
                uses = self.params()
            return ("closure", params, uses, self.block())
        if k == "id" and v == "new":
            self.take()
            cls = self.take()[1]
            return ("new", cls, self.args())
        self.take()
        if k == "id" and self.at("("):
            return ("fcall", v, self.args())
        return (k, v)

    def params(self):
        self.take("(")
        out = []
        while not self.at(")"):
            tok = self.take()
            if tok[0] == "var":
                out.append(tok[1])
        self.take(")")
        return out


def find_method(toks, name):
    """Token index of the body '{' of method `name`, or None."""
    for i in range(len(toks) - 2):
        if toks[i] == ("id", "function") and toks[i + 1] == ("id", name):
            j = i + 2
            while toks[j] != ("op", "{"):
                j += 1
            return j
    return None


# ---------------------------------------------------------------- items, bosses, worlds
def c_ident(s):
    return re.sub(r"_+", "_", re.sub(r"\W", "_", s.replace("'", "").replace("-", "_"))).strip("_")


item_src = (APP / "Item.php").read_text()
ITEMS = []  # (name, class, first byte)
for m in re.finditer(r"new Item(?:\\([\w\\]+))?\('(\w+)', \[([^\]]*)\]", item_src):
    b = m.group(3).split(",")[0].strip()
    ITEMS.append((m.group(2), (m.group(1) or "Item").replace("\\", "_"), int(b, 16) if b.startswith("0x") else -1))
for m in re.finditer(r"new ItemAlias\('(\w+)', '(\w+)'", item_src):
    ITEMS.append((m.group(1), "Alias", -1))
ITEM_ID = {n: i for i, (n, _, _) in enumerate(ITEMS)}
ITEM_CLASSES = sorted({c for _, c, _ in ITEMS})

boss_toks = tokenize((APP / "Boss.php").read_text())
BOSSES = []  # (name, closure)
for i, tok in enumerate(boss_toks):
    if tok == ("id", "new") and boss_toks[i + 1] == ("id", "static") and boss_toks[i + 3][0] == "str":
        p = Parser(boss_toks, i)
        node = p.expr()
        BOSSES.append((node[2][0][1], node[2][2]))
BOSS_ID = {n: i for i, (n, _) in enumerate(BOSSES)}


def world_regions(state):
    text = (APP / f"World/{state.capitalize()}.php").read_text()
    return re.findall(r"'([^']+)' => new Region\\([\w\\]+)\(\$this\)", text)


REGION_KEYS = [k for k, _ in world_regions("open")]
CLASS_OF = {s: dict(world_regions(s)) for s in STATES}
assert all(list(CLASS_OF[s]) == REGION_KEYS for s in STATES), "region keys differ between world states"
REG_ID = {k: i for i, k in enumerate(REGION_KEYS)}


# ---------------------------------------------------------------- region classes
class RegionClass:
    def __init__(self, cls):
        self.cls = cls  # e.g. Standard\LightWorld\NorthEast
        self.cid = cls.replace("\\", "_")
        path = APP / "Region" / (cls.replace("\\", "/") + ".php")
        self.text = path.read_text()
        self.toks = tokenize(self.text)
        m = re.search(r"extends (?:Region\\)?([\w\\]+)", self.text)
        self.parent = m.group(1) if m and m.group(1) != "Region" else None
        self.name = None
        self.region_items = []
        self.locations = []  # (name, kind, addr, exists_cfg)
        self.prize = None
        self.prefill = {}
        self.bosses = {}
        self.shops = []
        self.init = None
        if self.parent:
            par = RegionClass(self.parent)
            self.name, self.region_items, self.locations = par.name, par.region_items, par.locations
            self.prize, self.prefill, self.bosses, self.shops = par.prize, par.prefill, par.bosses, par.shops
            self.init = par.init
        m = re.search(r"protected \$name = '([^']*)';", self.text)
        if m:
            self.name = m.group(1)
        m = re.search(r"protected \$region_items = \[(.*?)\];", self.text, re.S)
        if m:
            self.region_items = re.findall(r"'(\w+)'", m.group(1))
        ctor = find_method(self.toks, "__construct")
        if ctor is not None:
            self.read_ctor(Parser(self.toks, ctor).block(), None)
        init = find_method(self.toks, "initalize")
        if init is not None:
            body = Parser(self.toks, init).block()
            if any(s[0] == "expr" and s[1][0] == "scall" and s[1][2] == "initalize" for s in body):
                errors.append(f"{self.cid}: parent::initalize() not supported yet")
            self.init = body

    def read_ctor(self, stmts, cond):
        for s in stmts:
            if s[0] == "if":
                self.read_ctor(s[2], s[1])
            elif s[0] == "assign" and s[1] == ("prop", ("var", "$this"), "locations") and s[2][0] == "new":
                for loc in s[2][2][0][1]:
                    self.add_loc(loc, cond)
            elif s[0] == "assign" and s[1] == ("prop", ("var", "$this"), "shops") and s[2][0] == "new" and s[2][2]:
                self.shops += [x[2][0][1] for x in s[2][2][0][1]]
            elif s[0] == "assign" and s[1][0] == "prop" and s[1][2] in ("boss", "boss_top", "boss_middle", "boss_bottom"):
                self.bosses[s[1][2]] = s[2][3][0][1]
            elif s[0] == "assign" and s[1] == ("prop", ("var", "$this"), "prize_location"):
                self.prize = s[2][2][1]
            elif s[0] == "expr" and s[1][0] == "mcall" and s[1][2] == "addItem" and s[1][3][0][0] == "new":
                self.add_loc(s[1][3][0], cond)
            elif s[0] == "expr" and s[1][0] == "mcall" and s[1][2] == "setItem":
                tgt, item = s[1][1], s[1][3][0][3][0][1]
                name = self.prize if tgt == ("prop", ("var", "$this"), "prize_location") else tgt[2][1]
                self.prefill[name] = item

    def add_loc(self, node, cond):
        kind = node[1].replace("Location\\", "").replace("\\", "_")
        addrs = node[2][1][1]
        a = addrs[0] if addrs else ("id", "null")
        if a[0] == "tern":
            a = a[2]  # Blacksmith: the swordsInPool address
        addr = int(a[1], 16) if a[0] == "num" else -1
        exists = None
        if cond is not None:
            exists = cond[3][0][1] if cond[0] == "mcall" and cond[2] == "config" else None
            if exists is None:
                errors.append(f"{self.cid}: location condition not understood")
        self.locations.append((node[2][0][1], kind, addr, exists))


CLASSES = {}
for s in STATES:
    for k, c in CLASS_OF[s].items():
        if c not in CLASSES:
            CLASSES[c] = RegionClass(c)
for k in REGION_KEYS:
    a = [CLASSES[CLASS_OF[s][k]].locations for s in STATES]
    assert all(x == a[0] for x in a), f"{k}: locations differ between world states"

LOCS = []  # (name, kind, addr, region key, exists)
for k in REGION_KEYS:
    for name, kind, addr, ex in CLASSES[CLASS_OF["open"][k]].locations:
        LOCS.append((name, kind, addr, k, ex))
LOC_ID = {l[0]: i for i, l in enumerate(LOCS)}
SHOPS = [(n, k) for k in REGION_KEYS for n in CLASSES[CLASS_OF["open"][k]].shops]
SHOP_ENUM = {n: "Z3_SHOP_" + c_ident(n) for n, _ in SHOPS}
assert len(set(SHOP_ENUM.values())) == len(SHOPS)
LOC_ENUM = {l[0]: "Z3_LOC_" + c_ident(l[0]) for l in LOCS}
assert len(set(LOC_ENUM.values())) == len(LOCS)

# ---------------------------------------------------------------- config keys
CFG = {}  # key -> default (int) or enum list


def cfg_field(key):
    return c_ident(key.replace(".", "_"))


def cfg_enum(key, val):
    if key not in ENUMS or val not in ENUMS[key]:
        raise KeyError(f"config {key} compared with unknown value {val!r}")
    return f"Z3_{cfg_field(key).upper()}_{c_ident(val).upper()}"


def note_cfg(key, default, where):
    if key in ENUMS:
        CFG[key] = ENUMS[key]
        return
    d = {"true": 1, "false": 0}.get(default, default)
    if isinstance(d, str):
        d = int(d, 16) if d.startswith("0x") else int(d)
    if d is None:
        d = EXTRA_CFG.get(key, 0 if key.startswith("can") else None)  # glitch switches: off unless config/logic.php sets them
        if d is None:
            errors.append(f"{where}: config {key} read without a default")
            d = 0
    if key in CFG and CFG[key] != d:
        errors.append(f"{where}: config {key} has defaults {CFG[key]} and {d}")
    CFG[key] = d


for key, d in EXTRA_CFG.items():
    CFG.setdefault(key, d)
for key in ENUMS:
    CFG[key] = ENUMS[key]


# ---------------------------------------------------------------- closure -> C
class Ctx:
    def __init__(self, rc, key, where, kind, locals_=()):
        self.rc, self.key, self.where, self.kind, self.locals = rc, key, where, kind, set(locals_)


def is_world(n):
    return n == ("prop", ("var", "$this"), "world") or n == ("var", "$world")


def is_cfg(n):
    return n[0] == "mcall" and is_world(n[1]) and n[2] == "config"


def loc_of(n, ctx):
    """$this->locations["X"] / $locations["X"] -> location name."""
    if n[0] == "idx" and n[2][0] == "str" and (n[1] == ("var", "$locations") or n[1] == ("prop", ("var", "$this"), "locations")):
        if n[2][1] not in LOC_ID:
            raise KeyError(f"unknown location {n[2][1]}")
        return n[2][1]
    return None


def item_of(n):
    if n[0] == "scall" and n[1] == "Item" and n[2] == "get":
        return "Z3_ITEM_" + n[3][0][1]
    if n == ("var", "$item"):
        return "item"
    return None


def cx(n, ctx):
    t = n[0]
    if t == "num":
        return n[1]
    if t == "id":
        if n[1] in ("true", "false", "null"):
            return {"true": "1", "false": "0", "null": "0"}[n[1]]
        raise ValueError(f"bare identifier {n[1]}")
    if t == "un":
        return f"{n[1]}{cx(n[2], ctx)}"
    if t == "tern":
        return f"({cx(n[1], ctx)} ? {cx(n[2], ctx)} : {cx(n[3], ctx)})"
    if t == "bin":
        op, a, b = n[1], n[2], n[3]
        if op in ("==", "!=", "===", "!=="):
            cop = "==" if op in ("==", "===") else "!="
            for x, y in ((a, b), (b, a)):
                if is_cfg(x) and y[0] == "str":
                    key = x[3][0][1]
                    note_cfg(key, None, ctx.where)
                    return f"(w->cfg.{cfg_field(key)} {cop} {cfg_enum(key, y[1])})"
            if item_of(a) or item_of(b):
                return f"({item_of(a) or cx(a, ctx)} {cop} {item_of(b) or cx(b, ctx)})"
            return f"({cx(a, ctx)} {cop} {cx(b, ctx)})"
        if op in ("&&", "||", "+", "-", "*", "<", ">", "<=", ">="):
            return f"({cx(a, ctx)} {op} {cx(b, ctx)})"
        raise ValueError(f"operator {op}")
    if t == "inst":
        if n[1] != ("var", "$item"):
            raise ValueError("instanceof on something else than $item")
        return f"(Z3_ITEMS[item].cls == Z3_IC_{n[2].replace('Item' + chr(92), '').replace(chr(92), '_')})"
    if t == "var" and n[1] == "$item":
        return "item"
    if t == "fcall" and n[1] == "is_a" and n[2][0] == ("var", "$item") and n[2][1][0] == "sconst":
        return f"(Z3_ITEMS[item].cls == Z3_IC_{n[2][1][1].replace('Item' + chr(92), '').replace(chr(92), '_')})"
    if t == "fcall" and n[1] == "in_array":
        x, arr = n[2][0], n[2][1]
        if arr[0] != "arr":
            raise ValueError("in_array on a non-literal array")
        if is_cfg(x):
            key = x[3][0][1]
            note_cfg(key, None, ctx.where)
            return "(" + " || ".join(f"w->cfg.{cfg_field(key)} == {cfg_enum(key, v[1])}" for v in arr[1]) + ")"
        lhs = cx(x, ctx)
        return "(" + " || ".join(f"{lhs} == {item_of(v)}" for v in arr[1]) + ")"
    if t == "call":
        name = n[1][1:]
        if name not in ctx.locals:
            raise ValueError(f"call to unknown closure ${name}")
        return f"{ctx.rc.cid}_{name}(w, it)"
    if t == "mcall":
        obj, name, args = n[1], n[2], n[3]
        if obj == ("var", "$items"):
            if name == "has":
                item = args[0][1]
                if item not in ITEM_ID:
                    raise KeyError(f"unknown item {item}")
                cnt = cx(args[1], ctx) if len(args) > 1 else "1"
                return f"z3_has(it, Z3_ITEM_{item}, {cnt})"
            if name in HELPERS:
                # the world is optional for these three; without it PHP skips the config checks
                wa = "NULL" if name in ("canExtendMagic", "canBunnyRevive", "canAcquireFairy") and not (args and is_world(args[0])) else "w"
                rest = [cx(a, ctx) for a in args if not is_world(a) and a != ("id", "null")]
                rest += HELPER_DEFAULTS.get(name, [])[len(rest):]
                return f"z3_{name}({wa}, it" + "".join(", " + r for r in rest) + ")"
            raise ValueError(f"$items->{name}")
        if is_world(obj) and name == "config":
            key = args[0][1]
            note_cfg(key, args[1][1] if len(args) > 1 else None, ctx.where)
            return f"w->cfg.{cfg_field(key)}"
        if obj[0] == "mcall" and is_world(obj[1]) and obj[2] == "getRegion":
            k = obj[3][0][1]
            fn = {"canEnter": "z3_region_enter", "canComplete": "z3_region_complete"}[name]
            return f"{fn}(w, it, Z3_REG_{c_ident(k)})"
        if obj == ("var", "$this") and name in ("canEnter", "canComplete"):
            fn = {"canEnter": "z3_region_enter", "canComplete": "z3_region_complete"}[name]
            return f"{fn}(w, it, Z3_REG_{c_ident(ctx.key)})"
        if obj[0] == "prop" and obj[1] == ("var", "$this") and obj[2].startswith("boss") and name == "canBeat":
            slot = {"boss": f"w->boss_of[Z3_REG_{c_ident(ctx.key)}]", "boss_top": "w->gt_boss[0]",
                    "boss_middle": "w->gt_boss[1]", "boss_bottom": "w->gt_boss[2]"}[obj[2]]
            return f"z3_boss_beat(w, it, {slot})"
        if obj == ("var", "$locations") and name == "itemInLocations":
            item = item_of(args[0])
            cnt = cx(args[2], ctx) if len(args) > 2 else "1"
            return "((" + " + ".join(f"(w->item_at[{LOC_ENUM[x[1]]}] == {item})" for x in args[1][1]) + f") >= {cnt})"
        loc = loc_of(obj, ctx)
        if loc:
            L = LOC_ENUM[loc]
            if name == "canAccess":
                a = args[0]
                if a == ("var", "$items"):
                    return f"z3_loc_access(w, it, {L})"
                if a[0] == "mcall" and is_world(a[1]) and a[2] == "collectItems":
                    return f"z3_loc_access_collected(w, {L})"
                raise ValueError("canAccess with other items")
            if name == "hasItem":
                return f"(w->item_at[{L}] == {item_of(args[0])})" if args else f"(w->item_at[{L}] != Z3_ITEM_NONE)"
            if name == "getItem":
                return f"w->item_at[{L}]"
        raise ValueError(f"method {name} on {str(obj)[:60]}")
    raise ValueError(f"node {t}: {str(n)[:80]}")


def body_c(stmts, ctx, indent="    "):
    out = []
    for s in stmts:
        if s[0] == "ret":
            out.append(f"{indent}return {cx(s[1], ctx)};")
        elif s[0] == "if":
            out.append(f"{indent}if ({cx(s[1], ctx)}) {{")
            out += body_c(s[2], ctx, indent + "    ")
            if s[3]:
                out.append(f"{indent}}} else {{")
                out += body_c(s[3], ctx, indent + "    ")
            out.append(f"{indent}}}")
        else:
            raise ValueError(f"statement {s[0]}")
    return out


def closure_c(node, ctx):
    try:
        if node[0] != "closure":
            raise ValueError("not a closure")
        return body_c(node[3], ctx)
    except (ValueError, KeyError, IndexError, TypeError) as e:
        errors.append(f"{ctx.where}: {e}")
        return ["    return 0; /* TODO */"]


# ---------------------------------------------------------------- walk initalize() of every class
class Logic:
    def __init__(self, rc, key):
        self.rc, self.key = rc, key
        self.locals, self.enter, self.complete = {}, None, None
        self.req, self.fill, self.always, self.shop = {}, {}, {}, {}

    def walk(self, stmts):
        for s in stmts:
            self.stmt(s)

    def chain(self, n):
        """$this->locations["X"]->setRequirements(f)->setFillRules(g)... -> (target, [(method, arg)])."""
        calls = []
        while n[0] == "mcall" and n[2] in ("setRequirements", "setFillRules", "setAlwaysAllow"):
            calls.append((n[2], n[3][0]))
            n = n[1]
        return n, calls[::-1]

    def stmt(self, s):
        where = f"{self.rc.cid}"
        if s[0] == "ret":
            return
        if s[0] == "if":
            c = s[1]
            if not (is_cfg(c) and c[3][0][1] == "region.swordsInPool"):
                errors.append(f"{where}: initalize-level if not understood")
            self.walk(s[2])
            return
        if s[0] == "assign" and s[1][0] == "var" and s[2][0] == "closure":
            self.locals[s[1][1][1:]] = s[2]
            return
        if s[0] == "assign" and s[1] == ("prop", ("var", "$this"), "can_enter"):
            self.enter = s[2]
            return
        if s[0] == "assign" and s[1] == ("prop", ("var", "$this"), "can_complete"):
            self.complete = s[2]
            return
        if s[0] == "expr":
            tgt, calls = self.chain(s[1])
            if tgt[0] == "idx" and tgt[1] == ("prop", ("var", "$this"), "shops") and [c[0] for c in calls] == ["setRequirements"]:
                self.shop[tgt[2][1]] = calls[0][1]
                return
            if tgt == ("prop", ("var", "$this"), "prize_location"):
                name = self.rc.prize
            else:
                name = loc_of(tgt, None) if tgt[0] == "idx" else None
            if name and calls:
                for meth, arg in calls:
                    if arg == ("prop", ("var", "$this"), "can_complete"):
                        arg = ("complete",)
                    {"setRequirements": self.req, "setFillRules": self.fill, "setAlwaysAllow": self.always}[meth][name] = arg
                return
        errors.append(f"{where}: statement not understood: {str(s)[:100]}")


LOGIC = {}
for s in STATES:
    for k in REGION_KEYS:
        c = CLASS_OF[s][k]
        if c not in LOGIC:
            lg = Logic(CLASSES[c], k)
            lg.walk(CLASSES[c].init or [])
            LOGIC[c] = lg


# ---------------------------------------------------------------- emit
H = ["/* Generated by tools/z3/port_world_php.py from alttp_php_randomizer (sporchia, MIT). Do not edit. */",
     "#ifndef Z3_WORLD_GEN_H", "#define Z3_WORLD_GEN_H", "", "#include <stdbool.h>", "#include <stdint.h>", "",
     "typedef struct Z3World Z3World;", "typedef struct Z3Items Z3Items;", ""]
C = ["/* Generated by tools/z3/port_world_php.py from alttp_php_randomizer (sporchia, MIT). Do not edit. */",
     '#include "z3_world.h"', ""]


def enum(name, members, extra=()):
    H.append("enum {")
    for i, m in enumerate(members):
        H.append(f"    {m} = {i},")
    for e in extra:
        H.append(f"    {e},")
    H.append("};")
    H.append("")


enum("items", [f"Z3_ITEM_{n}" for n, _, _ in ITEMS], [f"Z3_ITEM_COUNT = {len(ITEMS)}", f"Z3_ITEM_NONE = 0xFF"])
enum("item classes", [f"Z3_IC_{c}" for c in ITEM_CLASSES], [f"Z3_IC_COUNT = {len(ITEM_CLASSES)}"])
enum("bosses", [f"Z3_BOSS_{c_ident(n)}" for n, _ in BOSSES], [f"Z3_BOSS_COUNT = {len(BOSSES)}", "Z3_BOSS_NONE = 0xFF"])
enum("regions", [f"Z3_REG_{c_ident(k)}" for k in REGION_KEYS], [f"Z3_REG_COUNT = {len(REGION_KEYS)}"])
enum("locations", [LOC_ENUM[l[0]] for l in LOCS], [f"Z3_LOC_COUNT = {len(LOCS)}"])
enum("shops", [SHOP_ENUM[n] for n, _ in SHOPS], [f"Z3_SHOP_COUNT = {len(SHOPS)}"])
H += ["typedef struct { const char *name; int cls, byte; } Z3ItemDef;",
      "typedef struct { const char *name, *kind; int region; int32_t addr; } Z3LocDef;",
      "typedef struct { const char *key, *name; int boss, prize; } Z3RegionDef;",
      "extern const Z3ItemDef Z3_ITEMS[Z3_ITEM_COUNT];", "extern const Z3LocDef Z3_LOCS[Z3_LOC_COUNT];",
      "extern const Z3RegionDef Z3_REGIONS[Z3_REG_COUNT];",
      "typedef struct { const char *name; int region; } Z3ShopDef;", "extern const Z3ShopDef Z3_SHOPS[Z3_SHOP_COUNT];", "extern const char *const Z3_BOSS_NAMES[Z3_BOSS_COUNT];", ""]

C.append("const Z3ItemDef Z3_ITEMS[Z3_ITEM_COUNT] = {")
for n, c, b in ITEMS:
    C.append(f'    {{ "{n}", Z3_IC_{c}, {b} }},')
C.append("};\n")
C.append("const Z3LocDef Z3_LOCS[Z3_LOC_COUNT] = {")
for name, kind, addr, k, _ in LOCS:
    C.append(f'    {{ "{name}", "{kind}", Z3_REG_{c_ident(k)}, {addr if addr < 0 else hex(addr)} }},')
C.append("};\n")
C.append("const Z3RegionDef Z3_REGIONS[Z3_REG_COUNT] = {")
for k in REGION_KEYS:
    rc = CLASSES[CLASS_OF["open"][k]]
    boss = rc.bosses.get("boss")
    C.append(f'    {{ "{k}", "{rc.name}", {"Z3_BOSS_" + c_ident(boss) if boss else "Z3_BOSS_NONE"}, {LOC_ENUM[rc.prize] if rc.prize else "-1"} }},')
C.append("};\n")
C.append("const Z3ShopDef Z3_SHOPS[Z3_SHOP_COUNT] = {")
for n, k in SHOPS:
    C.append(f'    {{ "{n}", Z3_REG_{c_ident(k)} }},')
C.append("};\n")
C.append("const char *const Z3_BOSS_NAMES[Z3_BOSS_COUNT] = {" + ", ".join(f'"{n}"' for n, _ in BOSSES) + "};\n")

# closures first, so the config keys are known before the struct is written
body = []
for c, lg in LOGIC.items():
    rc, key = lg.rc, lg.key
    for name, node in lg.locals.items():
        ctx = Ctx(rc, key, f"{rc.cid}.${name}", "req", lg.locals)
        body.append(f"static bool {rc.cid}_{name}(const Z3World *w, const Z3Items *it)\n{{\n    (void)w; (void)it;")
        body += closure_c(node, ctx) + ["}", ""]
    for what, node in (("enter", lg.enter), ("complete", lg.complete)):
        if node is None:
            continue
        ctx = Ctx(rc, key, f"{rc.cid}.can_{what}", "req", lg.locals)
        body.append(f"static bool {what}_{rc.cid}(const Z3World *w, const Z3Items *it)\n{{\n    (void)w; (void)it;")
        body += closure_c(node, ctx) + ["}", ""]
    for what, table, sig, dflt in (("shop", lg.shop, "int shop", "1"), ("req", lg.req, "int loc", "1"), ("fill", lg.fill, "int loc, int item", "1"),
                                   ("always", lg.always, "int loc, int item", "0")):
        if not table:
            continue
        body.append(f"static bool {what}_{rc.cid}(const Z3World *w, const Z3Items *it, {sig})\n{{")
        body.append("    (void)w; (void)it;" + (" (void)item;" if "item" in sig else ""))
        E = SHOP_ENUM if what == "shop" else LOC_ENUM
        body.append(f"    switch ({sig.split()[1].rstrip(',')}) {{")
        for loc, node in table.items():
            if node == ("complete",):
                body.append(f"    case {E[loc]}: return z3_region_complete(w, it, Z3_REG_{c_ident(key)});")
                continue
            ctx = Ctx(rc, key, f"{rc.cid}[{loc}].{what}", what, lg.locals)
            lines = closure_c(node, ctx)
            if len(lines) == 1 and lines[0].startswith("    return "):
                body.append(f"    case {E[loc]}: {lines[0].strip()}")
            else:
                body.append(f"    case {E[loc]}: {{")
                body += ["    " + x for x in lines]
                body.append("    }")
        body.append(f"    default: return {dflt};\n    }}\n}}\n")

# bosses
body.append("bool z3_boss_beat(const Z3World *w, const Z3Items *it, int boss)\n{\n    switch (boss) {")
for n, node in BOSSES:
    ctx = Ctx(None, None, f"Boss {n}", "boss")
    lines = closure_c(node, ctx)
    body.append(f"    case Z3_BOSS_{c_ident(n)}: {lines[0].strip()}" if len(lines) == 1 else "    /* TODO multi-line boss */")
body.append("    default: return false;\n    }\n}\n")


def dispatch(fn, what, sig, call, dflt):
    out = [f"bool {fn}(const Z3World *w, const Z3Items *it, {sig})\n{{"]
    out.append("    (void)w; (void)it;" + (" (void)item;" if "item" in sig else ""))
    out.append(f"    switch ({'Z3_LOCS[loc].region' if 'loc' in sig else 'Z3_SHOPS[shop].region' if 'shop' in sig else 'reg'}) {{")
    for k in REGION_KEYS:
        cls = {s: CLASS_OF[s][k] for s in STATES}
        has = {s: (getattr(LOGIC[c], what) if what in ("enter", "complete") else bool(getattr(LOGIC[c], what))) for s, c in cls.items()}
        target = {s: (f"{what}_{CLASSES[c].cid}({call})" if has[s] else dflt) for s, c in cls.items()}
        if len(set(target.values())) == 1:
            if target[STATES[0]] != dflt:
                out.append(f"    case Z3_REG_{c_ident(k)}: return {target[STATES[0]]};")
        else:
            expr = target[STATES[0]]
            for s in STATES[1:]:
                expr = f"w->cfg.mode_state == {cfg_enum('mode.state', s)} ? {target[s]} : {expr}"
            out.append(f"    case Z3_REG_{c_ident(k)}: return {expr};")
    out.append(f"    default: return {dflt};\n    }}\n}}\n")
    return out


body += dispatch("z3_region_enter", "enter", "int reg", "w, it", "true")
body += dispatch("z3_region_complete", "complete", "int reg", "w, it", "true")
body += dispatch("z3_loc_req", "req", "int loc", "w, it, loc", "true")
body += dispatch("z3_shop_req", "shop", "int shop", "w, it, shop", "true")
body += dispatch("z3_loc_fill", "fill", "int loc, int item", "w, it, loc, item", "true")
body += dispatch("z3_loc_always", "always", "int loc, int item", "w, it, loc, item", "false")

# region items, prefills, conditional locations
body.append("bool z3_region_item(int reg, int item)\n{\n    switch (reg) {")
for k in REGION_KEYS:
    ri = CLASSES[CLASS_OF["open"][k]].region_items
    if ri:
        body.append(f"    case Z3_REG_{c_ident(k)}: return " + " || ".join(f"item == Z3_ITEM_{x}" for x in ri) + ";")
body.append("    default: return false;\n    }\n}\n")
body.append("void z3_world_defaults(Z3World *w)\n{")
body.append("    for (int i = 0; i < Z3_LOC_COUNT; i++) w->item_at[i] = Z3_ITEM_NONE;")
for k in REGION_KEYS:
    rc = CLASSES[CLASS_OF["open"][k]]
    for loc, item in rc.prefill.items():
        body.append(f"    w->item_at[{LOC_ENUM[loc]}] = Z3_ITEM_{item};")
    b = rc.bosses.get("boss")
    body.append(f"    w->boss_of[Z3_REG_{c_ident(k)}] = {'Z3_BOSS_' + c_ident(b) if b else 'Z3_BOSS_NONE'};")
    for i, lvl in enumerate(("boss_top", "boss_middle", "boss_bottom")):
        if lvl in rc.bosses:
            body.append(f"    w->gt_boss[{i}] = Z3_BOSS_{c_ident(rc.bosses[lvl])};")
body.append("}\n")
body.append("bool z3_loc_exists(const Z3World *w, int loc)\n{\n    (void)w;\n    switch (loc) {")
for name, _, _, _, ex in LOCS:
    if ex:
        note_cfg(ex, None, name)
        body.append(f"    case {LOC_ENUM[name]}: return w->cfg.{cfg_field(ex)};")
body.append("    default: return true;\n    }\n}\n")

# config struct, defaults and logic presets (config/logic.php)
for key in sorted(CFG):
    if key in ENUMS:
        enum(key, [cfg_enum(key, v) for v in ENUMS[key]])
H.append("/* World::config() keys the closures and helpers read; enums for the string-valued ones. */")
H.append("typedef struct {")
for key in sorted(CFG):
    H.append(f"    int {cfg_field(key)};  /* {key} */")
H.append("} Z3Cfg;\n")
C.append("void z3_cfg_defaults(Z3Cfg *c)\n{")
for key in sorted(CFG):
    v = CFG[key]
    C.append(f"    c->{cfg_field(key)} = {cfg_enum(key, v[0]) if isinstance(v, list) else v};")
C.append("}\n")
logic = (SRC / "config/logic.php").read_text()
C.append("void z3_cfg_logic(Z3Cfg *c)\n{\n    switch (c->logic) {")
for m in re.finditer(r"'(\w+)' => \[(.*?)\]", logic, re.S):
    sets = re.findall(r"'(\w+)' => (true|false)", m.group(2))
    if not sets:
        continue
    C.append(f"    case {cfg_enum('logic', m.group(1))}:")
    for k, v in sets:
        if k not in CFG:
            CFG_EXTRA_NOTE = k
        C.append(f"        c->{cfg_field(k)} = {1 if v == 'true' else 0};" if k in CFG else f"        /* {k}: not read by the ported logic */")
    C.append("        break;")
C.append("    default:\n        break;\n    }\n}\n")
C += body

H += ["void z3_cfg_defaults(Z3Cfg *c);", "void z3_cfg_logic(Z3Cfg *c);", "void z3_world_defaults(Z3World *w);",
      "bool z3_region_enter(const Z3World *w, const Z3Items *it, int reg);",
      "bool z3_region_complete(const Z3World *w, const Z3Items *it, int reg);",
      "bool z3_loc_req(const Z3World *w, const Z3Items *it, int loc);",
      "bool z3_loc_fill(const Z3World *w, const Z3Items *it, int loc, int item);",
      "bool z3_loc_always(const Z3World *w, const Z3Items *it, int loc, int item);",
      "bool z3_loc_exists(const Z3World *w, int loc);",
      "bool z3_shop_req(const Z3World *w, const Z3Items *it, int shop);",
      "bool z3_boss_beat(const Z3World *w, const Z3Items *it, int boss);",
      "bool z3_region_item(int reg, int item);", "", "#endif"]
OUT_H.write_text("\n".join(H) + "\n")
OUT_C.write_text("\n".join(C) + "\n")
print(f"{len(ITEMS)} items, {len(BOSSES)} bosses, {len(REGION_KEYS)} regions, {len(LOCS)} locations, {len(CFG)} config keys")
for e in errors:
    print("TODO", e)
sys.exit(1 if errors else 0)
