#!/usr/bin/env bash
# Builds the browser version into build/web inside the Emscripten image (frontends/web/Dockerfile).
# Needs what ./build-pc.sh needs (vendor/snes9x_libretro, vendor/Lemuroid) and, for Map Rando, tools/maprando/fetch.sh run once.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
IMAGE=smz3-web-build
docker build -q -t "$IMAGE" "$ROOT/frontends/web" >/dev/null
mkdir -p "$ROOT/build/web-cargo"
docker run --rm -u "$(id -u):$(id -g)" -v "$ROOT":/src -w /src -e HOME=/tmp -e CARGO_HOME=/src/build/web-cargo \
    -e SMZ3_VERSION="$(git -C "$ROOT" describe --tags --abbrev=0 2>/dev/null || echo dev)" \
    "$IMAGE" make -C frontends/pc WEB=1 web -j"$(nproc)"
echo "built $ROOT/build/web"
