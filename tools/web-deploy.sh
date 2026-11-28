#!/usr/bin/env bash
# Puts build/web (frontends/web/build.sh) on the game's server, where Caddy serves it at /play/ (frontends/web/README.md).
#   SMZ3_WEB_HOST=user@server tools/web-deploy.sh
# The files are compressed here first (Caddy's precompressed .br/.gz), then swapped in at once, so no player gets a new
# smz3.js beside an old smz3.wasm. The previous build stays in smz3-web.old until the next deploy.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HOST="${SMZ3_WEB_HOST:?set SMZ3_WEB_HOST to the server (user@host)}"
WEB=/srv/smz3-web
SRC="$ROOT/build/web"
for f in index.html smz3.js smz3.wasm sprites.data maprando.data; do [ -f "$SRC/$f" ] || { echo "missing $SRC/$f: run frontends/web/build.sh" >&2; exit 1; }; done
STAGE="$(mktemp -d)"
trap 'rm -r "$STAGE"' EXIT
cp "$SRC"/index.html "$SRC"/*.js "$SRC"/*.wasm "$SRC"/*.data "$STAGE"/
for f in "$STAGE"/*.js "$STAGE"/*.wasm "$STAGE"/*.data; do
    gzip -9 -k "$f"
    if command -v brotli >/dev/null; then brotli -q 11 -k "$f"; fi
done
rsync -a --delete "$STAGE"/ "$HOST:smz3-web.new/"
ssh "$HOST" "sudo rm -rf $WEB.old && { [ ! -d $WEB ] || sudo mv $WEB $WEB.old; } && sudo mv ~/smz3-web.new $WEB && sudo chown -R root:root $WEB && sudo chmod -R a+rX $WEB"
echo "deployed to $HOST:$WEB"
