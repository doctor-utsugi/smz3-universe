#!/bin/bash
A="adb"; D=/dev/input/event9; cd "${TMPDIR:-/tmp}"
./hold.sh up 0.3; sleep 0.5; ./hold.sh x 0.3; sleep 1; ./hold.sh right 6; ./hold.sh right 6
$A shell "sendevent $D 1 314 1; sendevent $D 0 0 0; sendevent $D 1 314 0; sendevent $D 0 0 0"; sleep 0.5
$A shell "sendevent $D 3 16 1; sendevent $D 0 0 0; sleep 0.2; sendevent $D 1 304 1; sendevent $D 0 0 0; sleep 0.9; sendevent $D 1 304 0; sendevent $D 0 0 0; sleep 0.7; sendevent $D 3 16 0; sendevent $D 0 0 0"
for i in 1 2 3 4 5 6; do ./hold.sh x 0.2; sleep 0.8; done; sleep 2; ./hold.sh right 5; sleep 3; ./hold.sh right 5; sleep 2
