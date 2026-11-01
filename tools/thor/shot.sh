#!/usr/bin/env bash
# usage: shot.sh <name> ; saves primary (top) and secondary (bottom) Thor screenshots
S=${SHOT_DIR:-/tmp}
adb exec-out screencap -p -d 4630946441858561667 > $S/$1_top.png
adb exec-out screencap -p -d 4630946482288158084 > $S/$1_bottom.png
