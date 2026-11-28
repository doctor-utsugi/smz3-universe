#!/usr/bin/env bash
# Puts the smz3.com landing page (this folder) on the server, where Caddy serves it from /srv/smz3-site (server/deploy/Caddyfile).
#   SMZ3_SITE_HOST=user@server frontends/site/deploy.sh
# The files go to a staging folder over there first and are installed only once all of them arrived, so a broken copy
# leaves the live page as it was. Caddy compresses on the fly (encode), so nothing is precompressed here.
set -euo pipefail
SRC="$(cd "$(dirname "$0")" && pwd)"
HOST="${SMZ3_SITE_HOST:?set SMZ3_SITE_HOST to the server (user@host)}"
SITE=/srv/smz3-site
FILES=(index.html site.css site.js logo.png favicon.png)
for f in "${FILES[@]}"; do [ -f "$SRC/$f" ] || { echo "missing $SRC/$f" >&2; exit 1; }; done
STAGE="$(mktemp -d)"
trap 'rm -r "$STAGE"' EXIT
for f in "${FILES[@]}"; do cp "$SRC/$f" "$STAGE/"; done
rsync -a --delete "$STAGE"/ "$HOST:smz3-site.new/"
ssh "$HOST" "sudo install -d -m 0755 -o root -g root $SITE && sudo install -m 0644 -o root -g root ~/smz3-site.new/* $SITE/ && rm -r ~/smz3-site.new"
echo "deployed to $HOST:$SITE"
