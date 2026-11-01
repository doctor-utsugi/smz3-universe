#!/bin/sh
# Builds smz3check (the proof-of-run verifier) next to server.js from the repository's C core.
set -e
cd "$(dirname "$0")/../.."
gcc -std=gnu11 -O2 -w -Icore -Icore/gen -Icore/ui $(ls core/gen/*.c | grep -v 'gen_rom\|test_') $(ls core/ui/*.c | grep -v 'test_\|smz3_host') \
    server/ladder/smz3check.c core/smz3_locations.c core/smz3_state.c core/smz3_inventory.c -o "${1:-server/ladder/smz3check}" -lm -lpthread
