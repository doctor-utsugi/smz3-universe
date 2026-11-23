#!/usr/bin/env bash
# usage: pad.sh <button-code-decimal>   (BTN_GAMEPAD/A=304, B=305, START=315, SELECT=314, DPAD_UP=544 DOWN=545 LEFT=546 RIGHT=547)
adb shell "su -c 'sendevent /dev/input/event9 1 $1 1; sendevent /dev/input/event9 0 0 0; sleep 0.12; sendevent /dev/input/event9 1 $1 0; sendevent /dev/input/event9 0 0 0'"
