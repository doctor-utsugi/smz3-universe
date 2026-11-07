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
rm -rf source/smz3 source/smz3_*.[ch] && mkdir -p romfs/smz3/ips
for f in smz3_generate smz3_fill smz3_world smz3_world_gen smz3_rng smz3_patch smz3_rom smz3_text smz3_sprite smz3_hints smz3_itemopts smz3_plando smz3_md5 smz3_romsetup smz3_romderived smz3_dungeonmaps smz3_goalicons; do
    cp "$ROOT/core/gen/$f.c" source/
done
cp "$ROOT"/core/gen/smz3_*.h source/
for f in smz3_state smz3_locations smz3_inventory; do cp "$ROOT/core/$f.c" source/; done
cp "$ROOT"/core/smz3_*.h source/
# the release tag on the top screen: the tag CI builds, else the newest tag here
VER="${GITHUB_REF_NAME:-}"; [[ "$VER" == v* ]] || VER="$(git -C "$ROOT" describe --tags --abbrev=0 2>/dev/null || echo dev)"
sed -i "s/#define SMZ3_VERSION \"dev\"/#define SMZ3_VERSION \"$VER\"/" source/smz3_version.h
for f in smz3_ui smz3_ui_opts smz3_run smz3_host smz3_preview smz3_tracker smz3_goal smz3_daily smz3_race smz3_proof smz3_runlogic smz3_discovery smz3_shortcuts smz3_counters smz3_rewind; do cp "$ROOT/core/ui/$f.c" source/; done
cp "$ROOT"/core/ui/smz3_*.h source/
cp "$ROOT"/vendor/Lemuroid/lemuroid-app/src/main/assets/smz3/ips/*.ips romfs/smz3/ips/
python3 "$ROOT"/tools/gen_3ds_assets.py "$ROOT"/vendor/Lemuroid/lemuroid-app/src/main/assets/smz3 source/smz3_mapdata.c romfs/smz3
python3 "$ROOT"/tools/gen_pc_logo.py "$ROOT"/assets/logo.png romfs/smz3/logo.pv 22
python3 "$ROOT"/tools/gen_flags.py --pv romfs/smz3/flags
# the app's icon on the HOME menu is the Thor's launcher icon, cut from the logo as tools/gen_app_icon.py does
python3 -c 'import sys; from PIL import Image; Image.open(sys.argv[1]).convert("RGB").crop((90, 90, 934, 934)).resize((48, 48), Image.LANCZOS).save(sys.argv[2])' \
    "$ROOT"/assets/logo.png resources/icon.png
docker run --rm -v "$PWD":/src -w /src devkitpro/devkitarm:latest make 3dsx -j"$(nproc)"
mkdir -p ../../build && cp output/src.3dsx ../../build/snes9x_3ds.3dsx
