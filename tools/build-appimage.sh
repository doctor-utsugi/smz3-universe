#!/usr/bin/env bash
# Packs build/pc/smz3 (./build-pc.sh first) as an AppImage with its own SDL2: build/SMZ3-x86_64.AppImage.
# Needs python3 with Pillow (icon) and curl. linuxdeploy is downloaded once into build/appimage/.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
W="$ROOT/build/appimage"
mkdir -p "$W/AppDir/usr/bin" "$W/AppDir/usr/share/applications" "$W/AppDir/usr/share/icons/hicolor/256x256/apps"
LD="$W/linuxdeploy-x86_64.AppImage"
if [ ! -x "$LD" ]; then
    curl -sL -o "$LD" https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20250213-2/linuxdeploy-x86_64.AppImage
    chmod +x "$LD"
fi
cp "$ROOT/build/pc/smz3" "$W/AppDir/usr/bin/smz3"
mkdir -p "$W/AppDir/usr/share/smz3"
rm -rf "$W/AppDir/usr/share/smz3/sprites"
cp -r "$ROOT/assets/sprites" "$W/AppDir/usr/share/smz3/sprites"
python3 - "$ROOT" "$W/AppDir/usr/share/icons/hicolor/256x256/apps/smz3.png" <<'PY'
import sys
from PIL import Image
logo = Image.open(sys.argv[1] + "/assets/logo.png").convert("RGB").crop((90, 90, 934, 934)).resize((256, 256), Image.LANCZOS)
logo.save(sys.argv[2])
PY
cat > "$W/AppDir/usr/share/applications/smz3.desktop" <<'DESK'
[Desktop Entry]
Type=Application
Name=SMZ3
Comment=Super Metroid and A Link to the Past randomizer with a built-in tracker
Exec=smz3
Icon=smz3
Categories=Game;
Terminal=false
DESK
cd "$W"
APPIMAGE_EXTRACT_AND_RUN=1 ARCH=x86_64 OUTPUT="SMZ3-x86_64.AppImage" "$LD" --appdir AppDir \
    --desktop-file AppDir/usr/share/applications/smz3.desktop --icon-file AppDir/usr/share/icons/hicolor/256x256/apps/smz3.png --output appimage
mv "$W/SMZ3-x86_64.AppImage" "$ROOT/build/SMZ3-x86_64.AppImage"
echo "built $ROOT/build/SMZ3-x86_64.AppImage"
