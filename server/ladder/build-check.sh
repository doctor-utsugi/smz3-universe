#!/bin/sh
# Builds smz3check (the proof-of-run verifier) next to server.js from the C core: the SMZ3 generator, core/smgen for
# Super Metroid runs and core/z3gen for A Link to the Past runs, with their base patches packed into C.
set -e
cd "$(dirname "$0")/../.."
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
python3 tools/gen_sm_ips_c.py patches/sm_randomizer.ips "$TMP/sm_ips_data.c" >/dev/null
python3 tools/z3/gen_z3_ips_c.py patches/z3_base.ips "$TMP/z3_ips_data.c" >/dev/null
gcc -std=gnu11 -O2 -w -Icore -Icore/gen -Icore/smgen -Icore/z3gen -Icore/ui $(ls core/gen/*.c | grep -v 'gen_rom\|test_') $(ls core/ui/*.c | grep -v 'test_\|smz3_host') \
    core/smgen/*.c "$TMP/sm_ips_data.c" core/z3gen/*.c "$TMP/z3_ips_data.c" \
    server/ladder/smz3check.c core/smz3_locations.c core/smz3_state.c core/smz3_inventory.c -o "${1:-server/ladder/smz3check}" -lm -lpthread
