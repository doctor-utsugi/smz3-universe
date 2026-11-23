#!/usr/bin/env bash
# Builds the PC frontend (build/pc/smz3): the shared C core, snes9x (libretro core, statically linked) and SDL2.
#   ./build-pc.sh            Linux/macOS native build -> build/pc/smz3
#   ./build-pc.sh windows    cross-build with MinGW-w64 -> build/win/smz3.exe (downloads SDL2's MinGW development files once)
# Needs a C and C++ compiler, make, python3 with Pillow only for tools/gen_pc_font.py (the generated font is committed) and the SDL2 development files.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
S9X="$ROOT/vendor/snes9x_libretro"
S9X_COMMIT=fae2fea08f74180759ef540ee94259213f503480
if [ ! -d "$S9X/.git" ]; then
    mkdir -p "$ROOT/vendor"
    git clone https://github.com/libretro/snes9x.git "$S9X"
fi
git -C "$S9X" checkout -q "$S9X_COMMIT"
[ -d "$ROOT/vendor/Lemuroid/lemuroid-app/src/main/assets/smz3/ips" ] || { echo "the IPS patches are expected in vendor/Lemuroid (see build-android.sh)" >&2; exit 1; }
if [ "${1:-}" = windows ]; then
    W="$ROOT/build/win"
    mkdir -p "$W"
    if [ ! -d "$W/SDL2/x86_64-w64-mingw32" ]; then
        T="$(mktemp -d)"
        curl -sL -o "$T/sdl.tgz" https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-devel-2.30.9-mingw.tar.gz
        tar xzf "$T/sdl.tgz" -C "$T"
        mkdir -p "$W/SDL2" && cp -r "$T"/SDL2-2.30.9/* "$W/SDL2/"
        rm -rf "$T"
    fi
    if [ ! -d "$W/s9x" ]; then
        cp -r "$S9X" "$W/s9x"
        find "$W/s9x" -name '*.o' -delete
    fi
    make -C "$ROOT/frontends/pc" WIN=1 -j"$(getconf _NPROCESSORS_ONLN)"
    echo "built $W/smz3.exe"
    exit 0
fi
case "$(uname -s)" in Darwin) export S9X_PLATFORM=osx;; esac
make -C "$ROOT/frontends/pc" -j"$(getconf _NPROCESSORS_ONLN)"
echo "built $ROOT/build/pc/smz3"
