#!/usr/bin/env bash
# Builds the vendored snes9x_3ds (with the SMZ3 run menu and offline generator from core/) in the devkitARM image.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT/vendor/snes9x_3ds"
git submodule update --init --depth 1
if [ -f "$ROOT/patches/snes9x_3ds-smz3.patch" ] && ! grep -q smz3run Makefile; then
    git apply "$ROOT/patches/snes9x_3ds-smz3.patch"
fi
# the shared C core is copied next to the emulator sources (tests and CLIs stay out)
rm -rf source/smz3 source/smz3_*.[ch] source/sm_*.[ch] source/z3_*.[ch] && mkdir -p romfs/smz3/ips
for f in smz3_generate smz3_fill smz3_world smz3_world_gen smz3_rng smz3_patch smz3_rom smz3_text smz3_sprite smz3_hints smz3_itemopts smz3_plando smz3_md5 smz3_romsetup smz3_romderived smz3_dungeonmaps smz3_goalicons; do
    cp "$ROOT/core/gen/$f.c" source/
done
cp "$ROOT"/core/gen/smz3_*.h source/
for f in smz3_state smz3_locations smz3_inventory; do cp "$ROOT/core/$f.c" source/; done
# the save state converter between this emulator and the libretro core (core/state)
cp "$ROOT"/core/state/smz3_statecvt.[ch] source/
# the Super Metroid generator (core/smgen), flat beside the rest: its one relative include is made plain
cp "$ROOT"/core/smgen/sm_*.[ch] source/
# the BPS reader for the Map Rando seeds the server sends (core/common)
cp "$ROOT"/core/common/smz3_bps.[ch] source/
# the MP3 decoder and the menu music (smz3music.cpp)
cp "$ROOT"/core/common/dr_mp3.h source/
cp "$ROOT"/assets/music/smz3_intro.mp3 romfs/smz3/
sed -i 's#"../gen/smz3_rng.h"#"smz3_rng.h"#' source/sm_fill.h
python3 "$ROOT"/tools/gen_sm_ips_c.py "$ROOT"/patches/sm_randomizer.ips source/sm_ips_data.c
# and the A Link to the Past one (core/z3gen), its includes already plain
cp "$ROOT"/core/z3gen/z3_*.[ch] source/
python3 "$ROOT"/tools/z3/gen_z3_ips_c.py "$ROOT"/patches/z3_base.ips source/z3_ips_data.c
cp "$ROOT"/core/smz3_*.h source/
# the release tag on the top screen: the tag CI builds, else the newest tag here
VER="${GITHUB_REF_NAME:-}"; [[ "$VER" == v* ]] || VER="$(git -C "$ROOT" describe --tags --abbrev=0 2>/dev/null || echo dev)"
sed -i "s/#define SMZ3_VERSION \"dev\"/#define SMZ3_VERSION \"$VER\"/" source/smz3_version.h
for f in smz3_ui smz3_ui_opts smz3_run smz3_host smz3_preview smz3_tracker smz3_goal smz3_daily smz3_race smz3_proof smz3_runlogic smz3_discovery smz3_shortcuts smz3_counters smz3_rewind smz3_sm smz3_z3 smz3_smmap smz3_json smz3_romm smz3_sync; do cp "$ROOT/core/ui/$f.c" source/; done
cp "$ROOT"/core/ui/smz3_*.h source/
cp "$ROOT"/vendor/Lemuroid/lemuroid-app/src/main/assets/smz3/ips/*.ips romfs/smz3/ips/
python3 "$ROOT"/tools/gen_3ds_assets.py "$ROOT"/vendor/Lemuroid/lemuroid-app/src/main/assets/smz3 source/smz3_mapdata.c romfs/smz3
python3 "$ROOT"/tools/gen_pc_logo.py "$ROOT"/assets/logo.png romfs/smz3/logo.pv 22
python3 "$ROOT"/tools/gen_flags.py --pv romfs/smz3/flags
# the certificate authorities the self-update checks GitHub against (the console has no store of its own)
cp /etc/ssl/certs/ca-certificates.crt romfs/smz3/cacert.pem
# the community sprites ship in the app; the SD card's 3ds/SMZ3/sprites can still add or replace one
rm -rf romfs/smz3/sprites && cp -r "$ROOT"/assets/sprites romfs/smz3/sprites
# the app's icon on the HOME menu is the Thor's launcher icon, cut from the logo as tools/gen_app_icon.py does
python3 -c 'import sys; from PIL import Image; Image.open(sys.argv[1]).convert("RGB").crop((90, 90, 934, 934)).resize((48, 48), Image.LANCZOS).save(sys.argv[2])' \
    "$ROOT"/assets/logo.png resources/icon.png
# the CIA's HOME menu banner (tools/gen_3ds_banner.py) and its title version, taken from the release tag
cp "$ROOT"/assets/3ds/banner.bnr resources/banner.bnr
IFS=. read -r MAJ MIN MIC <<< "$(echo "${VER#v}" | grep -E '^[0-9]+\.[0-9]+\.[0-9]+$' || echo 0.0.0)"
# a local build is ahead of the last tag: the HOME menu keeps a title's name and icon until its version changes
[[ "${GITHUB_REF_NAME:-}" == v* ]] || MIC=$((MIC < 15 ? MIC + 1 : 15))
sed -i -E "s/^(APP_VERSION_MAJOR = ).*/\1$MAJ/; s/^(APP_VERSION_MINOR = ).*/\1$MIN/; s/^(APP_VERSION_MICRO = ).*/\1$MIC/" resources/AppInfo
docker run --rm -v "$PWD":/src -w /src devkitpro/devkitarm:latest \
    sh -c "make 3dsx -j$(nproc) && make cia -j$(nproc)"   # one after the other: each target is its own sub-make over the same objects
mkdir -p ../../build && cp output/src.3dsx ../../build/snes9x_3ds.3dsx && cp output/src.cia ../../build/snes9x_3ds.cia
