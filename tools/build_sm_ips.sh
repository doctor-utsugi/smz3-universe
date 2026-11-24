#!/usr/bin/env bash
# Builds tewtal's sm_randomizer_rom (vendor/tewtal-SMZ3Randomizer/sm_randomizer_rom) with asar 1.81, as its Dockerfile
# does, into patches/sm_randomizer.ips. Hosts embed it at build time like ips_data.c (frontends/pc/Makefile):
#   python3 tools/gen_sm_ips_c.py patches/sm_randomizer.ips $(OUT)/sm_ips_data.c
# asar: vendor/asar/asar-1.81/build/asar/asar-standalone (https://github.com/RPGHacker/asar/archive/v1.81.tar.gz, cmake src).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC=$ROOT/vendor/tewtal-SMZ3Randomizer/sm_randomizer_rom
ASAR=${ASAR:-$ROOT/vendor/asar/asar-1.81/build/asar/asar-standalone}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cp -r "$SRC" "$WORK/rom"
rm -rf "$WORK/rom/.git"
cp "$ASAR" "$WORK/rom/resources/asar"
(cd "$WORK/rom" && bash ./build.sh)
cp "$WORK/rom/build/sm.ips" "$ROOT/patches/sm_randomizer.ips"
mkdir -p "$ROOT/build/smgen"
python3 "$ROOT/tools/gen_sm_ips_c.py" "$ROOT/patches/sm_randomizer.ips" "$ROOT/build/smgen/sm_ips_data.c"
md5sum "$ROOT/patches/sm_randomizer.ips"
