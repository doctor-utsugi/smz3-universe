#!/usr/bin/env bash
# Pins Map Rando (blkerby/MapRandomizer, MIT; sm-json-data CC BY 4.0) in vendor/maprando with patches/maprando-device.patch,
# and packs what a host loads into build/maprando-data, with the same fixed subset of each map pool on every host.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SRC=$ROOT/vendor/maprando
OUT=${1:-$ROOT/build/maprando-data}
COMMIT=8adf9c991078c5890f084b7c1502f17b8ddf83d7
POOLS=${POOLS:-"v119-standard v119-small"}
POOL_FILES=${POOL_FILES:-20}

if [ ! -d "$SRC/.git" ]; then
    git clone --filter=blob:none https://github.com/blkerby/MapRandomizer "$SRC"
fi
if [ "$(git -C "$SRC" rev-parse HEAD)" != "$COMMIT" ]; then
    git -C "$SRC" fetch --depth 1 origin "$COMMIT"
    git -C "$SRC" checkout --force "$COMMIT"
fi
git -C "$SRC" submodule update --init --depth 1 rust/lznint sm-json-data
if git -C "$SRC" apply --check "$ROOT/patches/maprando-device.patch" 2>/dev/null; then
    git -C "$SRC" apply "$ROOT/patches/maprando-device.patch"
fi

if [ ! -d "$SRC/patches/mosaic" ]; then
    id=$(cat "$SRC/MOSAIC_BUILD_ID")
    tmp=$(mktemp -d)
    curl -fsSL -o "$tmp/m.tar.zstd" "https://map-rando-artifacts.s3.us-west-004.backblazeb2.com/Mosaic/Mosaic-$id.tar.zstd"
    zstd -qd "$tmp/m.tar.zstd" -o "$tmp/m.tar"
    mkdir -p "$SRC/patches/mosaic"
    tar xf "$tmp/m.tar" -C "$SRC/patches/mosaic"
    rm -rf "$tmp"
fi

rm -rf "$OUT"
mkdir -p "$OUT/rust" "$OUT/sm-json-data" "$OUT/patches" "$OUT/TitleScreen" "$OUT/gfx" "$OUT/maps"
cp -r "$SRC/rust/data" "$SRC/rust/VERSION" "$OUT/rust/"
(cd "$SRC/sm-json-data" && cp -r connection enemies weapons tech.json helpers.json items.json numerics.json LICENSE.md \
    "$OUT/sm-json-data/" && find region -name '*.json' -exec cp --parents {} "$OUT/sm-json-data/" \;)
cp "$SRC/room_geometry.json" "$SRC/LICENSE" "$OUT/"
cp -r "$SRC/patches/ips" "$SRC/patches/rom_map" "$OUT/patches/"
mkdir -p "$OUT/patches/mosaic"   # the vanilla tile theme only, the one the hosts offer
cp "$SRC"/patches/mosaic/Base-* "$SRC/patches/mosaic/tilesets.bps" "$OUT/patches/mosaic/"
cp -r "$SRC/TitleScreen/Images" "$OUT/TitleScreen/"
cp -r "$SRC/gfx/title" "$OUT/gfx/"

for pool in $POOLS; do
    base=$(python3 -c "import json,sys; print(next(p['base_url'] for p in json.load(open(sys.argv[1]))['pools'] if p['name']==sys.argv[2]))" \
        "$SRC/rust/data/map_pools.json" "$pool")
    dir=$OUT/maps/$pool
    mkdir -p "$dir"
    files=""
    for i in $(seq 0 $((POOL_FILES - 1))); do
        f=maps-$i.avro
        cache=$SRC/maps/$pool/$f
        if [ ! -f "$cache" ]; then mkdir -p "$SRC/maps/$pool"; curl -fsSL -o "$cache" "$base$f"; fi
        cp "$cache" "$dir/$f"
        files="$files${files:+,}\"$f\""
    done
    echo "{\"maps_per_file\":100,\"files\":[$files]}" > "$dir/manifest.json"
done
du -sh "$OUT"
