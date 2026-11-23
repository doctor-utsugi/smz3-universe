"""Parser for the Cas' tracker region files: locations, rooms and region members as plain Python data."""
import re, glob
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REGIONS = ROOT / "vendor/SMZ3Randomizer/src/TrackerCouncil.Smz3.Data/WorldData/Regions"


def match_close(s, i, open_c="(", close_c=")"):
    """Index just past the bracket that closes the one at s[i]; skips strings and comments."""
    depth = 0
    n = len(s)
    while i < n:
        c = s[i]
        if c == '"':
            i += 1
            while s[i] != '"':
                i += 2 if s[i] == "\\" else 1
        elif s.startswith("//", i):
            i = s.index("\n", i)
            continue
        elif s.startswith("/*", i):
            i = s.index("*/", i) + 1
        elif c == open_c:
            depth += 1
        elif c == close_c:
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    raise ValueError("unbalanced")


def split_args(s):
    """Splits at top-level commas."""
    out, depth, cur, i = [], 0, [], 0
    while i < len(s):
        c = s[i]
        if c == '"':
            j = i + 1
            while s[j] != '"':
                j += 2 if s[j] == "\\" else 1
            cur.append(s[i:j + 1]); i = j + 1; continue
        if s.startswith("//", i):
            j = s.index("\n", i); i = j; continue
        if s.startswith("/*", i):
            j = s.index("*/", i) + 2; i = j; continue
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        if c == "," and depth == 0:
            out.append("".join(cur).strip()); cur = []
        else:
            cur.append(c)
        i += 1
    if "".join(cur).strip():
        out.append("".join(cur).strip())
    return out


def strip_comments(s):
    s = re.sub(r"/\*.*?\*/", "", s, flags=re.S)
    return re.sub(r"//[^\n]*", "", s)


def parse_location_call(text, start):
    """text[start:] begins at 'new Location('. Returns (named args dict, positional list, end index)."""
    op = text.index("(", start)
    end = match_close(text, op)
    args = split_args(text[op + 1:end - 1])
    pos, named = [], {}
    for a in args:
        m = re.match(r"(\w+):\s*(.*)$", a, re.S)
        if m and not a.startswith("new "):
            named[m.group(1)] = m.group(2).strip()
        else:
            pos.append(a)
    return named, pos, end


def parse_chain(text, i):
    """Method chain right after a call: .Weighted(x).Allow(...) -> [(name, argtext)]."""
    out = []
    while True:
        m = re.match(r"\s*\.(\w+)\(", text[i:])
        if not m:
            return out
        op = i + m.end() - 1
        end = match_close(text, op)
        out.append((m.group(1), text[op + 1:end - 1].strip()))
        i = end


def parse_file(path):
    raw = Path(path).read_text(encoding="utf-8-sig")
    s = raw
    cls = re.search(r"public (?:abstract )?class (\w+)\s*:\s*([^\n{]+)", s)
    info = {"file": Path(path).name, "class": cls.group(1), "bases": [b.strip() for b in cls.group(2).split(",")], "locations": [], "rooms": []}
    classes = [(m.start(), m.group(1), m.group(2)) for m in re.finditer(r"class (\w+)\s*:\s*(\w+)", s)]
    for m in re.finditer(r"(?:(\w+)\s*=\s*)?new Location\(", s):
        named, pos, end = parse_location_call(s, m.start() + len(m.group(0)) - len("new Location("))
        enclosing = [c for c in classes if c[0] < m.start()][-1]
        room = enclosing[1] if enclosing[2] == "Room" else None
        # first positional: this | room variable, then LocationId, rom address, type
        info["locations"].append({"prop": m.group(1), "room": room, "owner": pos[0], "id": pos[1].split(".")[-1], "rom": pos[2], "type": pos[3].split(".")[-1], "named": named,
                                  "chain": parse_chain(s, end)})
    return info, s
