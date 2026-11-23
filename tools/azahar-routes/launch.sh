#!/bin/bash
# az.sh: launch Azahar, enter SMZ3 menu, continue slot 3 (index 2), pass title
A="adb"; cd "${TMPDIR:-/tmp}"
$A shell svc power stayon true; $A shell am force-stop org.azahar_emu.azahar
$A shell am start -a android.intent.action.VIEW -n org.azahar_emu.azahar/org.citra.citra_emu.activities.EmulationActivity -d file:///sdcard/Azahar/sdmc/snes9x_smz3.3dsx >/dev/null 2>&1; sleep 38
./pad.sh a; sleep 4; ./pad.sh down; ./pad.sh down; ./pad.sh a; sleep 1.5; ./pad.sh a; sleep 22
for i in 1 2 3 4 5 6; do ./pad.sh start; ./pad.sh a; sleep 1.5; done; sleep 5
$A exec-out screencap -p > az_top.png; $A exec-out screencap -p -d 4630946482288158084 > az_bot.png
