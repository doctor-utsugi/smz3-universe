#!/bin/bash
A="adb"; D=/dev/input/event9; cd "${TMPDIR:-/tmp}"
./hold.sh right 3; sleep 1
$A shell "sendevent $D 1 311 1; sendevent $D 0 0 0; sleep 0.3; sendevent $D 1 307 1; sendevent $D 0 0 0; sleep 0.2; sendevent $D 1 307 0; sendevent $D 0 0 0; sleep 0.6; sendevent $D 1 307 1; sendevent $D 0 0 0; sleep 0.2; sendevent $D 1 307 0; sendevent $D 0 0 0; sleep 0.3; sendevent $D 1 311 0; sendevent $D 0 0 0"; sleep 3
