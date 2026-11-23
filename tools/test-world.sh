#!/bin/sh
# Differential test of the C world (regions, locations, rewards, bosses) against the C# tracker logic.
set -e
cd "$(dirname "$0")/.."
python3 tools/port_world.py
dotnet tools/gen-cli/bin/Release/net10.0/gen-cli.dll worldvectors "${1:-500}" "${2:-1}" build/world_vectors.txt
gcc -O1 -Wall -Wno-parentheses -Wno-unused-function -Icore/gen core/gen/test_world.c core/gen/smz3_world.c core/gen/smz3_world_gen.c -o build/test_world
build/test_world
