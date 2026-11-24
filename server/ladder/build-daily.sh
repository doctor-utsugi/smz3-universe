#!/bin/sh
# Builds smz3daily (the daily challenge generator) next to server.js from the repository's C core: the SMZ3 generator and,
# for the Super Metroid windows (--sm), the standalone one's logic and filler (core/smgen).
set -e
cd "$(dirname "$0")/../.."
gcc -std=gnu11 -O2 -w -Icore -Icore/gen -Icore/smgen -Icore/ui $(ls core/gen/*.c | grep -v 'gen_rom\|test_') core/ui/smz3_runlogic.c \
    core/smgen/sm_fill.c core/smgen/sm_world.c core/smgen/sm_world_gen.c \
    server/ladder/smz3daily.c server/ladder/smz3daily_sm.c core/smz3_locations.c core/smz3_state.c -o "${1:-server/ladder/smz3daily}" -lm -lpthread
