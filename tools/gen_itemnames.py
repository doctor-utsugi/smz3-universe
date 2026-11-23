#!/usr/bin/env python3
"""Writes core/gen/smz3_itemnames.h (ItemType name -> number) from smz3_enums.h."""
import re
from pathlib import Path
root = Path(__file__).resolve().parents[1]
src = (root / "core/gen/smz3_enums.h").read_text()
items = re.findall(r'^\s*ITEM_(\w+)\s*=\s*(\d+)', src, re.M)
o = ['/* Generated from smz3_enums.h by tools/gen_itemnames.py. Do not edit. */',
     'typedef struct { const char *name; int id; } SmzItemName;', 'static const SmzItemName SMZ_ITEM_NAMES[] = {']
o += ['    { "%s", %s },' % (n, v) for n, v in items]
o += ['};', '#define SMZ_ITEM_NAME_COUNT %d' % len(items)]
locs = re.findall(r'^\s*LOC_(\w+)\s*=\s*(\d+)', src, re.M)
o += ['typedef struct { const char *name; int id; } SmzLocName;', 'static const SmzLocName SMZ_LOC_NAMES[] = {']
o += ['    { "%s", %s },' % (n, v) for n, v in locs if n != 'COUNT_']
o += ['};', '#define SMZ_LOC_NAME_COUNT %d' % len([1 for n, v in locs if n != 'COUNT_'])]
(root / "core/gen/smz3_itemnames.h").write_text('\n'.join(o) + '\n')
