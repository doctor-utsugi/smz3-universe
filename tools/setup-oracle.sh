#!/bin/bash
# Prepares the C# oracle (vendor/SMZ3Randomizer) for the differential tests: applies
# patches/smz3randomizer-trace.patch, which adds PatcherService.Trace (the list of patch updates
# the C# patcher produces) and is what test-patch.py / test-plando.py compare the C port against.
# Safe to run again. Then rebuild the CLI: cd tools/gen-cli && dotnet build -c Release
set -e
root="$(cd "$(dirname "$0")/.." && pwd)"
v="$root/vendor/SMZ3Randomizer"
p="$root/patches/smz3randomizer-trace.patch"
if git -C "$v" apply --reverse --check "$p" 2>/dev/null; then
    echo "trace patch already applied"
elif git -C "$v" apply --check "$p"; then
    git -C "$v" apply "$p"
    echo "trace patch applied; rebuild tools/gen-cli"
else
    echo "trace patch does not apply to vendor/SMZ3Randomizer" >&2
    exit 1
fi
