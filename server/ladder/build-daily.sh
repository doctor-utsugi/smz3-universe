#!/bin/sh
# Builds smz3daily (the daily challenge generator) next to server.js from the C core: the SMZ3 generator, core/smgen
# for --sm and core/z3gen (through core/ui/smz3_z3.c) for --z3.
set -e
cd "$(dirname "$0")/../.."
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
python3 tools/z3/gen_z3_ips_c.py patches/z3_base.ips "$TMP/z3_ips_data.c" >/dev/null
gcc -std=gnu11 -O2 -w -Icore -Icore/gen -Icore/smgen -Icore/z3gen -Icore/ui $(ls core/gen/*.c | grep -v 'gen_rom\|test_') core/ui/smz3_runlogic.c \
    core/smgen/sm_fill.c core/smgen/sm_world.c core/smgen/sm_world_gen.c core/z3gen/*.c "$TMP/z3_ips_data.c" core/ui/smz3_z3.c core/ui/smz3_run.c \
    server/ladder/smz3daily.c server/ladder/smz3daily_sm.c server/ladder/smz3daily_z3.c core/smz3_locations.c core/smz3_state.c -o "${1:-server/ladder/smz3daily}" -lm -lpthread
