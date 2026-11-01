#!/bin/bash
# Starts the SMZ3 generator service on port 8765 (detached). The Thor reaches it through `adb reverse tcp:8765 tcp:8765`.
cd "$(dirname "$0")/.."
dotnet build tools/gen-cli -c Release 2>&1 | grep -E " error |Build succeeded"
setsid dotnet tools/gen-cli/bin/Release/net10.0/gen-cli.dll serve rom/supermetroid_ju.sfc rom/alttp_usa.sfc "${1:-8765}" > /tmp/gen-serve.log 2>&1 &
