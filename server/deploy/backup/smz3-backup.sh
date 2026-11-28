#!/bin/bash
# Nightly: the app's data folder and a fresh Redis snapshot in /var/backups/smz3/smz3-YYYYMMDD.tar.zst, the last 14 kept.
# Installed as /usr/local/sbin/smz3-backup.sh, run by smz3-backup.timer; the files are root:smz3backup 0640.
set -euo pipefail
umask 027
DEST=/var/backups/smz3
APP=/srv/smz3/app
KEEP=14
OUT="$DEST/smz3-$(date -u +%Y%m%d).tar.zst"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"; rm -f "$OUT.part"' EXIT

install -d -m 0750 -o root -g smz3backup "$DEST"

# BGSAVE, then wait for LASTSAVE to move (also covers a save that was already running).
before="$(redis-cli LASTSAVE)"
redis-cli BGSAVE >/dev/null || true
for _ in $(seq 300); do
    [ "$(redis-cli LASTSAVE)" != "$before" ] && break
    sleep 1
done
[ "$(redis-cli LASTSAVE)" != "$before" ] || { echo "redis BGSAVE did not finish" >&2; exit 1; }
redis-cli INFO persistence | grep -q '^rdb_last_bgsave_status:ok' || { echo "redis BGSAVE failed" >&2; exit 1; }
rdir="$(redis-cli --raw CONFIG GET dir | sed -n 2p)"
rfile="$(redis-cli --raw CONFIG GET dbfilename | sed -n 2p)"
cp "${rdir:-/var/lib/redis}/${rfile:-dump.rdb}" "$WORK/dump.rdb"

# data/maprando is a cache of generated patches the server rebuilds on demand.
tar --zstd -cf "$OUT.part" --exclude=data/maprando -C "$APP" data -C "$WORK" dump.rdb
chgrp smz3backup "$OUT.part"
chmod 0640 "$OUT.part"
mv "$OUT.part" "$OUT"

ls -1 "$DEST"/smz3-*.tar.zst | sort -r | tail -n +$((KEEP + 1)) | xargs -r rm -f --
