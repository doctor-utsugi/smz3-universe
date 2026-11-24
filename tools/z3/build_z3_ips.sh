#!/usr/bin/env bash
# Builds KatDevsGames/z3randomizer at the commit alttpr pins (composer.json z3/randomizer, Rom::BUILD 2024-02-18) with
# asar 1.81 onto the ALttP JP 1.0 ROM, as UpdateBuildRecord does (asar --fix-checksum=off LTTP_RND_GeneralBugfixes.asm),
# checks the result against Rom::HASH and stores it as patches/z3_base.ips. Hosts embed it like sm_randomizer.ips:
#   python3 tools/z3/gen_z3_ips_c.py patches/z3_base.ips $(OUT)/z3_ips_data.c
# usage: tools/z3/build_z3_ips.sh [path/to/alttp_jp_v1.sfc]   (default rom/alttp_jp_v1.sfc)
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
COMMIT=dcb0a2b42d14445f7994a0e9e4d63cbecf4b98d3
JP_MD5=03a63945398191337e896e5771f77173
BASE_MD5=edc01f3db798ae4dfe21101311598d44
JP=${1:-$ROOT/rom/alttp_jp_v1.sfc}
SRC=$ROOT/vendor/z3randomizer
ASAR=${ASAR:-$ROOT/vendor/asar/asar-1.81/build/asar/asar-standalone}
[ "$(md5sum < "$JP" | cut -d' ' -f1)" = "$JP_MD5" ] || { echo "not the JP 1.0 ROM: $JP" >&2; exit 1; }
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
git -C "$SRC" archive "$COMMIT" | tar -x -C "$WORK"
cp "$JP" "$WORK/working.sfc"
(cd "$WORK" && "$ASAR" --fix-checksum=off LTTP_RND_GeneralBugfixes.asm working.sfc)
GOT=$(md5sum < "$WORK/working.sfc" | cut -d' ' -f1)
[ "$GOT" = "$BASE_MD5" ] || { echo "built ROM md5 $GOT, Rom::HASH is $BASE_MD5" >&2; exit 1; }
python3 "$ROOT/tools/z3/make_ips.py" "$JP" "$WORK/working.sfc" "$ROOT/patches/z3_base.ips"
mkdir -p "$ROOT/build/z3gen"
python3 "$ROOT/tools/z3/gen_z3_ips_c.py" "$ROOT/patches/z3_base.ips" "$ROOT/build/z3gen/z3_ips_data.c"
md5sum "$ROOT/patches/z3_base.ips"
