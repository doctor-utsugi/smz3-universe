#!/bin/bash
A="adb"; D=/dev/input/event9; cd "${TMPDIR:-/tmp}"
$A shell "sendevent $D 3 16 1; sendevent $D 0 0 0; sleep 0.15; sendevent $D 1 304 1; sendevent $D 0 0 0; sleep 0.3; sendevent $D 1 304 0; sendevent $D 0 0 0; sleep 1; sendevent $D 3 16 0; sendevent $D 0 0 0"; sleep 3
