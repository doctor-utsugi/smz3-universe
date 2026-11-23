#!/bin/sh
# Builds smz3daily (the daily challenge generator) next to server.js from the repository's C core.
set -e
cd "$(dirname "$0")/../.."
gcc -std=gnu11 -O2 -w -Icore -Icore/gen -Icore/ui $(ls core/gen/*.c | grep -v 'gen_rom\|test_') core/ui/smz3_runlogic.c \
    server/ladder/smz3daily.c core/smz3_locations.c core/smz3_state.c -o "${1:-server/ladder/smz3daily}" -lm -lpthread
