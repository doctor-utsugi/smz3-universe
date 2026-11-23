#!/bin/sh
# Differential test of the C logic port against the C# tracker logic.
set -e
cd "$(dirname "$0")/.."
python3 tools/port_logic.py
dotnet tools/gen-cli/bin/Release/net10.0/gen-cli.dll vectors "${1:-1000}" "${2:-1}" build/logic_vectors.txt
gcc -O1 -Wall -Wno-parentheses -Icore/gen core/gen/test_logic.c -o build/test_logic
build/test_logic
