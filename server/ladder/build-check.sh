#!/bin/sh
# Builds smz3check (the proof-of-run verifier) next to server.js from the repository's C core: the SMZ3 generator and,
# for Super Metroid runs, the standalone one (core/smgen, with its base patch packed by tools/gen_sm_ips_c.py).
set -e
cd "$(dirname "$0")/../.."
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
python3 tools/gen_sm_ips_c.py patches/sm_randomizer.ips "$TMP/sm_ips_data.c" >/dev/null
gcc -std=gnu11 -O2 -w -Icore -Icore/gen -Icore/smgen -Icore/ui $(ls core/gen/*.c | grep -v 'gen_rom\|test_') $(ls core/ui/*.c | grep -v 'test_\|smz3_host') \
    core/smgen/*.c "$TMP/sm_ips_data.c" \
    server/ladder/smz3check.c core/smz3_locations.c core/smz3_state.c core/smz3_inventory.c -o "${1:-server/ladder/smz3check}" -lm -lpthread
