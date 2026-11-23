#!/bin/bash
# hold.sh <left|right|up|down|a|b|x|y> <seconds> [other buttons simultaneously via 2nd call]
A="adb"; D=/dev/input/event9; s=$2
case $1 in
 left) d="3 16 -1"; u="3 16 0";; right) d="3 16 1"; u="3 16 0";; up) d="3 17 -1"; u="3 17 0";; down) d="3 17 1"; u="3 17 0";;
 a) d="1 304 1"; u="1 304 0";; b) d="1 305 1"; u="1 305 0";; x) d="1 307 1"; u="1 307 0";; y) d="1 308 1"; u="1 308 0";;
esac
$A shell "sendevent $D $d; sendevent $D 0 0 0; sleep $s; sendevent $D $u; sendevent $D 0 0 0"
