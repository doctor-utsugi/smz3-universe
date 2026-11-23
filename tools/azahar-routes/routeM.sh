#!/bin/bash
A="adb"; cd "${TMPDIR:-/tmp}"
./hold.sh left 7; ./hold.sh x 0.3; ./hold.sh left 4; sleep 1; ./hold.sh left 5; ./hold.sh x 0.2; ./hold.sh left 1; sleep 2; ./hold.sh left 5
./hold.sh down 0.4; sleep 0.3; ./hold.sh down 0.4; sleep 0.5; ./hold.sh left 0.5; ./hold.sh x 0.2; sleep 1.5; ./hold.sh left 2; ./hold.sh x 0.2; sleep 1.5; ./hold.sh left 3; ./hold.sh x 0.2; sleep 0.4; ./hold.sh x 0.2; sleep 2; ./hold.sh left 4; ./hold.sh x 0.2; sleep 1.5; ./hold.sh left 3; ./hold.sh left 1; sleep 5; ./hold.sh right 1.5; sleep 6; ./hold.sh left 1; sleep 5; ./hold.sh right 2; sleep 6; ./hold.sh right 2; sleep 3; ./hold.sh left 1.5; sleep 4; ./hold.sh right 2; sleep 3; ./hold.sh right 2; sleep 3
