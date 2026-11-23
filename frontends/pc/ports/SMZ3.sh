#!/bin/bash
# SMZ3 in the Ports menu of handheld Linux frontends (ROCKNIX, Knulli, ...). Lives in roms/ports next to the SMZ3 folder,
# which holds the AppImage (named SMZ3.aarch64 so the frontend doesn't list it) and the two base ROMs, loose or zipped.
DIR="$(cd "$(dirname "$0")" && pwd)/SMZ3"
APP="$DIR/SMZ3.aarch64"
cd "$DIR" || exit 1
chmod +x "$APP"

export APPIMAGE_EXTRACT_AND_RUN=1   # these systems have no FUSE
# the SDL inside the AppImage crashes on ROCKNIX; the system's one also knows the handheld's pad
for lib in /usr/lib/libSDL2-2.0.so.0 /usr/lib64/libSDL2-2.0.so.0; do
    [ -e "$lib" ] && { export LD_PRELOAD="$lib"; break; }
done

# on dual-screen handhelds sway keeps the second screen off unless a game asks for it
OFF=$(swaymsg -r -t get_outputs 2>/dev/null | awk -F'"' '/"name"/ { n = $4 } /"power": false/ { print n }')
for out in $OFF; do swaymsg output "$out" power on > /dev/null; done

{
    # the first time, the ROMs in this folder become the base ROMs
    "$APP" config | grep -q '^check both' || "$APP" setup "$DIR"
    "$APP"
} > "$DIR/log.txt" 2>&1

for out in $OFF; do swaymsg output "$out" power off > /dev/null; done
