# TODO

Open items, kept as they come up.

## Unverified (built, not seen on hardware)
- PS pad: RT/LT rebinding and PS button icons (code review only).
- Quick states with the pad combos (Select + RB/LB/dpad); the keyboard keys were checked on PC.
- macOS tarball with the `sprites` folder (CI workflow edited, never run; the Windows zip was rebuilt locally and checked under Wine).
- The new end of the run (Triforce room / ship) in a real game, and the run-complete popup on Android: needs a run played to the end.
- Everything on a real Thor and a real 3DS (checked on the Android emulator and Azahar only).

## To fix
- Android, new run start screen: the Link and Samus sprite pictures are blank (seen on the Pixel 8 emulator with the release APK). Make them show and check it on the emulator.

## Not reproduced
- Tiny logo under the Z seen in a GPU screenshot (not seen with the software, OpenGL or OpenGL ES renderers).
- "Points already collected in a new run": ask for a screenshot if it shows up.

## Won't do
- Widescreen: bsnes-hd outputs 16:9, but outside Mode 7 the extra columns show garbage (both games only draw 256 pixels), and there is no such core for the 3DS.
