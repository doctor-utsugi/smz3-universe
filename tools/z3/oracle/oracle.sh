#!/usr/bin/env bash
# Builds the PHP oracle image (if missing, or with --build) and runs z3oracle.php with the current directory as /work.
#   tools/z3/oracle/oracle.sh [--build] <verb> <args...>     (paths relative to the current directory)
set -euo pipefail
DIR=$(cd "$(dirname "$0")" && pwd)
IMAGE=smz3u-z3oracle:219fcafd
if [ "${1:-}" = "--build" ] || ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    [ "${1:-}" = "--build" ] && shift
    docker build -t "$IMAGE" "$DIR"
fi
[ $# -eq 0 ] && exit 0
exec docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/work" "$IMAGE" "$@"
