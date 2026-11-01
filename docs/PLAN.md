# Plan

## Decisions (from the kickoff interview)

- Targets: 3DS, New 3DS, AYN Thor (Android, dual screen).
- Architecture: shared C core, two frontends (`frontends/3ds` with devkitPro, `frontends/android` with the NDK).
- Run creation: the SMZ3 generator is ported to C and runs on the device, offline (reference: Vivelin/SMZ3Randomizer, MIT, C#).
- SNES emulator: reuse an existing one (candidate to be chosen after research; needs WRAM reads and a second render surface).
- Bottom screen: current map, inventory and checks, run data, logic reachability.
- Start menu: "Create run" (options panel, like the web) and 4 slots. A slot offers Continue / Delete and shows a run summary. Visual reference: Secret of Mana's file select.
- Each slot has its own SRAM/savestates, a name and a last-played date.

## Phases

Priority change (2026-10-19): the AYN Thor comes first, the 3DS after. The phases below are reordered accordingly in "Thor-first plan".

1. Toolchain: install devkitPro, build an existing SNES emulator for 3DS, boot `smz3_test_popoie_fang.sfc`.
2. Autotracking: read WRAM (items, location flags, room/map position) and verify against the test ROM.
3. Bottom screen: run data and inventory first, then maps, then logic.
4. Slots and start menu.
5. Generator port and options panel.
6. Android/Thor frontend on the same core.

## Open questions

- Which emulator core (snes9x 3DS port vs libretro snes9x/bsnes) and its license.
- Whether Old 3DS is in scope (user said 3DS and New 3DS; SMZ3 is 6 MB).
- Where the logic/location data lives in the C# source, and whether the Cas' fork or the original SMZ3 generator is the base.

## Phase 1 status

- Emulator base: matbo87/snes9x_3ds (Snes9x license, non-commercial), cloned to `vendor/` (gitignored) with its rcheevos submodule.
- `./build-3ds.sh` builds it in the `devkitpro/devkitarm` Docker image; output in `build/snes9x_3ds.3dsx`. Builds clean.
- Not yet booted: no 3DS emulator was installed. Known upstream: Citra nightly <= 2104 works, Azahar renders Mode7 as a solid yellow texture. (Superseded: Azahar 2126.2 on the Thor boots it, with a garbled picture, see below; a real 3DS is untested.)

## Citra nightly 2104 (2026-11-01)

The Linux AppImage of Citra nightly 2104 (`citra-linux-appimage-20240303-0ff3440`, kept on archive.org since the official
repositories went down) draws snes9x_3ds correctly: the menu on the top screen at 400x240, the bottom panel, offline generation,
and Super Metroid running at 59 FPS with the tracker (`docs/shots/3ds-citra2104-menu.png`, `docs/shots/3ds-citra2104-game.png`).
The garbled picture is Azahar's alone. Real hardware is still untested.

## Phase 1 test results (2026-10-19, Azahar 2126.2 flatpak)

- The menu renders and runs at 60 FPS, but every game (SMZ3, plain Super Metroid, Super Mario World) shows the same garbled, streaked output. It is identical under OpenGL, Vulkan and the software renderer, so it is not the ROM and not the GPU backend.
- The software renderer also stalls the game at 0 FPS.
- Upstream says Citra nightly <= 2104 works. Next: try an older Citra/Lime3DS build, or test on real hardware. (Superseded: Azahar on the Thor is used as it is; only the picture is wrong, menus, generation, panel and input work. Real 3DS hardware is untested.)

## Emulator investigation (2026-10-19)

- The official upstream v1.61 `.3dsx` is garbled in Azahar 2126.2 (OpenGL) too, so the local Docker build is fine.
- Azahar 2127.0-alpha3 (AppImage) is also garbled with the official build.
- Likely cause: snes9x_3ds renders into 1024x1024 textures (tile cache and Mode7) by shrinking the texture dimensions to force a 512 stride and moving the framebuffer address (`source/3dsgpu.cpp`, `source/3dsimpl_gpu.cpp`). Azahar emulates this wrongly; real hardware is fine per upstream.
- Azahar 2120.3 is unpacked in `build/emu/2120.3/` (portable `user/` dir) but untested: key injection did not register in that build. (Superseded, not used.)
- Citra 2104 has no prebuilt binaries available; only a source mirror (Nailington/Citra-Nightly).
- Desktop automation (ydotool + KWin focus script) is unreliable and must not be used without a verified focus: a mouse tap once landed on the Claude app window.

## Thor-first plan

Base: a fork of Lemuroid (Swordfish90/Lemuroid, GPL-3.0, Kotlin) on top of LibretroDroid. It already ships a libretro snes9x core, save states, input, audio and video, and LibretroDroid's `core.cpp` already calls `retro_get_memory_data`, which is what autotracking needs. Cloned in `vendor/Lemuroid`; `./build-android.sh` builds `build/lemuroid-debug.apk` (builds clean with JDK 21).

The Thor exposes its bottom screen as a secondary Android display, so the tracker UI goes in an Android `Presentation` on that display. With no Thor at hand it can be tested with a simulated secondary display (`settings put global overlay_display_devices`).

1. Run the stock APK on a device and check snes9x plays `smz3_test_popoie_fang.sfc`.
2. Expose WRAM from LibretroDroid to Kotlin and read items/locations from the test ROM.
3. Second-screen `Presentation` with run data and inventory; then maps and logic.
4. Start menu with 4 run slots (Continue / Delete, summary) and the "Create run" options panel.
5. Port the generator to C (NDK) and run it on the device.
6. 3DS frontend on the same C core.

Connected now over adb: an XPPen tablet (MDP1221, Android 14), not the Thor.

## Thor-first, phase 2 result (2026-10-19)

- LibretroDroid got `readMemory(id, offset, length)` (C++ `LibretroDroid::readMemory`, JNI, `LibretroDroid.java`, `GLRetroView.readMemory` plus `MEMORY_*` constants). Lemuroid builds against the local copy through a composite build. Both changes are saved as `patches/*.patch` because `vendor/` is gitignored.
- Verified on an Android 16 x86_64 emulator (headless, `-wipe-data -partition-size 16384`; a plain Pixel_8 AVD runs out of space): Lemuroid scans the ROM folder, recognises `smz3_test_popoie_fang.sfc` as SNES and boots it (Nintendo logo). The `Smz3Probe` log reads 131072 bytes of WRAM changing every second.
- SRAM from snes9x reports only 32768 bytes (checked: `sram=32768`). SMZ3 keeps data beyond that; next step is to find where the item and location flags live (WRAM vs SRAM) and whether snes9x exposes them.
- Build notes: JDK 21 for Gradle, NDK 27 needs `#include <functional>` in two LibretroDroid headers (patched), LibretroDroid needs AGP 8.7.1 to match Lemuroid, and both repos need their submodules (`build-android.sh` does it). For a small emulator install, strip other ABIs and re-sign (the full debug APK is 226 MB).

## Phase 2b: SMZ3 state decoding (2026-10-19)

Source of truth: the Cas' tracker (vendor/SMZ3Randomizer, MIT). Its autotracking reads, in snes9x addressing:

- WRAM `0x7E0020` != 0: a game has started. CartRAM `0xA173FE`: `0x00` Zelda, `0xFF` Super Metroid, `0x11` credits.
- Zelda checks: 16-bit word at `0x7EF000 + addr*2`; for chest checks the table's flag is the BIT NUMBER (4..11, so mask `1 << flag`; fixed 2026-10-20 after the first real Zelda pickup, it was applied as a mask before), for misc checks it is a mask. Misc checks (`ZeldaMisc`): byte at `0x7EF280 + addr`. When Metroid is the current game, the same Zelda layout is read from CartRAM `0xA06000` (SRAM offset 0).
- Metroid checks: byte at `0x7ED870 + addr`, masked. When Zelda is current, it is read from CartRAM `0xA160C0` (SRAM offset `0x20C0`).
- CartRAM offset from a snes9x address: `(bank - 0xA0) * 0x2000 + (addr & 0xFFFF) - 0x6000`.

Built:

- `tools/extract_locations.py` parses the C# region files into `data/locations.json`: 316 locations (216 Zelda, 100 Metroid), all with memory address and flag. `tools/gen_locations_c.py` turns it into `core/smz3_locations.c`.
- `core/smz3_state.{h,c}`: game detection, started flag, per-location cleared check and a summary; `core/test_smz3.c` prints a summary from two dump files (build: `gcc -std=c99 -o test_smz3 core/test_smz3.c core/smz3_state.c core/smz3_locations.c`).

Verified live (emulator, real snes9x memory through `readMemory`): before a file starts the summary reads `started=0`, and a fresh Super Metroid start reads `game=metroid started=1 zelda=0/216 metroid=0/100`. Positive detections (a collected item) and the Zelda side were verified later on the Thor, see "Check detection in play" and "Zelda pickup, persistence" below; the cross-game SRAM mirror is covered by `core/test_state_zelda.c` and by the 3DS panel. The 32 KB SRAM that snes9x exposes covers the offsets used above (`0x33FE` and `0x20C0`).

Samples (memory dumps of a running game, kept locally and not published): `data/sample_wram_sm_start.bin`, `data/sample_sram_sm_start.bin`.

## Phase 3: second screen (2026-10-19)

- `frontends/android/` builds `libsmz3core.so` (CMake, JNI in `jni/smz3_jni.c`, linked with `-Wl,-z,max-page-size=16384` so it loads on 16 KB-page devices) from the shared C core. Lemuroid's `lemuroid-app` module builds it through `externalNativeBuild`.
- `Smz3Tracker` (patch `patches/lemuroid-tracker.patch`) reads WRAM/SRAM through `GLRetroView.readMemory` every 500 ms, calls the C core and draws the run on the secondary display with an Android `Presentation`: current game, run time, Zelda and Metroid check counts with bars, and the last 8 checks. It shows when a presentation display exists or is added later (`DisplayListener`).
- (Superseded by the Thor test below, where the panel was seen working.) First emulator run: the first run failed with `UnsatisfiedLinkError` (16 KB page size, now fixed), and a second run was cut short because the shared machine was overloaded (load average 60+ from other jobs) and the emulator stopped answering. The overlay-display approach (`settings put global overlay_display_devices`) did create a display. (Superseded by the Thor test below.)
- Decision (2026-10-19): the emulator is switched off. Testing continues on the real AYN Thor (Android build) and, on the same device, its 3DS emulators for the 3DS frontend.

Next on the Thor: install `build/lemuroid-debug.apk` (arm64), check the bottom screen shows the tracker, then start the start menu with 4 run slots.

## Thor test (2026-10-19): AYN Thor, Android 13, arm64

- Displays: id 0 `Built-in Screen` 1080x1920 (the top screen, runs the game and the app UI in landscape) and `Screen-2` 1080x1240 (bottom screen, flagged `FLAG_PRESENTATION`, so `DISPLAY_CATEGORY_PRESENTATION` returns it). Screenshots per display: `adb exec-out screencap -p -d <SurfaceFlinger display id>` (see `tools/thor/shot.sh`); the game surface comes out black (secure surface), the Presentation does not.
- Installed `build/lemuroid-debug.apk`, scanned `Download/roms`, booted `smz3_test_popoie_fang.sfc`: **the tracker shows on the bottom screen**. It read "Waiting for a file" before a file existed, then "Super Metroid", a running timer (0:00:27 after 27 s), Zelda 0/216 and Metroid 0/100 once the file started. Memory reads and the C core work on the device.
- Input: `adb shell input keyevent` (also with source `gamepad`) does not reach Lemuroid. The Thor has Magisk root, and `sendevent` on `/dev/input/event9` (`Odin Controller`) does (`tools/thor/pad.sh`, BTN_START=315, BTN_A=304).
- Positive check and Zelda side: verified later (see "Check detection in play" and "Zelda pickup, persistence").

## Phase 4: start menu with 4 run slots (2026-10-19, tested on the Thor)

- New first tab **Runs** (the start destination) in Lemuroid: four slot cards in one row on the Thor's top screen. An empty slot offers **Create run**; a used one shows name, Zelda/Metroid check counts, time played, current game and last played date, a progress bar, and **Open** → dialog with **Continue** / **Delete** / Cancel.
- **Create run** opens an options panel (run name, ROM from the library, logic Normal/Hard, keysanity). Code: `lemuroid-app/.../mobile/feature/runs/` (`RunStore`, `RunsScreen`, `RunCreateScreen`), plus the route in `MainNavigationRoutes.kt`, `MainActivity.kt` and a `GameDao.selectListBySystem` query.
- Each slot is a JSON file under the app's `files/runs/`. The game runs in the separate `:game` process, so the active slot is shared through the file `runs/active`, and `Smz3Tracker` writes the progress (checks, game, play time, last played) into that slot every ~5 s. The Runs screen refreshes on resume.
- Verified on the Thor: created Run 1, Continue launched the game, the file started through the controller, and the slot then read "Zelda 0/216 · Metroid 0/100 · 0:00:41 played · in Super Metroid · Oct 19, 2026".

Known limits:
- Create run does not generate a seed yet: it picks an existing ROM from the library, and the logic/keysanity choices are only stored. Until the generator is ported, runs on the same ROM share its save file. Per-run ROMs (one generated ROM per slot) fix this.
- The options panel has 3 options, not the web's full set.
- Positive check detection: verified later on the Thor.

Next: port the generator (seed + patch) so Create run produces a ROM per slot with the full options panel; verify a positive check; then the 3DS frontend. (All done in the later sections.)

## Phase 5: bottom screen v2 (verified on the Thor)

The bottom screen now has four tabs (touch): RUN (summary, bars, recent checks), ITEMS (inventory icons, dimmed when not owned, counts for swords/mails/bottles/hearts/ammo/tanks), CHECKS (cleared/total per region, 40 regions) and MAP (the SMZ3 combined map, pan with a finger, pinch to zoom, double tap to refit, yellow dots for pending checks and small green dots for cleared ones).

- `core/smz3_inventory.c`: inventory table and decoder. Addresses come from the Cas' tracker (`ZeldaMiscData.cs`, `MetroidStateChecks.cs`), not from memory: my first list had several wrong offsets (for example the boots/flippers/pearl bytes and the Metroid beam bits). Zelda inventory is read from WRAM while in Zelda and from the SRAM mirror (offset 0x340) while in Metroid. Metroid inventory only exists in WRAM, so the Kotlin side keeps the last known values while Zelda is the current game.
- `tools/extract_locations.py` now records the room of Zelda locations too; `tools/gen_map_markers.py` joins `data/locations.json` with the tracker's `maps.json` to place all 316 locations on `smz3_fullmap.png` (position = room X/Y x region scale + map offset; 316 of 316 placed, checked by drawing them over the map).
- `tools/prepare_assets.py` copies the item icons and a half-size map (webp) plus marker positions into the Lemuroid assets. They are not committed: the sprites repository has no license, and `vendor/` is ignored.
- Kotlin: `Smz3Presentation.kt` (tabs, pages), `Smz3MapView.kt` (custom view); JNI gained `inventory`, `inventoryInfo`, `locationInfo`, `locationCount`, `inventoryCount`.
- Thor testing tip: `adb shell input -d 4 tap X Y` taps on the bottom display (display id 4); the bottom screen is 1240x1080.
- Seen again: the slot playtime counted 0:14:20 although little was played, so the playtime-while-backgrounded bug is real.
- Logic reachability, positive-check and Zelda-side verification and the generator were done in the later phases.

## Phase 6: headless generator (works on the PC)

`tools/gen-cli` is a small .NET console app that builds the Cas' tracker DI container without Avalonia (`AddConfigs`, `AddRandomizerServices`, MSU library, sprite services, as in the project's own PatchBuilder) and calls `IRomGenerationService.GenerateRandomRomAsync`:

```
dotnet build tools/gen-cli -c Release
dotnet tools/gen-cli/bin/Release/net10.0/gen-cli.dll rom/supermetroid_ju.sfc rom/alttp_usa.sfc out.sfc [options.yml]
```

It writes a 6 MB SMZ3 ROM in about 3 seconds. Needs: the text/profile configs from `TheTrackerCouncil/SMZ3CasConfigs` copied into `~/.local/share/SMZ3CasRandomizer/DefaultData/Configs` (without them every game line is empty and patching throws a FormatException), and the SQLite context migrated (done by the harness). (Superseded: the generator is ported to C and runs on the device, offline; this C# CLI is only the test oracle.)

## Phase 7: HUD themes

The bottom screen follows the game being played. `Smz3Theme` holds the palettes: Zelda (dark green, gold frame, serif type) and Super Metroid (near-black, orange visor frame, monospace, cyan bars). `Smz3HudView` draws the top panel of the RUN tab like the real HUD: a "LIFE" row of hearts with heart pieces and bottles for ALttP; ENERGY with tank squares and missile/super/power bomb counts for Super Metroid (the ammo numbers are the maximum capacities the tracker reads). Tabs, text, bars and map dots are recoloured on every switch. Before a file starts it keeps the last theme (Zelda at first).

Preview without playing both games: put `1` or `2` in `files/debug_game` of the app (`adb shell run-as com.swordfish.lemuroid.debug sh -c 'echo 1 > files/debug_game'`) and delete it afterwards. Verified on the Thor in both themes.

## Phase 8: Create run generates a real seed (verified on the Thor)

`tools/gen-cli serve <sm-rom> <z3-rom> <port>` (start it with `tools/serve-generator.sh`) is an HTTP service around the Cas' tracker generator:

- `GET /schema` describes the options panel straight from the source: choices for game mode, keysanity, item placement, Tourian door and pyramid hole (with the web's descriptions), plus every logic checkbox with its label and group (read from the `DynamicFormFieldCheckBox` attributes).
- `POST /generate` takes `{seed?, keysanity, itemPlacement, gameMode, tourianBossDoor, pyramidHole, logic:{Name:bool}}` and answers with a patch ("SPT1": u32 length, then u32 offset, u32 length, bytes records) against the raw combined Super Metroid + ALttP ROM, plus the seed in `X-Seed`. About 1 MB, 15 to 20 seconds.

In the app, Create run loads the schema (server URL editable, default `http://localhost:8765`, which on the Thor is the PC through `adb reverse tcp:8765 tcp:8765`; the same URL can point to a server on the network), asks for the seed, finds the user's Super Metroid and ALttP ROMs on the device by their internal header (library first, then the usual ROM folders), combines them like `Rom.CombineSMZ3Rom`, applies the patch and writes `files/runs/smz3_run_N.sfc`. That file is registered in the library as the slot's own game, so every run has its own SRAM and savestates. Continue re-registers the game if the library dropped it. Delete removes the ROM. Seen working: slot 2 created from the options panel, launched from Continue, bottom screen tracking it with the seed stored in the slot JSON. (Superseded: Create run now generates on the device through the shared C core, no server; the service remains an oracle tool.)

Cleartext HTTP is allowed through `network_security_config.xml` because the service has no TLS. Playtime now only counts while the game's frame counter moves (ALttP 0x7E001A / Super Metroid 0x7E05B6), not just while the process is alive.

## Phase 9: logic reachability

`POST /logic` on the generator service takes `{settings, items:[ItemType names, repeated for counts]}` and answers with the ids of the locations that are in logic (`Location.IsAvailable(Progression)` over a `World` built from the run's settings and cached per settings). The tracker maps the inventory it reads from memory to ItemType names (`RunLogic.items`; ammo capacities become pack counts, bottles and sword/shield/mail/glove levels repeat), asks the service whenever the inventory changes and keeps the answer. The run stores its generation settings so the logic matches the seed's options. On the MAP tab pending checks in logic are bright and the rest are small grey dots; CHECKS shows "+N in logic" per region. Verified on the Thor with a fresh Super Metroid file: only the starting checks light up.

Limits: dungeon rewards (pendants, crystals), defeated bosses and keysanity keys are not sent yet, so checks that depend on them stay out of logic; the service must be reachable (nothing lights up differently when it is not, the last answer stays). (Superseded: `RunLogic.reachable` calls the C logic through the JNI on the device.)

## Phase 10: discovery, mini-map, map mode
- `Smz3Discovery` tracks SM rooms (WRAM 0x079B) and ALttP overworld screens / dungeons (0x8A, 0x040C, 0x1B) per run (`runs/slotN.disc.json`) and converts them into discovered rectangles of the half-size map.
- `Smz3MapView` draws a veil over undiscovered areas (light when the whole map is shown, dark in "discovered only" mode, where markers outside seen rectangles are hidden) and the player dot; a non-touch `follow` instance is the RUN mini-map (a crop centred on the player).
- Create run has a "Tracker map" choice stored as `Run.mapMode`.
- Position decoding on the Thor: the Super Metroid ship shows at the right place on the map and the ALttP overworld dot follows Link (seen in play, 2026-10-20). Indoor ALttP positions: checked on the Thor on 2026-10-20 (Link's House: the dot sits on its marker, `smz3_zelda_room_location`, see "Second round").

## Phase 11: light the dark rooms
The generator has no dark-room patch, so Create run applies one to the finished ROM: every ALttP underworld room header (table at $04:F1E2 of the ALttP half, headers in bank $04) has the "dark room" flag in bit 0 of byte 0; clearing it for rooms 0-295 lights all caves and dungeon rooms. Option "Light the dark rooms" in Create run, stored as `lightDarkRooms` in the run settings. Verified on the ROM bytes (Old Man cave rooms 0xE4/0xE5 carry the flag) and in play on the Thor on 2026-10-20 (a lit and a dark room, `docs/shots/dark-room-flag-*-in-game.png`, see "Second round").

## Verification index (2026-10-20 review)

Each claim below names its proof. "NOT VERIFIED" means no capture, log or test backs it.

| Area | Proof |
|---|---|
| C generator equals the C# oracle (fill, patch, modes, hints, plando) | `tools/test-fill.py` 10/10, `test-patch.py` 10/10, `test-modes.py` 40/40 (1 seed unfillable in both), `test-hints.py` 80/80, `test-plando.py` 60/60 |
| Menu path equals the generator, jingle row, plando path | `build/test_ui`, `build/test_host` (needs the IPS dir and a plando YAML) |
| Zelda chest and misc decoding | `core/test_state_zelda.c`; real pickup on the Thor (log `zelda=0/216` to `zelda=1/216`) |
| Metroid pickup on the Thor | log `metroid=0/100` to `metroid=1/100` plus green map dot |
| Metroid pickup in Azahar (3DS panel) | `docs/shots/3ds-panel-real-pickup-azahar.png` and `.txt`, game RAM read through `debug_state.txt` |
| 3DS ARM ROM equals the x86 ROM | byte comparison against `build/gen_rom` (0 differing bytes), seeds 1108909371 and 1451069107 |
| Persistence | Thor: `GameViewModelSaves.write` log, Continue after `force-stop` restores `zelda=1/216` |
| Periodic autosave (60 s) | Thor log while playing without leaving |
| Dark rooms | ROM bytes, `gen_rom dark=`, in-play shots |
| Indoor Zelda position | Thor, Link's House (dot on its marker) |
| Sprites (Samus) | in-play shots `docs/shots/samus-*-in-game.png`, byte check against the C# manifest |
| HUD sheet and fallbacks | Thor, generated sheets (valid, none, corrupt, 130x32) |
| Helps (all, limited, none) | Thor (Android) and Azahar (3DS pause menu), `<rom>.help` counters |
| NOT VERIFIED | Link sprite in play; real 3DS hardware (menu, generation time and memory, picture, panel, hints, `.srm`); official HUD graphics; syncing with a real RomM client (only the file naming was designed and the stable names checked); the playtime-while-backgrounded fix (no dedicated test); logic reachability with dungeon rewards, defeated bosses and keysanity keys in the Android tracker (never sent, see Phase 9 limits); 3DS Zelda pickup and the game-to-game transition counters (in progress at the time of this review) |

## Sprite picker grid and Helps row (2026-10-20)

- The Link and Samus pickers are a 5x3 grid with the sprite's picture (`UiCmd` kind 2, a `<name>.pv` file next to the `.rdc`: u8 w, u8 h, RGBA, at most 56x56, made by `tools/gen_sprite_previews.py`; 628 files, 6.9 MB). The d-pad moves by cell, L/R by page of 15, touch picks a cell, the chosen sprite has an accent bar. The limit was 128 names (silently dropping most of the 517 Link and 107 Samus sprites); it is 640 now and the names are sorted (case-insensitive). Plandos keep the plain list.
- "Helps available" only shows when the help mode is "Limited helps" (`visible()` in `core/ui/smz3_ui.c`; asserted in `test_ui`).
- Frontends: Android draws previews in `NativeMenuScreen.kt` (decoded once, nearest-neighbour scaling, a missing or corrupt file leaves the cell blank); the 3DS draws them in `smz3run.cpp` (cache of 40, runs of one colour become one rectangle in the software framebuffer).
- Proof so far: `core/ui/test_preview.c` (run under ASan/UBSan: 20000 fuzzed buffers, truncated, zero-size and empty ones rejected, all 628 real files parse; the worst page of 15 needs 13478 rectangles, about 47k pixel writes) and `docs/shots/sprite-picker-grid-layout.png` (a PC rendering of the real draw commands with the real `.pv` files, not a device capture).
- The Lemuroid app was stripped to the runs menu (no top bar, no bottom navigation) and relaunches itself on the main screen when started on the bottom one.

### Only the Thor can check (do it all at once when it is connected)

1. Install `build/lemuroid-debug.apk`; open it from the bottom-screen launcher: it must end up on the top screen, showing only the runs menu (no bars).
2. Copy `build/pv/link` and `build/pv/samus` (from `tools/gen_sprite_previews.py`) to `/sdcard/SMZ3/sprites/{link,samus}`; open Create run, both sprite pickers: all 517 / 107 sprites reachable, pictures drawn, d-pad, L/R paging, touch a cell, last sprite of the last page, picking one and generating a run with it.
3. Rename one `.pv` away and truncate another: the picker must not hang or crash.
4. "Helps available" appears only with Limited helps (Simple tab).
5. Azahar: push `build/snes9x_3ds.3dsx` and the previews to `sdmc:/SMZ3/sprites`, repeat 2-4 on the 3DS menu (pad and touch); page-turn speed.
6. Azahar Zelda side: chest pickup through the Save and Quit spawn menu, return to Super Metroid through the portal, both counters kept (the route to the portal and the Fortune Teller arrival are done, see `tools/azahar-input.py`).
7. Link sprite visible in play (Zelda) with a chosen sprite.
8. Rerun the persistence check with the new APK (periodic autosave, force-stop).

## Portal freeze: the base ALttP ROM revision matters (2026-10-20)

- Symptom (user, on the Thor): entering a portal (Crateria map station, Super Metroid to the Zelda Fortune Teller) hangs the game. Reproduced in Azahar too, also with a ROM from the C# generator, so it is not the C generator: after the transition WRAM `$1A` (frame counter) stays at 0xA1, module 0x0F, room 0x122, and the CPU sits in the SM BRK handler (`PB:PC 80:8573`, same registers on the Thor's autosave and in Azahar). The stack shows a BRK/COP raised at `0D:FD11` (file 0x4DFD0F).
- Cause: the SMZ3 IPS patches are written for A Link to the Past Japanese v1.0 (MD5 `03a63945398191337e896e5771f77173`, the tracker's `SourceRomValidationService`). The ROM in use (`alttp_usa.sfc`, MD5 `608c22b8ff930c62dc2de54bcd6eba72`) is another revision: at `0x4DFD02` the patch writes `JSL $B8B7AF` plus eight NOPs, which in that ROM ends one byte too late and leaves `0b 02 c9 ..` (PHD; COP). Super Metroid (JU, `21f3e98d...`) was the right one. The C# generator does not check, so all oracle tests passed with the wrong base ROM (the C and C# ROMs are identical in the code region `0x2A0000+`).
- Fix: `core/gen/smz3_md5.{c,h}` (`test_md5.c` checks the standard vectors and prints the MD5 of a file) and a check in both frontends before generating: only the exact revisions are taken as base ROMs, and a wrong one shows "ALttP ROM must be Japanese v1.0 (03a63945)" (3DS: `findBaseRoms`; Android: `RunGenerator.findBaseRoms` plus `NativeMenuScreen`, which now also shows why a generation failed for 5 s). Verified on the Thor with the wrong ROM installed: `docs/shots/wrong-alttp-rom-message-thor.png`. NOT VERIFIED: that the portal works with a correct ROM (it is not available here); runs created before this change with the wrong ROM crash at portals and must be recreated.
- Also verified on the Thor: the app opened from the bottom screen relaunches on the main screen (the relaunch needs `FLAG_ACTIVITY_MULTIPLE_TASK`; without it the new task was lost) and shows only the runs menu.

## Direction (2026-10-20): all menus, options and generation live in the shared C core

Requested by the user: the options system, run loading and generator must work in-game, without a separate Android-only UI, because the same thing has to run on the 3DS.

- New shared module `core/ui`: a pure-C state machine (slot select, run summary, Create run, options, sprite picker, generating progress) with no platform calls. It consumes input events (d-pad, A/B, touch) and emits draw commands (rects, text, icons) into a small framebuffer/command list. Each frontend only blits it: Android draws it in the bottom-screen Presentation (and on top when no game runs), the 3DS draws it with citro2d on the bottom screen.
- Options come from a C table (`core/ui/options.c`), generated from the same schema the generator uses (`tools/gen-cli schema`), split into Simple (mode, keysanity, placement rule, goal crystals, seed) and Advanced (logic tricks, tourian, pyramid, dark rooms, ...), like the web generator.
- Sprites: a C sprite service applies the Link (ALttP) and Samus (SM) sprite files to the generated ROM (`.spr` for Link, IPS/Cas format for Samus), with a picker that previews the sprite. Sprite files ship as assets/SD card files.
- Run storage moves to a C-readable format (slot files with a fixed header + settings blob) so the same files work on both devices.
- The Kotlin run screens (RunCreateScreen, Runs screen) become a fallback and are removed once the C UI reaches parity on the Thor.

## Native saves and savestates, and syncing with RomM (investigated 2026-10-20)

RomM (rommapp/romm; "RomMancer" in the request) syncs saves per ROM: the device sends, for each `rom_id`, a list of save files and state files with `file` name, `mtime` and `sha1`; the server answers with upload, download, noop or conflict operations (`keep_both` by default). The clients (Grout, Argosy, romm-android) pair a local save with a ROM by platform and file name without extension. States are plain files; the protocol carries no emulator or core information. Sources: docs.romm.app device sync protocol and Saves & States, grout.romm.app save sync.

What this means for generated ROMs:
- A run's ROM needs a stable file name and has to exist in RomM's library for its saves to be paired. Name it `smz3_<seed>_<settingshash>.sfc` (settings hash = first 8 hex of FNV-1a over the settings JSON) and keep `<same name>.srm` and `<same name>.state` beside it. Two runs with the same seed and settings are the same ROM, so they share saves, which is the wanted behaviour.
- Because the generator is deterministic per seed and settings, another device can regenerate the identical ROM from the run file instead of downloading it; RomM then only has to carry the `.srm` and `.state` files. A savestate only restores on the identical ROM and the same snes9x core, so states are best treated as same-core only.
- Conflicts (same ROM played on two devices between syncs) appear in RomM as two entries; the player picks one. The run file's play time and last played time help choosing.

Where each frontend keeps saves today: Lemuroid writes `.srm` and states in its private app storage (not reachable by a sync client); snes9x_3ds writes `.srm` beside the ROM and states in its own folder. Work to do (done, see "Stable ROM names and shared save folders"): export Lemuroid's saves to `/sdcard/SMZ3/saves/<romname>.srm|.state` (or point its save directory there) so Grout/Argosy or a RomM client can sync them; on the 3DS use `sdmc:/SMZ3/saves/` with the same names. Uploading the generated ROM itself to RomM (API) is a later option and needs the user's server credentials.

### core/ui status (2026-10-20)

`core/ui/smz3_ui.{h,c}` + `smz3_ui_opts.c` hold the menu state machine on a 320x240 canvas: slot list, slot menu (Continue/Delete), Create run with Simple and Advanced tabs (L/R switch), sprite pickers, busy screen. `ui_input` takes buttons, `ui_draw` fills a list of rect/text commands, `ui_settings` turns the choices into `SmzSettings` + `SmzPatchCfg` + sprite names; the host reads `u->action` (continue, delete, generate). `core/ui/test_ui.c` scripts a whole flow (build line in the file header of the commit message: gcc over core/ui and core/gen). Next: a C-side run store, the Android renderer for these commands, and the sprite service that applies the chosen sprites. (All done in the sections below.)

### Sprites (2026-10-20)

`core/gen/smz3_sprite.{h,c}` parses RDC files and applies the Link (type 1) and Samus (type 4) blocks exactly as the C# `Rdc`/`DataBlock.Apply` manifests do; `smz_generate` applies them before the Cas IPS patches, like `RomGenerationService`. Checked: every `.rdc` in vendor/SMZ3CasSprites parses, and the Link graphics land byte-identical at 0x508000. `SmzPatchCfg` gets `link_rdc`/`samus_rdc`; JNI `generate` takes both as optional byte arrays; `gen_rom` reads them from `SMZ_LINK`/`SMZ_SAMUS`. Not shipped in the APK: the sprite folders are about 17 MB (Link) and 51 MB (Samus with previews), so they will be read from a `sprites/` folder on the SD card.

### Menu on the Thor (2026-10-20, tested)

The Runs tab is now `NativeMenuScreen`: a Compose canvas that forwards keys/touches to `core/ui` through the JNI (`uiInput`, `uiTouch`, `uiDraw`, `uiAction`) and draws its rect/text commands. Slot list, Continue/Delete, Create run (Simple/Advanced tabs, seed, keysanity, placement, crystals, sprites, tracker map, dark rooms, screen filter), sprite pickers and generation all go through the C code; `ui_settings_json` produces the settings JSON stored with the run. Verified on the Thor by touch: created Run 4 with the Link sprite "abigail.1" and the Samus sprite "140", the ROM generated offline and booted to the title screen. Sprites are read from `/sdcard/SMZ3/sprites/{link,samus}/*.rdc`. The sprites in game and the real controls (sendevent on the Odin Controller) were checked later; the old Kotlin screens (`RunsScreen`, `RunCreateScreen`) are still in the tree but unused.

### Stable ROM names and shared save folders (2026-10-20, tested on the Thor)

New runs write `smz3_<seed>_<settingshash>.sfc` (hash of the ROM-relevant settings, see `RunStore.romName`) so the ROM, `.srm` and state names are the same on every device. `DirectoriesManager` now keeps saves and states in `/sdcard/SMZ3/saves` and `/sdcard/SMZ3/states` when that folder is writable (copying what was in the app folder) so a RomM client such as Grout or Argosy can reach them. Verified: a new run is named `smz3_1049978792_43a1c78e.sfc` and the `.srm` folder moved. Dark-room lighting now happens inside the C generator (`SmzPatchCfg.light_dark_rooms`), and generation validates a playthrough with up to 5 attempts (`smz_fill_valid`), which the tracker's logic call shares.

### 3DS frontend (2026-10-20)

`vendor/snes9x_3ds` gets `source/smz3run.{h,cpp}`: the file browser has a first entry "SMZ3 runs ..." that opens the shared `core/ui` menu on the second screen (320x240, exactly the canvas size), driven by the D-pad, A/B, L/R and the touch screen. Create run generates offline in a 512 KB thread through `smz_host_generate` (`core/ui/smz3_host.c`, a C wrapper so the C++ side never includes the generator headers), writes the ROM and a `slotN.run` file (`core/ui/smz3_run.c`) under `sdmc:/SMZ3/runs/`, and Continue hands the ROM path to the emulator's normal loader. Inputs: the player's own Super Metroid and ALttP ROMs from `sdmc:/SMZ3/base`, `sdmc:/roms/snes`, `sdmc:/roms` or the SD root (found by internal header); the 25 IPS patches are bundled in the app's romfs; sprites come from `sdmc:/SMZ3/sprites/{link,samus}/*.rdc`. `./build-3ds.sh` copies the C core next to the emulator sources, applies `patches/snes9x_3ds-smz3.patch` and builds `build/snes9x_3ds.3dsx`; the build links cleanly. (Run time, tracker panel and saves on the 3DS were done and tested in Azahar afterwards, see the next sections; real hardware is the only thing left.)

### 3DS verified on the Thor (2026-10-20)

Ran `build/snes9x_3ds.3dsx` in Azahar on the Thor and drove the menu with the pad (sendevent on the Odin Controller): slots, Create run, generation, run file and ROM written under `sdmc:/SMZ3/runs/`. The ARM ROM is byte-identical to the one the x86 generator builds for the same seed and settings. Getting there needed three fixes: the guid derivation ran before the seed was set in `ui_settings`; `smz3_text.c` drew two `pick()` values inside one call, whose argument order differs between GCC for x86 and for ARM (now hoisted into locals); and the .NET Random seeding used signed overflow (now unsigned wraparound, still 0 mismatches against the C# vectors). `core/ui/test_host.c` checks that the menu path equals the plain generator.

### Hint tiles and help modes (2026-10-20)

**Hint tiles in C.** `core/gen/smz3_hints.{h,c}` ports the C# `GameHintService` for the Cas generator: the pool (up to 5 progression item hints from the last third of the playthrough, the pendant dungeons plus Hyrule Castle and Ganon's Tower, 32 location/room/group hints, the two medallion hints), the .NET `Random(seed).Sanitize()` shuffles (progression picks, whole pool, tile order), and `CheckIfLocationsAreImportant` (locations replaced by rupees, replay of the spheres, `VerifyWorld`, the keysanity key check). The hint seed is the second value drawn after filling (`SmzPatchCfg.hint_seed`). Static data (pool, tile keys, location names) is generated by `tools/gen_hints.py` from `gen-cli hintdata` into `smz3_hints_data.h`. The wording is picked from the seed (the C# picks with an unseeded `Random`, so only the choices can be compared). `tools/test-hints.py` compares tile code, type, locations, usefulness and medallion with the C# generator: 40/40 seeds identical (keysanity and placement rules mixed). `tools/test-patch.py` now skips the text patches on both sides. The 3DS ARM ROM with hint tiles is byte-identical to the x86 one (seed 1108909371, run on the Thor).

**Help modes.** The level of help is chosen when a run is created (Simple tab, `HelpMode` + `HelpCount`, stored in the run settings as `helpMode`/`helpCount`):
- *All helps*: 15 hint tiles in the ROM, pending checks on the map, in-logic highlighting and unlimited hints from the HELP tab.
- *Limited helps* (default 3, 1..15): no hint tiles, no pending dots on the map and no list of what is left. Each help asked costs one: a hint (the same 15 hints the tiles would show, in order, `smz_help_texts` / JNI `helpTexts`) or "show the checks left in a region" (those dots appear on the map and in the CHECKS list). Everything given stays visible (hint history, revealed regions) and is stored in `slot<N>.help.json` next to the run.
- *No helps*: no tiles, no pending dots, no HELP tab, no logic information.
`helpCount` is a tracker option and is not part of the stable ROM name; `helpMode` is (all vs the others changes the ROM). Implemented on the Thor in `Smz3Help.kt`, `Smz3Presentation.kt` (HELP tab, inline region picker because a Presentation window cannot open dialogs) and `Smz3MapView.setVisibleChecks`; tested on the Thor with a Limited run (3 helps, a hint and a region reveal spent, buttons disabled at 0). The None mode was checked on the Thor too (no HELP tab, no pending dots on the map). The 3DS has the same options in the shared menu and the same hint tiles in the ROM; its help requests during play go through the pause menu (see "3DS hints on request").

### 3DS saves and in-game run (2026-10-20)

`file3dsGetRelatedPath` (snes9x_3ds) sends `.srm` and savestates of runs under `sdmc:/SMZ3/runs/` to `sdmc:/SMZ3/saves` and `sdmc:/SMZ3/savestates` with the ROM's stable name (folders created with the run). On the Thor's Azahar, Continue on a generated run loads the ROM and starts Super Metroid (the picture is garbled, as every snes9x_3ds game is in Azahar; real hardware untested). (The `.srm` write and the in-play panel were verified later, see "3DS verification round".)

### 3DS tracker panel (2026-10-20, tested on the Thor)

While a run plays, `smz3TrackerFrame` (called once a second from `emulatorLoop`) draws a panel on the second screen from the emulator's RAM/SRAM with the shared `core/smz3_state.c`: current game, Zelda x/216, Metroid y/100 and play time. Verified in Azahar on the Thor (title screen: "Waiting for a file"). Counts mid-game were checked later (next sections). By scope the 3DS panel is text only: no recent-checks list, map, inventory or HUD icons (those are Android-only), and the help requests live in the emulator's pause menu.

### 3DS hints on request (2026-10-20, tested on the Thor)

Touching the bottom screen while playing opens snes9x_3ds's pause menu (that is the emulator's own behaviour), so the helps live there: for a generated run the "CURRENT GAME" list has "Ask for a hint". Generation (`smz_host_generate`, new `hints_out`) also writes the 15 hints a player may ask for into `<rom>.help` next to the ROM (`mode count used given`, then one hint per line; none for the no-helps mode). Asking spends a help in the limited mode (the dialog shows how many are left) and gives the next hint; the all-helps mode is unlimited. Verified in Azahar on the Thor with a limited run (3 helps): first hint shown, "helps left: 2 of 3". Revealing a region does not exist on the 3DS because its tracker has no map. Raw touch for tests: `sendevent /dev/input/event5` (the bottom panel's touchscreen, ABS_MT_POSITION up to 1080x1240).

### 3DS verification round (2026-10-20, Azahar on the Thor)

- **Panel counters.** With Super Metroid really running (pad macro: Continue, Start, A x12 until the panel says "Super Metroid" and the clock runs) the counters read the emulator's memory correctly. Real pickups are impractical blind (Azahar's picture of snes9x_3ds is garbled), so `sdmc:/SMZ3/debug_poke.txt` (lines `w <hex offset> <hex value>` for WRAM, `s ...` for SRAM, OR-ed in once a second; harmless when absent) injects flags: three Metroid flags and two Zelda flags (the saved copy read while in Super Metroid) gave Metroid 3/100 and Zelda 2/216 on the panel. (That Zelda test used masks, matching the old decoder; after the chest-bit fix below the poke for Link's House is `s 208 10`, and the panel shows Zelda 1/216 and Metroid 1/100 with `w d870 01` too.)
- **.srm.** Opening the pause menu writes the SRAM: `sdmc:/SMZ3/saves/smz3_<seed>_<hash>.srm` (32 KB) appears with the stable name.
- **Helps.** Limited (3): hint shown, 2 of 3 left, file `1 3 2 2` after two hints. All: hint given, `used` stays 0 (`0 3 0 1`). None: "This run has no helps.". The counters live in `<rom>.help`, so relaunching and pressing Continue again keeps them (`1 3 1 1` after a fresh start); Reset only restarts the console and never touches the file.
- **Pending for the owner (real hardware, cannot be done here):** run `build/snes9x_3ds.3dsx` on a real 3DS: menu, generation time and memory (512 KB generation thread), picture, the panel, the hint request from the pause menu, `.srm` in `sdmc:/SMZ3/saves`.

### Sprites in game (2026-10-20)

Checked on the Thor (Android app, which renders correctly) by playing a generated run until Super Metroid started (the combined ROM soft-resets a few times after the file select; alternating Start and A gets through it):
- Samus "combat-armor": the beam-in on the ship shows the chosen sprite with its own palette (`docs/shots/samus-combat-armor-in-game.png`).
- Samus "140": the sprite is a geometric figure (white square, ball, triangle); the same white square shows on the file select. The ROM bytes of every Samus field equal what the C# manifest writes (rebuilt in Python from `Rdc.cs` and compared against the final ROM, 0 differing bytes in all regions), so this is the artwork, not a patch bug (`docs/shots/samus-140-in-game.png`).
- Link: NOT VERIFIED on screen (Zelda was reached later, but the Link sprite was not compared in play). The Link block goes through the same code and was already shown byte-identical to C# at 0x508000.
- 3DS: Azahar draws garbled output, so the sprites there are covered by the byte-identity check only.

## Game modes and start-with / early items (2026-10-20)

Ported to the shared C core and verified against the C# oracle (`tools/test-modes.py`, `tools/test-hints.py`, `test-fill`, `test-patch`: identical placement, patch bytes and hint tiles on random seeds, including seeds both generators reject).

- Game modes: Vanilla, All Dungeons, Spazer Hunt (spazers in pool / required). Modes force 7 crystals and 4 bosses; All Dungeons forces a closed pyramid. Mode flag, goal texts, Ganon texts and hint `VerifyWorld` rules follow the C# code.
- Items tab in Create run (48 groups, 181 options from `ItemSettingOptions.yml`, generated by `tools/gen_itemopts.py`): "Have X" removes the item from the pools and writes it to the starting equipment; "Early X" places it in an early sphere before the main fill.
- `smz_setting_set` accepts `GameMode`, `SpazersInPool`, `SpazersRequired` and `opt:<group>`; the Android JNI and the 3DS job pass them through.
- Thor: a run created from the Items tab with Spazer Hunt + "Have Bow" generated on the device. Its ROM is byte-identical to `gen_rom` from the PC build except the pyramid flag (the UI default is open) and the checksum that follows from it.
- Not supported (documented limits): RandomizeNumericAmounts, per-location item preferences (LocationItems), LiftOffOnGoalCompletion, the "sphere 2" early-item validation.
- Etecoons jingle: all 19 jingles are dumped from the C# patch (`tools/gen_jingles.py`) and applied by the C patcher (random pick derived from the seed, or fixed via `SmzPatchCfg.etecoons_jingle`); `test-patch.py` compares them on random seeds (30/30). It has a Simple-tab row on both frontends (see below).
- Plando text overrides: done (see the next section).

## Plandomizer (2026-10-20)

A plando config is the YAML the C# Cas' Randomizer writes next to every seed (`Spoiler_Plando_*.yml`, PascalCase keys). `core/gen/smz3_plando.{h,c}` parses the flat subset it uses: `Seed`, `GameModeType`, goal counts, `KeysanityMode`, `SkipTourianBossDoor`, `OpenPyramid`, `Logic`, `Items` (location name to item), `Rewards`, `Medallions`, `ZeldaPrizes`, `WaterfallFairyTrade`, `PyramidFairyTrade`. `smz_generate_plando` builds the seed from it (no filling) and patches like the C# plandomizer: prize and fairy-trade overrides, the Tourian plaque of plandos, no hint tiles, patch seed = the config's seed number (`smz_seed_number`: integer, hex or FNV-1a hash).

- Verified against C#: `tools/test-plando.py` (gen-cli `plandobatch` exports each random seed's plando YAML, plays it back through the real `Smz3Plandomizer` and dumps the patches; the C port must write the same bytes): 60/60 identical, plus 30/30 and 40/40 on other seed sets (modes, keysanity, doors, jingles included; seeds the C# cannot generate are skipped).
- Menu: a "Plando" row (Simple tab) picks a file from `/sdcard/SMZ3/plando` (Android) or `sdmc:/SMZ3/plando` (3DS). The plando's settings replace the form's and are what the run stores; on Android the YAML is kept inside the run's settings (`plandoYaml`) so the tracker's logic and the help hints use the plando's items, rewards and medallions.
- Thor: generated a run from a plando file in the app; it plays (Super Metroid starts, panel works) and its ROM equals the PC build's except the guid bytes. 3DS: `test_host` shows the menu path equals `smz_generate_plando`; on the 3DS (Azahar on the Thor) the plando picker was driven end to end: pick `test.yml`, create the run, Continue starts Super Metroid and the panel responds; the generated ROM equals the PC build's except the guid bytes and checksum. A 3DS Spazer Hunt + "Have Bow" run is byte-identical to PC `gen_rom` (0 differing bytes). Real 3DS hardware is still untested.
- The `Text` section (30 overrides: Ganon, NPCs, pedestals, signs, the 15 hint tiles) works for single-line values (plain, single or double quoted); multi-line YAML block scalars are ignored. Verified byte for byte: every second test case overrides all 30 texts, which makes the whole text patch (the fixed slots and the 0x1C8000 string table, entry by entry) deterministic and equal to the C#'s, except the two merchant choices, whose Yes/No wordings stay random in both generators.
- Ignored, as in the C# plandomizer: `StartingInventory` and `TrackerLocationLines` (it clears the item options). Not supported: `LiftOffOnGoalCompletion`, multiworld.

## Etecoons jingle row, oracle trace patch, check detection in play (2026-10-20)

- **Etecoons jingle** is a Simple-tab row ("Random" + the 19 jingles, `UIO_JINGLE`). It sets `SmzPatchCfg.etecoons_jingle` in `ui_settings` (3DS and the shared menu path) and is stored in the run's settings JSON as `etecoonsJingle`; Android's generator takes it as the eleventh native parameter (`RunGenerator.nativeParams`, `fill_settings` in the JNI). Verified: `test_host` (menu path with jingle 3 equals `smz_generate` with jingle 3 and differs from the default ROM) and on the Thor (run created from the menu with "Battletoads Win": the device ROM is byte-identical to `build/gen_rom ... jingle=3`, 0 differing bytes; the first attempt showed the row did not reach the ROM because the JNI parameters had no jingle slot, now fixed). `gen_rom` accepts `jingle=N`.
- **Oracle trace patch**: `patches/smz3randomizer-trace.patch` adds `PatcherService.Trace` (the list of patch updates the C# patcher produces) to `vendor/SMZ3Randomizer`; `tools/setup-oracle.sh` applies it idempotently (`vendor/` is gitignored), then rebuild `tools/gen-cli`. `test-patch.py` and `test-plando.py` need it. Checked: it applies to a pristine checkout and the battery passes with it.
- **Check detection in play** (Thor, Android app, real input through `sendevent`, no flags poked): the run used Morph Ball, Bombs and 20 Missiles as start items so the early Crateria path opens (the ROM is `gen_rom ... 2000 ... "opt:Morph Ball=2" "opt:Morph Bombs=2" "opt:Missiles=2"` copied over a run's ROM file). Route: ship, left door, bomb-block tunnel (morph + bombs), shaft, blue door, hall, jump onto the pipes, five missiles on the red door, Bomb Torizo room. The Chozo orb has to be shot (aim up-diagonal with R and fire) and the revealed item touched. Result: the tracker log went from `metroid=0/100` to `game=2 started=true zelda=0/216 metroid=1/100` on the pickup and the check's dot on the bottom screen's map turned green, so the real RAM-to-tracker detection works (no bug found). Earlier `started=false` spans were the screen timing out (it pauses the emulator and blanks both displays) or room transitions; keep `adb shell svc power stayon true` while testing.
- **HUD spritesheet (optional)**: the RUN tab's HUD loads `/sdcard/SMZ3/hud/hud.png` when it exists: four square cells in one row, left to right heart, missile, super missile, power bomb (cell size = image height, any size; the width must be exactly four cells). Missing, corrupt or wrongly sized sheets fall back to the bundled icons. Tested on the Thor with a generated sheet of four coloured squares: heart (Zelda panel), missile, super and power icons were all replaced; with no file, a corrupt file and a 130x32 sheet no crash and the default icons showed. The official game graphics are supplied by the owner (not downloaded here).

## Zelda pickup, persistence, 3DS jingle and HUD scope (2026-10-20)

- **Reaching Zelda.** The SMZ3 ROM always boots into Super Metroid (the boot code at file 0x40CF77 writes `0xFF` to the game byte every reset, so flipping the SRAM byte does nothing and forcing `0x00` there boots to a black screen). Zelda is also reachable by crossing a portal: in Super Metroid the Crateria map station door (door 0x8976, bomb-free route in room 0x92FD, shaft down to the door at its right wall) teleports into the Fortune Teller room (reached in Azahar, the 3DS panel switched to "A Link to the Past"). To play the Zelda side without crossing a portal, restore a Zelda savestate of any SMZ3 ROM into the run (`/sdcard/SMZ3/states/snes9x/<rom>.state` and `.srm`), die (or save and quit) and pick "Continue" on the Game Over menu: the SMZ3 spawn menu offers Link's House (shown as "サムス's House") and the Sanctuary.
- **Bug found and fixed by the first real Zelda pickup.** Opening the Link's House chest set bit 4 of the word at `0x7EF208` (`0x0002` -> `0x0012`) but the tracker stayed at `zelda=0/216`: the table stores the chest bit number (4..11) and `smz3_location_cleared` tested it as a mask. Fix in `core/smz3_state.c` (`1 << flag` for chest checks, mask for misc checks); `core/test_state_zelda.c` reproduces the case (fails on the old code, passes now; build line in its header). Metroid flags are masks and were already right.
- **Result on the Thor** (APK rebuilt with the fix, real input, no pokes): the log goes from `zelda=0/216` to `game=1 started=true zelda=1/216 metroid=0/100` when the chest opens, the Link's House dot on the map tab turns green.
- **Persistence on the Thor.** Leaving the game with Back writes the SRAM and the autosave (`GameViewModelSaves.write ... writingSram=true writingAutoSave=true`, files under `/sdcard/SMZ3/saves` and `/sdcard/SMZ3/states/snes9x`). After `am force-stop`, reopening the app shows the slot as `Zelda 1/216` and Continue restores the open chest with the tracker at `zelda=1/216` (not 0). A force-stop in the middle of a game, without leaving it first, writes nothing: only the last exit counts. (Back has to go to the game's window; after tapping the bottom screen use `input -d 0 keyevent KEYCODE_BACK`.)
- **3DS in Azahar (Thor).** The jingle row is in the 3DS menu (row 16 of the Simple tab: "Etecoons jingle", Random plus the 19 jingles); a run created there with "Battletoads Win" (seed 1451069107) is byte-identical to `gen_rom ... 1451069107 0 0 7 7 4 jingle=3` (0 differing bytes). A real pickup was driven later (see the second round below); the panel was first checked with `debug_poke.txt` (`s 208 10` and `w d870 01`): Zelda 1/216 and Metroid 1/100. The decoder is the same C code as the Android build, whose real pickups (Metroid and Zelda) are verified above.
- **3DS HUD spritesheet.** The optional `/sdcard/SMZ3/hud/hud.png` is Android-only: the 3DS panel is text only (no heart, missile or bomb icons), so there is nothing to replace there and no 3DS fallback is needed.

## Second round: plando host path, Azahar, dark rooms, indoor position, autosave (2026-10-20)

- **Plando host path**: `build/test_host rom/supermetroid_ju.sfc rom/alttp_usa.sfc <IpsPatches> build/plando/<n>.yml` prints "plando host path identical" (seeds 9, 22, 57).
- **Azahar, real pickup (achieved).** The picture of snes9x_3ds is garbled in Azahar (cause: snes9x_3ds renders into 1024x1024 textures and shrinks the texture dimensions to force a 512 stride, `gpu3dsAllocVramTextureAndTarget` in `source/3dsgpu.cpp`; OpenGL ES, Vulkan, software shader and `use_hw_shader=0` do not help, so it is an emulation limit, not a configuration problem), so the run is followed through the bottom panel: with `sdmc:/SMZ3/debug_panel.txt` present, `smz3TrackerFrame` also prints room, Samus x/y, missiles, collected/equipped items and the selected item, and writes them to `debug_state.txt` for scripts. Driving real input through `sendevent` (`tools/azahar-input.py`, `tools/azahar-routes/`; every command runs as one whole adb line), Samus walked the Android route (landing site, door, bomb tunnel, red door with missiles) into the Bomb Torizo room, shot the orb and touched the item: the panel went from `Metroid 0 / 100` to `Metroid 1 / 100` (`docs/shots/3ds-panel-real-pickup-azahar.png`, state line in the `.txt`). Game RAM was read, no flag was poked. The earlier failures were a wrong Azahar button map: jump is 304, shoot 307, item cancel 308 (not shoot), Select 314. Real 3DS hardware is still untested. Lemuroid savestates cannot be loaded by snes9x_3ds, so the Zelda side was not driven on the 3DS (its decoder is the shared C code verified on the Thor).
- **Dark rooms in play (Thor).** Room header byte 0 bit 0 is the dark flag (Sanctuary header `0xC1` is dark in play, `0xC0` lit; shots `docs/shots/dark-room-flag-set-in-game.png` and `dark-room-flag-clear-in-game.png`). WRAM `$0458` is not the indicator. `gen_rom ... dark=0|1` differs only by 85 header bit-0 clears plus the checksum, and no flagged room is left.
- **Indoor position.** `smz3_zelda_room_location(room)` (core, tested in `test_state_zelda.c`) maps the current room (WRAM `0xA0`) to the first Zelda chest location with that room id; `Smz3Discovery` places the player dot on that marker when indoors outside known dungeons, else on the last overworld position. Checked in Link's House: the dot sits on its marker.
- **Autosave.** Home/background already saved; a 60 s periodic save (`BaseGameScreenViewModel.PERIODIC_SAVE_MS`) now covers a force-stop mid-game. Verified by the `GameViewModelSaves.write` log while playing and by Continue after force-stop.

## Final state (2026-10-20)

Clean pass on the final commit: test-fill 10/10, test-patch 10/10, test-modes 40/40 (1 seed unfillable in both), test-hints 80/80, test-plando 60/60, test_ui and test_host pass; both vendor patches match the vendor trees; `./build-android.sh` and `./build-3ds.sh` build. Verified on the Thor: modes/items, plando, jingle row, real pickup detection on both games (Metroid and Zelda), persistence after force-stop, HUD sheet and fallbacks. Verified in Azahar (Thor): the 3DS plando picker, a Spazer Hunt run and the jingle row end to end, and the panel counters (injected flags, and a real Metroid pickup read from game memory, 1/100). Left for the owner: testing on a real 3DS, and supplying the official HUD graphics as `/sdcard/SMZ3/hud/hud.png`. Known limits are listed in the sections above (numeric amounts, per-location preferences, LiftOffOnGoalCompletion, multi-line YAML scalars, merchants' random Yes/No).

## Portal freeze, controls, hotkeys, QoL tab, widescreen (2026-10-20, evening)

- **Portal freeze = wrong base ROM.** The user's `alttp_usa.sfc` has MD5 `608c22b8...`; the SMZ3 patches need ALttP Japanese v1.0 (`03a63945398191337e896e5771f77173`). With the other revision the IPS patch at 0x4DFD02 (`JSL $B8B7AF` + 8 NOPs) misaligns, the CPU reaches `BRK` and loops in SM's handler at `80:8573`. Reproduced identically on the Thor and in Azahar. Both frontends now check the MD5 of both base ROMs (`core/gen/smz3_md5.c`, `test_md5`) and refuse the wrong one with a message. Runs made with the wrong ROM must be created again with the right one. **Not verified with the right ROM: none is available here.**
- **Controls.** The Odin controller reports the physical north button as Android X (BTN_NORTH, 307). Lemuroid's Xbox-style X/Y swap put it on SNES Y. `LemuroidInputDeviceGamePad` skips the X/Y swap when the device name contains "Odin" (A/B swap stays). Reasoned from the key layout; the physical button was not pressed in a test.
- **Bottom screen idle.** `Smz3IdlePresentation` (starfield, title, "Waiting for a game...") shows on the secondary display while `MainActivity` is resumed. Verified on the Thor (docs/shots/bottom-idle-screen-thor.png).
- **RUN tab.** The HUD is gone; the tab shows completion %, time, per-game bars, checks/bosses/items counts, pace (checks per hour), the three regions nearest to done, recent checks and the mini map. Tapping the mini map opens the MAP tab fitted to the Super Metroid area Samus is in (`Smz3Discovery.areaBox`) or to the whole ALttP strip. Verified on the Thor for the ALttP case (docs/shots/run-tab-redesign-thor.png, run-tap-big-map-thor.png); the Super Metroid area box is not checked on a device.
- **Dark World markers.** All 316 markers exist in `map_markers.json`. The checks that are not in logic were drawn as `#80808080` dots, invisible on the brown Dark World; they now have a dark fill and a light rim. Seen on the Thor.
- **Hotkeys and CONFIG tab** (`Smz3Hotkeys`, tracker CONFIG tab). Combos of one or two buttons: save state L2+Start, load state L2+Select, fast forward R2 (toggle, x4 by default, x2/3/4/6/8), next save slot L2+R1; active slot 1-4 uses Lemuroid's slot states. Tapping a button cycles it. Verified on the Thor with injected key events: slot file written, "Fast forward x4" toast. Load state was injected too but its effect was not inspected.
- **QoL tab in Create run.** New level between Advanced and Items: Light dark rooms, Wall jump logic, and the easier-logic toggles (no screw attack soft lock, no five power bomb seed, left sand pit, launch pad, waterway, east Crateria sky item, blue Brinstar top, Kholdstare, Zora, quarter magic, no low-resource Dark World). The tricks that make logic harder stay in Advanced. `test_ui` covers it. These are logic settings, not changes to how the games play (no easier wall jump mechanic exists in the patches).
- **Screen shape.** Simple tab option: Original 4:3, Wide 16:9, Square pixels 8:7, stored in the run JSON (`screenShape`, not part of the ROM name). It stretches the picture; it does not show more of the game. Android: new `GLRetroView.aspectRatioOverride` (LibretroDroid patch) applied at game start and switchable live in the CONFIG tab; seen on the Thor (4:3 pillarbox vs full 16:9). 3DS: sets the Scaling (4:3 / 8:7 / fullscreen 400x240) when a run continues; compiled, not run in Azahar.
- Option indices moved (UIO_SHAPE = 11, UIO_HELP 12, UIO_HELPS 13, UIO_PLANDO 18, UIO_JINGLE 19); checked by static asserts.

### Verification on the Thor and Azahar (2026-10-20, night)

- **Test ROM.** `rom/alttp_jp_v1.sfc` is the correct ALttP (JP v1.0, MD5 `03a63945...`, gitignored); `rom/alttp_usa.sfc` is the wrong revision and must not be used for portal tests. Runs 2-4 on the Thor were generated on-device with the JP ROM.
- **Portal (Thor, JP ROM): works.** SM Parlor door at (475,912): shoot it open, hold right 3 s, land in Zelda room 0x122 at (2648,3256). Link moves; walking up into the cave door returns to SM. Three full round trips without a freeze. Observation: after each return, 1-2 tracker samples show room=a59f x=18 y=1163 (stale Kraid pointer) before the correct 0x92FD; the final state is right.
- **Load state / next slot (Thor): proven** with the logged NMI frame counter: saving slot 3 at nmi ~1170, loading later jumps back to ~1214; the slot cycle goes 3, 4, 1, 2; loading slot 2 gives nmi ~610 (when it was saved). Hotkeys L2+Start, L2+Select, R2 (x4 toast) and L2+R1 all fired.
- **North button (Thor): proven in the core.** evdev 307 reaches the core as BUTTON_X and SM pad word `$8B` = 0x0040 (SNES X); 308 gives Y (0x4000), 304 gives B (0x8000), 305 gives A (0x0080).
- **SM area map (Thor): proven.** Tapping the mini map with Samus in Crateria opens the MAP tab fitted to Crateria.
- **Checklist (Thor): done.** Sprite grid (518 entries, paging, empty cell for a truncated `.pv` and for an empty one, no crash), Helps row only in Limited helps, persistence of runs and states after relaunching.
- **Azahar widescreen: geometry verified.** The three shapes render at different widths on the top screen (original 4:3 box, pixel slightly narrower, wide fills the 16:9 screen): `docs/shots/azahar-3ds-top-shape-{original,pixel,wide}.png`. The game picture itself is garbled in Azahar (known, see the emulator investigation), so the content of those shots is not meaningful.
- **NOT verified:** the portal and the sprite grid/Helps repeat on the 3DS (Azahar garbles the picture and the build writes no state file, so there is nothing to observe), the zora sprite in play, real 3DS hardware.
- **Debug logs kept** in the Lemuroid tracker (`SMZ3 key`, pad word, SM game state, the `SMZ3 game=...` line, `SMZ3 hotkey`); they are cheap and `tools/thor-input.py` parses the `game=` line. `tools/thor-route-portal.py` drives the SM route to the portal.

### Follow-up checks (2026-10-20, late night)

- **`test_host` and `test_preview`** pass on the commit `b690959` (host path identical, jingle row reaches the ROM; 628 previews parsed).
- **Wrong room after the portal: fixed.** After Zelda to SM, the SM room pointer `$79B` holds the Kraid room (`a59f`) for 1-3 samples while the game is not yet in normal gameplay. It was worse than a log oddity: `Smz3Discovery.updateMetroid` added the stale room to the discovered set (run 3's `slot2.disc.json` contained 42399 = 0xa59f, the Kraid room, never visited) and moved the map position. Now a sample counts only when `$0998` = 8 (normal gameplay) and the same room appears in two consecutive samples. Verified on the Thor: with 42399 removed from the disc file, three more Zelda/SM round trips still logged `room=a59f` raw 3 times, no `SMZ3 SM room discovered a59f` line, and the file stayed clean after the 60 s autosave. No unit test: the Lemuroid module has no test source set or JUnit dependency, and the fix is checked against the device log.
- **Screen shape tip** now fits ("Wide stretches the picture to 16:9."): `docs/shots/screen-shape-tip-thor.png`.
- **Zora sprite in play** (run 4, Link sprite zora.2): visible in the Zelda doorway and walking after the portal: `docs/shots/zora-sprite-in-play-thor.png`, `zora-sprite-walking-thor.png`.
- **Azahar garbled picture: not solved.** Every ROM (SMZ3 runs, plain Super Metroid, Mario World, the title screen of the upstream build) shows the same streaked output. Tried: Vulkan with hardware shaders off (the default here), OpenGL ES, software renderer (also stalls at 0 FPS), OpenGL with hardware shaders + accurate multiplication, Vulkan with hardware shaders + GLSL shader gen + native resolution + no texture filter + async presentation, Azahar 2126.2, 2127.0-alpha3 and 2120.3 (key injection dead). The menu bottom screen and the top menu render fine, so the fault is in how the game's framebuffer texture is emulated (snes9x_3ds shrinks 1024x1024 texture dimensions to force a 512 stride and moves the framebuffer address). Without a Citra <= 2104 build or real hardware there is no way to get a valid picture, so the portal, the sprite grid and the Helps row stay unverified on the 3DS; the same ROM bytes (identical to the x86 generator's output) and the same snes9x core are what ran correctly on the Thor.
- A test slot on Azahar (slot 4) disappeared while I was driving the menu blind with the pad; it was a throwaway run.

### Closing the gaps that do not need the Azahar picture (2026-10-20, night)

- **Room filter is a pure function with unit tests.** `Smz3RoomFilter.accept(gameState, room)` (state `$0998` = 8 and the same room in two consecutive samples) is used by `Smz3Discovery`. `lemuroid-app/src/test/.../Smz3RoomFilterTest.kt` (JUnit 4.13.2, new `testImplementation`; run with the cached JDK 21: `JAVA_HOME=~/.gradle/jdks/eclipse_adoptium-21-amd64-linux.2 ./gradlew :lemuroid-app:testFreeBundleDebugUnitTest --offline`) has 5 tests, all passing: the logged portal-return sequence with 1, 2 and 3 stale Kraid samples then the Parlor room (accepted only as Parlor), a stale room seen once in normal gameplay, a legitimate room change (accepted on its second sample), nothing accepted outside state 8, and the count reset after leaving gameplay.
- **Portal on the 3DS, verified by reading memory instead of the picture.** `sdmc:/SMZ3/debug_panel.txt` makes the 3dsx write `debug_state.txt` (SM room, Samus x/y, Zelda module/room/x/y). A fresh run created on Azahar with the JP ROM (seed 478518996), started with the pad, routed with `tools/azahar-input.py` plus the same nudges as on the Thor: SM room 0x92FD at (475, 912), shoot the door, hold right: Zelda room 290 (0x122) at (2648, 3275); hold up: back to SM 0x92FD (469, 918). Three attempts: two full round trips, one where the door did not open, as on the Thor. The values equal the Thor's. The bottom-screen panel stays legible while the top picture is garbled.
- **Sprite grid and Helps row on the 3DS: verified with captures** (the bottom screen is drawn by the 3dsx and is not corrupted): `docs/shots/azahar-3ds-sprite-grid.png` (1/518 .. 4/518, 15 per page, Default first), `docs/shots/azahar-3ds-helps-row.png` ("Limited helps" shows the extra "Helps available 3" row; "All helps" hides it). The truncated-`.pv` empty cell was not re-checked on the 3DS.
- **Old Citra build: not available.** The Azahar/Lime3DS GitHub releases start at 2124-rc2 for Android with listed APKs (2120.3 and 2127-alpha3 were tried before), the repository `PabloMK7/citra` does not exist, and `citra-emu/citra-nightly` / `citra-canary` publish no releases. A Citra <= 2104 binary is therefore not obtainable here (a desktop emulator on the PC is not allowed either). Left as a block that needs real 3DS hardware or an older Citra build supplied by the user.
- **Data touched while testing, for the record:** run 1 on the Thor was deleted to reach the Create screen (it was the old frozen test run with the wrong ALttP ROM); Azahar's slot 4 (a throwaway run) was lost while driving the menu blind. A new Azahar run 4 (seed 478518996) now exists. From now on: when a free slot is needed, move the `.run` file aside, test, and move it back (done once for the 3DS grid check; restored and compared with `cmp`). The Azahar `runs` directory is also copied to `/sdcard/SMZ3backup` on the Thor.

### Door, truncated sprite on the 3DS, playtime, review of what is unproven (2026-10-20, night)

- **The portal door that "did not open" is normal game behaviour, not a patch or ROM fault.** The tracker log now also prints `pose` ($0A1C), `mv` ($0A1E) and `shots` ($0CCE). After a Zelda to SM return Samus stands at (469, 918) with pose 0x0002 (standing, facing LEFT), so the script's shot goes away from the door and the following "hold right" only turns her and walks 6 px (pose 0x89, running right). The first trip starts at (475, 912) with pose 0x0001 (facing right), so it works; that is why it was trip 1 and 3 of 3 on both platforms. Fix is in the input script: `tools/thor-input.py portal_trip()` taps right first. 4 trips in a row on the Thor: Zelda room 0x122 and back to SM 0x92FD each time. The 3DS was not re-run with the fix; its pattern (trips 1 and 3 ok, 2 failed) is the same, but pose is not in its debug state, so the same cause is inferred, not measured there.
- **Truncated `.pv` on the 3DS: verified.** `adventure-2600.1.pv` cut to 100 bytes shows an empty cell in the grid (entry 4/518), no crash (`docs/shots/azahar-3ds-truncated-pv-empty-cell.png`). The original `.pv` and the `.run` were restored and compared with `cmp`.
- **Link sprite in play: verified** (zora.2, `docs/shots/zora-sprite-*-thor.png`).
- **Playtime while backgrounded: verified on the Thor.** Run 3 played about 65 s: `playTimeMs` 843000 to 904353. Sent to the background (HOME) for 100 s: 909664, then unchanged after `force-stop`. Only the few seconds before the app paused were added.
- **Desktop Citra/Lime3DS builds were not tried.** Running an emulator on the PC is against the owner's standing rule (tests only on the Thor), so that part of the request stays undone; the Azahar APKs on the Thor are the only emulator used.
- **Still without a real proof, after reviewing the whole file:** real 3DS hardware (menu timing and memory, picture, `.srm`, hints from the pause menu); the picture itself in any emulator (Azahar garbles it); Zelda pickups and the game-to-game transition counters on the 3DS (the portal itself is proven through RAM); official HUD graphics; syncing with a real RomM client; logic reachability with dungeon rewards, defeated bosses and keysanity keys in the Android tracker; the release build with its own name (parked).

### Logic inputs of the tracker, keysanity, 3DS pose and Zelda counters (2026-10-20, night)

- **Bug found: the tracker's logic never received the items the logic assumes.** `smz_reachable` was called with only the inventory read from memory. `core/gen/test_reach.c` showed that with every item of a seed only 242 of 316 checks were in logic: the Super Metroid keycards (assumed owned when Metroid keysanity is off), the Zelda keys (assumed when Zelda keysanity is off, as the Cas' tracker does with `assumeKeys`) and the seed's start items were missing. New `smz_assumed_items()` (core/gen/smz3_generate.c) returns exactly those; the JNI `reachable` adds them. After the fix every seed gives 316 of 316 with its own items.
- **Keysanity keys and cards are now read from the game.** `RunKeys.kt` (Android, `lemuroid-app/.../feature/runs`): small keys `$7EF4E0..ED` (WRAM `0xF4E0+` while in ALttP, SRAM `0x3F50+` while in Super Metroid), big keys `$7EF366/7`, keycards `$7ED830/1` (WRAM in SM, SRAM `0x3970` in ALttP); addresses taken from the vendored Cas' tracker (`ZeldaMiscData.cs`, `MetroidBosses.cs`). Only read when the run's keysanity includes that game; the tracker sends them with the inventory. Dungeon rewards (pendants, crystals) and Metroid bosses need nothing: the logic derives them from the items (`smz_reachable_prog`).
- **Tests.** `test_reach` (6 seeds x keysanity None/Zelda/Metroid/Both: completeness, keys and cards matter only in keysanity, small empty-inventory set, at least 10 rewards and the 4 Metroid bosses derived with every item, none with nothing, monotonic). JUnit (13 tests, run with the cached JDK 21): `RunKeysTest` (synthetic RAM: keys while in each game, big keys, keycards from both sides, keysanity of one game not reading the other, 0xFF ignored), `RunLogicItemsTest` (levels become repeated item types, absent items skipped), `Smz3RoomFilterTest`.
- **On the Thor:** a keysanity "Both" run was created (slot 1 `Run 1`, 0:00) and the log shows `SMZ3 logic keysanity=Both items=0 keys=0 inLogic=37` against `keysanity=None ... inLogic=40` on run 3 with an empty inventory. No real key or keycard was picked up, so the key addresses are backed by the Cas' tracker source and the synthetic-RAM tests, not by a measurement on the device.
- **3DS pose measured.** `debug_state.txt` now has 14 columns (the last two: Samus pose `$0A1C` and total checks). After a Zelda to SM return Samus stands at (469, 918) with pose 2 (standing, facing left): 4 trips in a row with the right-tap first worked (Zelda room 290 and back to SM 0x92FD each time), so the cause of the door that did not open is measured on both platforms.
- **Zelda counters on the 3DS across the portal: checked with a poked bit, not a real chest.** A chest bit was set once with `debug_poke.txt` (`s 208 10`, Link's House word `0x7EF208` bit 4, in SRAM while Super Metroid runs) and the file removed afterwards: the panel and `debug_state` went `Zelda 0/216` to `Zelda 1/216` (`zelda_cleared`=1, total 1), and over 3 round trips SM to Zelda (room 290) and back the counter stayed 1 in both games (read from WRAM in Zelda, from SRAM in SM). A real chest was attempted: Link was killed by soldiers outside the portal cave, continued from the death prompt into Link's House (room 0x104) and a blind sweep of the whole interior pressing the action button facing up found no chest, so the real Zelda pickup on the 3DS stays NOT VERIFIED (it was verified on the Thor).
- **RomM sync: no mock was written.** The project has no RomM client or sync code (only the file naming and hash design in "Native saves and savestates"); a mock server would only test the mock. It stays as a design with stable names, and needs a real RomM server plus a client to be proven.
- **Keysanity tip text** shortened ("Shuffles keys, maps, compasses and keycards.") because it overflowed into the Create button on the Thor: `docs/shots/keysanity-tip-thor.png`.
- **External blocks (need real hardware, a real server or official assets):** real 3DS hardware (timings, memory, picture, `.srm`), a valid emulator picture for snes9x_3ds, official HUD graphics, a real RomM server and client, the release build with its own name (parked). The Thor's Run 1 was deleted earlier and Azahar's slot 4 lost earlier; both recorded above.

### Nerfed charge: an option, off by default like the web (2026-10-21)

- **Finding.** Runs from the app let Samus charge the beam without the Charge Beam item; a game made on the web did not. `nerfed_charge.ips` ("charge beam is available from the start, with nerfed damage", damage divided by 3) was always in `SMZ_DEFAULT_CAS_IPS`. The C# generator has it as `CasPatches.NerfedCharge` (default true in the source), but the web ROM the owner played (`rom/smz3_test_popoie_fang.sfc`) has the vanilla bytes at `$90:B81E` (`89 00 10 D0 0A`), the app's runs have the patched ones (`89 00 00 80 0A`). The web ran with it off, so off is the app's default now.
- **Change.** `SmzPatchCfg.nerfed_charge` (0 after `smz_patch_cfg_defaults`); `build_rom` skips `nerfed_charge.ips` unless it is set. New QoL row "Nerfed charge" ("Charge the beam from the start, at a third of the damage.") in the shared menu (`O_NERF`, `UIO_NERF` = 22), stored in the run JSON as `nerfedCharge` and passed to the generator by the 3DS host, by the JNI (`params[11]`) and by `RunGenerator.nativeParams`. `gen_rom` takes `nerf=1`. Runs made before this keep the ROM they were generated with.
- **Tests.** `test_host` now also checks that the default menu ROM has the vanilla bytes at `0x10B81E` and that turning the row on gives the patched bytes and `"nerfedCharge":true` in the JSON; `test_ui` prints the new key; `test-patch` 10/10, `test-modes` 20/20 and `test-plando` 10/10 identical to the C# generator (they compare the patch classes, not the IPS stage).
- **On the Thor: verified.** The new APK shows "Nerfed charge" in the QoL tab (Off by default; the tip fits above Create). A default run made in the app has the vanilla bytes at `$90:B81E` and `"nerfedCharge":false`; in play Samus fires a plain beam when the shoot button is held for 2.5 s (no charge glow). A run created with the row On has the patched bytes (`89 00 00 80 0A`) and `"nerfedCharge":true`, and the same hold shows the charge glow on the arm cannon. Both ROMs were read from the app's private `files/runs`. For the test, Slot 1 was moved aside (`.aside`, backup also in `/sdcard/SMZ3backup/runs_slot0`), then restored: its three files are byte-identical to the backup (`cmp`) and `active` is back to 3. The two test runs and their ROMs were removed. Not checked on the 3DS (the C test and the build cover it).

### The app is called SMZ3, sharper map, release build (2026-10-21)

- **Own app.** `applicationId` is `com.utsugi.smz3` (debug: `.debug`), label "SMZ3" ("SMZ3 debug"), new launcher icon (gold triangle and "SMZ3" on dark blue; `tools/gen_app_icon.py` writes the legacy mipmaps, the adaptive foreground and the monochrome layer), "Lemuroid" replaced by "SMZ3" in the strings of every locale. Kotlin packages and the `namespace` are unchanged. It installs beside the old app; its data is separate (runs are not migrated; `/sdcard/SMZ3/saves` is shared). Notifications must be allowed once (`pm grant ... POST_NOTIFICATIONS`), otherwise "Continue" only shows the toast "Please enable notifications from Home to continue". The Lemuroid credit is in the README (GPL-3.0 fork).
- **Sharper map.** `map.webp` (half size, lossy q85, Lanczos) is now `map.png`, full size and lossless (3 MB), drawn into the same half-size coordinate space with mipmaps (`Smz3MapView.MAP_W/MAP_H`); markers, rooms and the player position did not change. Checked on the Thor: the Super Metroid lines are clean and the markers sit on the same rooms.
- **Release build.** `./build-android.sh release` makes `build/smz3-release.apk` (R8 on, signed with a generated local key `vendor/Lemuroid/release.jks`, git-ignored; packaging needs `-Xmx8g`, with the default 2.5 GB it died with OutOfMemory). A `-keep` rule for `Smz3Native` keeps the JNI names. Installed on the Thor: it creates a run (generator through JNI), starts the game and the tracker shows "Super Metroid", the clock and Samus's position, so R8 did not break the native calls. Keep a copy of `release.jks`: apps signed with a different key cannot update it.
- **3DS: nerfed charge verified in Azahar.** New 3dsx pushed to `sdmc:/snes9x_smz3.3dsx` (old one at `/sdcard/SMZ3backup/azahar_nerf`). A default run made from the 3DS menu has the vanilla bytes at `$90:B81E` and `"nerfedCharge":false`; one made with QoL > "Nerfed charge: On" has `89 00 00 80 0A` and `true`. The ROM names (`..._14757a80`, `..._ce263471`) equal the Android ones for the same settings. Two slots were moved aside for this and restored: the four `.run` files are identical to the backup (`cmp`).
- **Not done:** a real Zelda chest on the 3DS and a real key pickup in keysanity (the portal and the counters were already verified through RAM and a poked bit).
- **Slim app (2026-10-21, later).** The release APK went from 190 MB to 15 MB: `lemuroid-app/build.gradle.kts` keeps only arm64-v8a (`ndk.abiFilters`) and excludes every libretro core but snes9x (`packagingOptions.jniLibs.excludes`; the other cores were 90 % of the package, across four ABIs). The nav graph in `MainActivity.kt` now has only the runs menu (the Lemuroid library, search, systems and settings screens were already unreachable and are gone). Checked on the Thor with a fresh install of the release APK: create a run, Continue, intro, Samus on the ship, tracker shows "Super Metroid" with the clock and her position. Left as is: `assets/libretro-db.sqlite` (12 MB uncompressed, ROM-scan metadata; not removed to avoid breaking the game registry) and the internal Kotlin package names `com.swordfish.lemuroid.*` (a rename would touch the whole fork and its patch; nothing visible uses them).
- **Package moved to Utsugi (2026-10-21).** The project will live under the `doctor-utsugi` account: `applicationId` is `com.utsugi.smz3` (debug `.debug`), and the local release key is generated with `O=utsugi` (the old key and the earlier test installs were removed from the Thor; they held no runs). Verified on the Thor with a fresh install of the release APK: create a run, Continue, tracker shows "Super Metroid" and the clock. Not pushed, and the repo is not transferred yet (transfer: see the `doctor-utsugi` notes, `POST /repos/{owner}/{repo}/transfer`, and set the local identity to `doctor-utsugi`). The generator is not a web service: the app generates on the device with the C core; `tools/gen-cli` (C#) is only the test oracle and the dev service on localhost:8765, and nothing of it is published on any website.

- **Thor round (2026-10-21).** Bottom screen at start: the "Waiting for a game" text is gone and two leaderboards (default rules and custom rules, top 5 each, by play time) are drawn instead; a run is added when the credits are reached (`SMZ3_GAME_CREDITS`, stored in `runs/leaderboard.json`, `Run.completedAt`), and the rule set comes from the new `defaultRules` flag in the run's settings JSON (`ui_is_default`, which ignores seed, sprites, screen and map view). New QoL option "Save states" (`saveStates`, off by default): with it off the hotkeys, the quick save/load and the game menu slots do nothing and a note says so. Fast-forward is now held (R2 by default) instead of toggled, and the play time and the clock advance by the speed factor (verified: 39 s of clock in ~12 s real at x4). Messages are a small white text at the bottom right instead of toasts. Odin buttons are straight (Thor B is the console's B; the old A/B swap is gone) and Super Metroid's own controller settings start as shot Y, jump B, dash A, item cancel X: the generator rewrites the immediates of the four code sites that load the default bindings (`sm_default_controls` in `smz3_generate.c`; set by `ui_settings` and the JNI, off in `gen_rom` so the C# oracle stays byte-identical). This replaced an input-level remap. Verified on the Thor with a new run: B jumps, Y fires, A runs; item cancel (X) not exercised; runs created before this keep the stock layout. The map's player dot blinks and a [CENTER] button recentres it. The chosen Link/Samus sprite shows beside its row in the Create run list. Not yet done: the bosses/crystals display on the RUN tab (mock-up shown, waiting for approval).

- **RUN tab redesign (2026-10-21).** Info on the left (46%), the following mini map on the right (tap opens the big map; the [CENTER] button is only on the big one). One line with the four Super Metroid boss tokens (lit when beaten, event word `$7ED828`) and the crystals 1 to 7, numbered, blue for 1-4 and 7 and red for 5 and 6, lit from the crystal byte `$7EF37A` (bit 0 is crystal 6, then 1, 5, 7, 2, 4, 3). Both are read from the work RAM when their game is running and from the save RAM when the other one is (Zelda block at `$A06000`, Super Metroid event word at `$A16078`). Below: "RECENT ITEMS" with the item icon and name of the last five pickups (inventory increases; the first read after starting is the baseline) and "NEARLY DONE". Verified on the Thor: layout, tokens dimmed, no crystals lit on a new run. Not verified: the crystal bit order (from memory of the ALttP RAM map), the beaten-boss bits while Zelda runs, and the recent items list filling up on a real pickup.

- **Fix (2026-10-21):** the RUN tab's recent item names were drawn black on the green Zelda theme (the labels were created after the theme was applied); they are now white with a dark shadow. Not checked in a Zelda pickup, only built.

- **Detection, items grid, markers, quick morph (2026-10-24).**
  - **"Waiting for a file" in play.** `smz3_started` read `$7E0020`, which is Link's Y position (low byte) in ALttP and something unrelated in Super Metroid, so it dropped to 0 whenever that byte was 0; the tracker then showed the idle text and, because discovery runs only while `started`, skipped map discovery for those samples (the "touched a map but it is not discovered" report). It now takes the SRAM and picks the signal by game: ALttP module `$7E0010` >= 6, Super Metroid game state `$7E0998` >= 6, credits always started. The tracker also holds the last live summary for 2 s so one odd frame cannot flash the idle text. Verified on the Thor in Super Metroid (shows "Super Metroid", the clock runs); the ALttP branch is covered by `test_state_zelda` only, not seen on the device.
  - **ITEMS tab.** 11 columns of 44 dp cells, all 54 items fit on the Thor's bottom screen without scrolling (checked).
  - **Map markers.** Tap an empty spot on the big map: an overlay with the item grid (Zelda and Super Metroid, filter field, optional description) places an item icon there; tap a marker to read it and delete it. Stored per run in `runs/slot<N>.marks.json` (`Smz3Marks`), removed with the run, drawn on the big map and on the RUN mini map (checked on the Thor, keyboard included: the overlay resizes above the soft keyboard).
  - **Quick morph.** QoL row "Quick morph" (off by default, `quickMorph` in the run JSON, `params[12]` in the JNI, `UIO_MORPH` = 24) applies `UnifiedAim.ips`: the aim-down button morphs and aim up/down share one button. Not part of `defaultRules`. `test_host` checks the ROM changes. On the Thor the patched ROM boots and plays; the morph itself was not exercised (a new run starts without the Morph Ball).
  - **Random music (not built).** The generator has no music option: SMZ3 music comes from MSU-1 packs (`msu1-v6.ips` plus `<rom>-N.pcm` tracks), and the libretro snes9x in the app has MSU-1. A random-SPC option would need the library rendered to PCM (per run, large) and does not exist on the 3DS port; proposal pending.

- **Random music (2026-10-24).** QoL row "Random music" (`randomMusic` in the run JSON, `UIO_MUSIC` = 25, not part of `defaultRules`). `tools/music-bank.py` renders random `.spc` files of a library (ffmpeg with libgme; tracks of 45 s or more, up to 150 s, 3 s fade) to MSU-1 PCM (`MSU1` + loop point 0 + 44.1 kHz 16-bit stereo); 119 tracks (2.1 GB) were rendered on the library `snes spc osts` and pushed to `/sdcard/SMZ3/music`. On Continue, `Smz3Music.prepare` writes `<rom>.msu` and symlinks `<rom>-N.pcm` for the 47 looping SMZ3 slots (A Link to the Past 2,4,5,7,... and Super Metroid 103-127; numbering from MSURandomizer's `snes/z3m3` type) to random bank tracks chosen by the seed. The base ROM already has the MSU-1 code (`LDA $2002 / CMP #'S'` at `0xF900`), so no extra patch is applied. **Not working yet:** the core logs `ROM+RAM+BAT+MSU-1` when the `.msu` exists, but on the Thor (debug build, `run-as`) the `:game` process never opens any `.pcm` (no fds, no PCM-rate reads) while walking rooms in Super Metroid, even with links for every track 1-200. The cause is not found; nothing was heard on the device.

- **Random music works (2026-10-24).** The earlier "not working" note was a wrong lead: the emulator runs in the `:game` process and I was inspecting the main one. Confirmed working on the Thor by the owner.

- **Runs list, ladders, music packages (2026-10-24).**
  - **Runs list.** Up to 32 runs (`UI_SLOTS`, `RunStore.SLOTS`), 4 rows shown, chevrons and an `(n/32)` title, L/R page by 4. The 3DS keeps 4 (`slot_count` defaults to 4; Android calls `uiSetSlotCount`). Used runs have an orange bar and bright text; empty ones show "- empty -" and "+ New run (A)". Delete goes through a `UI_CONFIRM` screen (Cancel is the default).
  - **Bottom screen.** `Smz3MenuSelection` mirrors the highlighted run. The idle screen shows DEFAULT RUN or CUSTOM RUN and the options that differ (`diff` array written by `ui_settings_json`, which leaves out cosmetic options and the helps count), plus the music package. Runs created before this show "No option list stored for this run". With nothing selected it shows the default top 3 and a SHOW LADDERS button for the full ladders (top 5 each) and BACK.
  - **Music package.** QoL option "Music package" (`musicPackage`: "" original, "*" random, else a folder of `/sdcard/SMZ3/music/<name>/`). Numbered `*-N.pcm` files replace track N; otherwise the pool is dealt randomly over the 47 looping slots. Replaces the "Random music" toggle.
  - **ITEMS and map.** One 9-column items grid sized to the screen; bigger markers; a 650 ms ring at the tapped spot before the marker picker opens.
  - **Verified on the Thor:** list scroll and chevrons, used/empty look, run info for old and new runs, package picker, items grid, markers, delete confirm screen (Cancel only), SHOW LADDERS and BACK. **Not verified:** a real delete, music playback with a chosen package, the ring animation by eye, Zelda detection in play, Quick morph in play, the 3DS build with the new shared UI.

- **Add-on music pack "SNES greatest hits" (2026-10-24).** Music is never part of the release: packs are add-ons the player copies to `/sdcard/SMZ3/music/<name>/`. `tools/music-pack.py` renders a curated pack from a `.spc` library using a JSON map (`tools/music-packs/greatest-hits.json`: SMZ3 slot -> game folder + ID666 title, picked to match the scene, e.g. a Final Fantasy IV cave for the Zelda cave). Output is `<pack>-<slot>.pcm` for the 47 looping slots (about 1.0 GB at the 150 s cap); one-shot slots keep the original music. Rendered on a build machine, pushed to the Thor by hand. Not heard in play yet. Idea pending: a shared pool plus JSON maps so several packs reuse the same rendered tracks.

- **Timers kept on load (2026-10-24).** The RUN tab's clock restarted at 0 on every load because it showed only the time counted in the current session. It now shows the saved play time of the run plus what has accumulated since the last save (`baseMs + pendingPlayTime` in `Smz3Tracker`). Verified on the Thor: a run saved at 0:11:22 showed 0:11:41 about 20 s after loading. Not checked: a finished run (the clock may jitter after the credits).

- **Create run tabs regrouped (2026-10-24).** The old Simple/Advanced/QoL split mixed both games and general options (music sat under QoL next to logic tweaks). The tabs are now Run (seed, game mode, keysanity, item placement, helps, tracker map, plando), Zelda (crystals, pyramid hole, Link sprite, dark rooms and the Zelda logic tweaks), Metroid (Tourian, Samus sprite, jingle, wall jump, nerfed charge, quick morph, Spazer Hunt counts, the Metroid logic tweaks) and Play (screen filter and shape, save states, music package), plus Items unchanged. `UI_TABS` in `smz3_ui_opts.c` lists the keys of each tab in display order, so the option indices that `ui_settings` reads did not move and run files are unaffected. The Spazer counts only show in Spazer Hunt. The title is "New run" so the five tabs fit. Verified on the Thor (all four new tabs drawn); `test_ui` and `test_host` pass. The 3DS shares this screen and was not run on the console.

- **Map desync in east Crateria, recent items in two columns (2026-10-25).** In the combined map image the east of Crateria (map columns 46 and up: East Ocean, the rooms beside the Wrecked Ship entrance) is drawn seven columns to the right of the ROM's room coordinates, because the Wrecked Ship area sits where those rooms would be. The tracker used the ROM coordinates, so in the East Ocean the position dot, the discovered rectangle and the zoom box landed on the Wrecked Ship rooms. `Smz3Discovery.SmRoom.col` now adds the seven columns for area 0 rooms from column 46 (EAST_CRATERIA); checked by drawing the shifted rectangles over `map.png` (they follow the pink rooms) and on the Thor (the dot of the user's run in the East Ocean sits on its pink strip). Other areas were not compared one by one. RUN tab: the recent items list is two columns of up to eight (smaller icon and text, names ellipsized); not seen with items on screen yet (the list was empty after loading the run).

- **Boss indicators fixed (2026-10-25).** The four Metroid boss tokens and the map marks read the event word at `$7ED828` with bits 0x0100/0x0200/0x0400/0x0800, but the flags are one byte per area (offset from `$7ED828`): Kraid byte 1 flag 0x01, Ridley byte 2 flag 0x01, Phantoon byte 3 flag 0x01, Draygon byte 4 flag 0x01 (same table as the SMZ3 Cas tracker's `BossConfig`). Byte 1 flag 0x02 is Spore Spawn, so the Phantoon token lit up as soon as Spore Spawn died; Draygon and Ridley used wrong bits too. `Smz3Bosses` now reads the area bytes (work RAM, or the parked save RAM block at `+0x2078` while Zelda runs). Checked against the owner's save: Kraid's flag is set and so are "Energy Tank, Kraid" and "Varia Suit", so Kraid really is dead there. Built; **not installed or seen on the Thor yet** (the console was offline).

- **Recent lists kept per run, map markers redrawn (2026-10-25).** The RUN tab's recent items (and the recent checks list) lived only in memory and started empty every time a run was opened; they are now stored in `runs/slot<N>.recent.json` (`RunStore.recentFile`, removed with the run) and restored when the slot becomes active, written whenever they change. Map dots now look the same in both games and stand out on every area colour: pending checks are big amber dots with a black and a white rim, cleared ones small solid white dots with a black rim, checks out of logic dark dots with a white rim (the old cleared dot was a light green that vanished on the green Brinstar and the pending yellow on the yellow Wrecked Ship). The per-game theme used to recolour the dots (green for Zelda, orange for Metroid) and overrode these paints; `setTheme` no longer touches them. Seen on the Thor: amber pending dots with a white ring and white cleared dots read clearly on the blue, green, red and yellow Metroid areas. Not checked on the device: the restore of the recent items (the list was empty, no pickup made), and the Zelda map close up.

## Zelda special overworld areas on the map

`$8A` values of 0x80 and above (Master Sword grove 0x80, Zora's domain 0x81) were masked with `& 0x3F` and their local coordinates drawn as light-world coordinates, so Link appeared in the north-west forest and the wrong screen was marked as discovered. Those areas now sit on the marker of their item (Master Sword Pedestal, Zora's Ledge), other special areas keep the last overworld position, and none of them is added to the discovered screens. Verified on the Thor with a run standing in Zora's domain: the player ring is on the Zora marker in the north-east.

## Items counter after a restart

The Super Metroid items were read only while Metroid ran, so after reopening a run inside Zelda the cache was empty and "Items" dropped (29 to 17 on the owner's run). With Zelda current they are now read from the parked save-slot copy in SRAM (`$09A2` at `+0x2010`, so address + `0x166E`). Checked on the owner's state on the PC (29/54, same as before the restart) and on the Thor after reopening the run (29/54).

The overworld screen that leads into a special area (Lost Woods for the Master Sword grove, the north-east corner for Zora's domain) now counts as discovered, otherwise the player stood on a dimmed, undiscovered part of the map. Seen on the Thor: the area around Zora's domain is lit.

Large overworld areas (Lake Hylia, Death Mountain, Lost Woods...) are 2x2 screens named by `$8A` with their first screen only, so Link stood in a quadrant never marked as discovered. The screen under his coordinates (`y/512*8 + x/512`) is now marked too. Seen on the Thor in Lake Hylia: the quadrant under the player is lit; the other quadrants of that large area stay dim until visited. The map markers with a note also get a thicker pink rim (not yet seen with a note on the Thor).

ITEMS tab grid: the cell size no longer comes from display metrics minus a guessed chrome height; a small `ViewGroup` sizes square cells from the width and height the page really gets (9 columns, 6 rows). Built, **not installed or seen on the Thor yet** (console offline).

ITEMS grid seen on the Thor: all 54 cells (9 x 6) fit with a margin on every side, counts uncut. Recent items also confirmed on the owner's run: a Hi-Jump Boots pickup showed as the first entry.

## Dungeon maps on the bottom screen (2026-10-25)

Inside a Zelda dungeon the mini map and the MAP tab show the floor Link is on, drawn like the game's own pause-menu dungeon map (idea from the DS port alttp-ds, which draws that same map). `tools/zelda-dungeon-maps.py <US ALttP rom> <dir>` decodes it from the ROM: the 14 floor layouts (5x5 rooms per floor, `$8AF605`), the room shapes (`$8AFBE4`), the room piece table, the map graphics packs and the map palette (`$9BE544`), and writes `dungeon_maps.png` (rooms fully drawn), `dungeon_maps_hint.png` (the outline the map item reveals) and `dungeon_maps.json` into `lemuroid-app/src/main/assets/smz3/`. Those three files are generated from the user's ROM and are **not committed** (the patch pathspec excludes them); without them the app keeps the old behaviour. The JP 1.0 ROM stores its graphics packs elsewhere, so the tool is run on the US ROM (same pictures).

In the app (`Smz3DungeonMaps.kt`): the dungeon is `$040C / 2`, the floor `$A4` (signed, plus the floors below ground), the room `$A0` (with the game's three room aliases), Link's spot inside the room `($22 & 0x1E0) >> 5` and `($20 & 0x1E0) >> 5`, visited quadrants `$7EF000 + 2 * room` (low nibble) and the map item `$7EF368`. Each 8x8 quadrant shows the full drawing when visited, the outline with the dungeon's map item, nothing otherwise. Boss icon, stairs and the checks inside the dungeon are not drawn. **Built, not installed or seen on the Thor yet** (the console went offline during the install); no saved state has Link inside a dungeon, so only the generated atlas has been looked at.

Seen on the Thor with the owner's run inside a dungeon: the floor map shows ("B1", one room, Link blinking), and a chip at the top right of the map view switches to the full combined map ("FULL MAP") and back ("DUNGEON"); the choice resets when Link leaves the dungeon. The floor picture is drawn at the size of the view even when only one room is known, so early on it is small.

Dungeon view, second round (seen on the Thor inside Misery Mire, then Turtle Rock): with the run's map mode "all" every room is drawn fully (the visited / map-item logic only applies to "discovered" runs). The view is cropped to the part of the 5x5 grid the dungeon uses. A column of floor buttons on the left (top floor first, current one highlighted, "HERE" below them when another floor or dungeon is picked), "<" and ">" to flip through the 14 dungeons (opens on the ground floor), the title with the floor and "n/total". Rooms with unopened checks get the same dots as the world map: amber for in logic, dark with a light rim for out of logic, both side by side when a room has one of each (room number from the new JNI `zeldaLocationRoom`, so it is the room of the check, not the exact chest). The world map has a "DUNGEONS" chip that opens this view from outside (on Link's dungeon when he is in one, else the last one picked); inside, "FULL MAP" goes back to the world map. Taps on the dungeon view no longer reach the marker picker. Not drawn: the boss icon, stairs, and the exact chest position inside a room.

Boss icon in the dungeon view: a red "B" in the boss room, on the boss's floor, shown while the boss is alive (room flag `0x800`) and the compass is owned (`$7EF364`) or the run shows the whole map. Boss room, floor and spot per dungeon come from the game's own map routine (same tables as alttp-ds). Seen on the Thor in Misery Mire.

Chest positions in the dungeon view: `tools/zelda-dungeon-maps.py` also reads each room's object stream from the ROM (pointer table `$1F8000`, floor and layout bytes, three object layers, chests are the type 3 objects `0x19` and `0x31`) and writes the tile position of every chest into `dungeon_maps.json`. The `n`th chest of a room is the check with flag bit `4 + n` (JNI `zeldaLocationRoom` now returns the room plus the bit shifted by 16), so unopened chests are drawn where they stand (tile / 4 inside the 16 px room cell). Checks that are not chests (boss, key drops, rooms without a known chest) stay together at the room's centre. The "DUNGEONS" chip of the world map sits at the bottom left. Seen on the Thor in Misery Mire; the match between each dot and the real chest was not checked against the game screen.

RUN tab mini map: no map choosing there (that is the MAP tab's job). It shows the dungeon floor Link is on while he is inside one and the world map around him otherwise, with "+" and "-" buttons at the bottom right (x1.4 steps, 0.5 to 4, kept while the app runs; zoomed in, the dungeon picture follows Link). The floor list, "<" ">", "FULL MAP", "HERE" and "DUNGEONS" exist only on the MAP tab. On runs that show the whole map, rooms of a dungeon floor Link has not been in are drawn with the same picture at low opacity and visited quadrants at full strength; runs that show only what is discovered keep the map-item outline. Seen on the Thor in Misery Mire (mini map, "+" zoom, faint rooms); the world-map zoom was not tried.

Fixes seen on the Thor: the dungeon names were in the wrong order (the owner stood in Thieves' Town and the view said Misery Mire); the palace index `$040C / 2` follows the game's own order Sewers, Hyrule Castle, Eastern, Desert, Castle Tower, Swamp, Palace of Darkness, Misery Mire, Skull Woods, Ice, Tower of Hera, Thieves' Town, Turtle Rock, Ganon's Tower. The RUN tab is more compact so the title ("A Link to the Past", which shrinks to fit its line) and the "NEARLY DONE" block fit without scrolling, and tapping the mini map no longer jumps to the MAP tab.

## New-run flow: modes, saved customs, sprite step

Creating a run now starts at a mode picker (`UI_MODE`): **Chill run**, **Original**, **Custom** and **Saved customs** (only while at least one exists).

- Chill run: Zelda-only keysanity, wall jump Hard, all helps, discovered tracker map, light dark rooms, save states on. The preset is the `CHILL` spec in `core/ui/smz3_ui.c`.
- Original: every option at its default. Custom: the tabbed options panel; its last two rows are "Save as custom..." and "Next: sprites and start".
- Every mode ends in the sprite step (`UI_START`): Link and Samus rows (A opens the sprite grid, Right moves to the Random chip) and a Start button. The Link/Samus options left the Zelda and Metroid tabs; option indices are unchanged.
- Saved customs are stored as option-key specs (`Key=value,...`, only values that differ from the defaults), so new options never invalidate them. A first run installs nine well-known ones (keysanity, placements, All Dungeons, Spazer Hunt, quick finish, open doors, casual, hardcore); each can be deleted (Right, A, A). The list is persisted by the host as `name<TAB>spec` lines: Android `files/saved_customs.txt`, 3DS `sdmc:/SMZ3/saved_customs.txt` (`ui_saved_load` / `ui_saved_text` / `ui_saved_dirty`). An existing empty file means "no customs", it does not bring the built-ins back.
- Naming uses a d-pad keyboard (A-Z, 0-9, `-.'!`, Space, Delete, Save, Cancel; L deletes, R types a space), so nothing needs touch.

Verified: `core/ui/test_ui.c` (flow, presets, naming, persistence round trip, deletes), the Android release build on the Thor (mode picker, saved list, delete confirm, sprite step with grid and Random, naming screen, list surviving a reinstall) and a 3DS build. Not verified: a run generated through each mode on the device, and the 3DS menu on hardware.

## Run list rows

Each used slot in the run list shows a big total percent (3x5 pixel digits drawn as rectangles, so every frontend renders them the same), the run name and play time, a Link icon with the Zelda counter and a Samus icon with the Metroid counter, each with a thin progress bar, and the game the run is in. The hosts pass the numbers with `ui_set_slot_stats` after `ui_set_slot`; a slot without them keeps the old summary line. The palette gained `C_GREEN`; the Android canvas now snaps rectangle edges to whole pixels so the digit blocks meet without seams.

Verified on the Thor with the existing runs (Android). The 3DS build compiles; the rows were not looked at on a 3DS.

## Original Link and Samus pictures from the ROMs

The run list rows and the sprite screens draw the games' own standing Link and Samus instead of hand-made art. `tools/gen_original_icons.py <Super Metroid JU> <ALttP> <sprites dir>` reads the tiles and palettes from the ROMs and writes `link/Original.pv` and `samus/Original.pv` (the repository only holds tile numbers and positions, never pixels). The UI asks for the sprite key `link/Original` / `samus/Original` like any other sprite, so the cell for "Original" in the sprite grid shows the same pictures. Copy the two files next to the other previews (`/sdcard/SMZ3/sprites/{link,samus}` on the Thor, `sdmc:/SMZ3/sprites/{link,samus}` on the 3DS); without them those spots stay empty. The run list no longer shows the "in Zelda / in Metroid" tag, which crowded the counters.

## Run list: crystals and bosses instead of names and counters

The row no longer shows "Slot N Run N" nor the Zelda/Metroid counters. Link's line shows the seven crystals as drawn gems (blue, red for 5 and 6, dark while missing); Samus's line shows the four boss tokens (the tracker's own artwork from the app assets, dimmed while the boss lives). Two thin bars on the right keep the check progress, with the play time above them. The masks (`bm`/`cm` in the 3DS run file, `bossMask`/`crystalMask` in the Android run JSON) are written by the tracker every ten seconds while a run is played, so a run shows its gems and tokens after it has been opened once; older runs start empty. The 3DS run list reads the same fields but its tracker does not write them yet, and its boss tokens need the artwork as `reward/<boss>.pv` previews, so on the 3DS the tokens stay blank.

## Cozy run, run names, list refresh, empty slots

- The "Chill run" preset is now **Cozy run**: Keysanity Zelda only, wall jump Easy, light dark rooms and save states on, the rest default (all helps, whole tracker map). The generator has no option that shuffles only the Zelda dungeon chests; Zelda-only keysanity spreads that game's keys, maps and compasses over its whole world, and Super Metroid keeps its own rules. Seeds are always solvable.
- A new run is named after how it was set up ("Cozy run", "Original", "Custom" or the saved custom's name) via `ui_run_label`, and the row shows that name. Older runs keep their "Run N".
- The Android run list refreshes on every resume, so progress made in a run (crystals, bosses, time) shows when returning to the list.
- An empty slot shows a plus, "New run" instead of "Slot N - empty - / + New run (A)".

## Idle screen starfield loop

The waiting screen of the second display looped its stars with a visible jump: each star moved a fraction of the screen per loop and its twinkle did not complete whole cycles. Stars now cross the screen a whole number of times per 40 s loop, twinkle a whole number of cycles, and fade in and out at the top and bottom edges, so the restart is seamless.

## Online ladder (smz3.indieviral.com)

`server/ladder/` is a dependency-free Node service (pm2 `smz3-ladder`, local port 3360, Apache reverse proxy and TLS from the subdomain script). Data lives in `data/ladder.json`; the HMAC secret in `data/secret` (both ignored by git, never deploy over them).

- `POST /api/start {install, kind}` returns a signed token (and the month's seed for `monthly`). `POST /api/finish {token, timeMs, player}` is the only way to add a run. Unknown keys are refused.
- Checks on finish: valid signature, token not used before, `10 min <= timeMs <= wall clock time since the token was issued`, per-IP rate limits. One entry per install on each ladder (its best).
- Ladders: `original` (default rules, all time) and `monthly` (fixed seed derived from the month by the server, a new one every month; `?kind=monthly&month=YYYY-MM` shows past months). The page at `/` has a month selector.
- Limits, stated honestly: the time is still reported by the app. The token stops forged requests and times shorter than the real time elapsed, not a patient cheater or a modified app.
- App (`OnlineLadder.kt`): the first played tick of an original-rules run asks for a token (stored as `runs/slotN.token`); at the credits the time is queued and sent, retried on the next idle screen if offline. The bottom screen shows the fetched original ladder tagged ONLINE and keeps the local custom ladder. The player name is `runs/player_name.txt` (default `Player-xxxx`).
- Not done yet: the Monthly seed mode in the new-run menu (the server side and the seed endpoint exist).

## Player name and flag

The start screen of a new run (Android only, enabled by the host with `ui_player_enable`) has a Player row: the name opens the same d-pad keyboard as saved customs (`name_for = 1`), the flag chip opens a scrolling list of 41 countries (`UI_COUNTRY`, ISO codes, first entry "No flag"). The profile is one per device, stored by the host as `name<TAB>CC` in `runs/player.txt` (`ui_player_load/text/dirty`, JNI `uiPlayerLoad/Changed`) and sent with the finished run (`country` is optional on `/api/finish`). The web page and the bottom screen show it as a regional-indicator emoji flag before the name. It is set before starting, not at the credits, because the game process has no menu at that moment; it can be changed for the next run at any time.

### Online consent

Pressing Start on a new run asks once about the online ladder (`UI_ONLINE`): Yes (then name and flag if missing), Not now (offline this run, asked again next time) or No, don't ask again. The answer is `online_mode` (0 ask, 1 on, 2 off), the third field of `runs/player.txt`. The Player row of the start screen shows the state and reopens the question (when On it has name, flag and an On chip). Only runs generated while On get `runs/slotN.online`; the tracker asks the server for a token only for those, so Not now and Off runs never contact it.

### Monthly seed

`GET /api/monthly` gives the month and its fixed seed (9 digits, derived on the server from a secret and the month, so it cannot be known ahead). The app fetches it when the menu opens, keeps a copy for the same month (`runs/monthly.json`) and calls `ui_set_monthly`; only then the mode list shows "Monthly seed" (Original rules, `Seed` option set to that value, run named "Monthly YYYY-MM"). Without a connection and without a copy of this month the row does not appear (3DS never shows it). A monthly run marked online asks the server for a `monthly` token on its first played tick; the web page has a month selector for past ladders. Ladder per month keeps each install's best time.

### Online runs in the run list, and hardening

- Runs marked online (`runs/slotN.online`, written at generation) are drawn in the run list over a translucent gradient (teal to navy for Original rules, violet to gold for the monthly seed; draw command kind 3, the palette gained four colours) with the player's name and flag code at the right under the time. Android only; the 3DS never marks a slot online.
- Server: the client address is the last `X-Forwarded-For` entry (the one Apache appends), so the per-hour limits cannot be dodged by sending the header; the limit table is pruned; only the best time per installation and ladder is stored (growth is bounded by the number of installations); a token is valid for 3 days and a time cannot exceed that; security headers (CSP, nosniff, no-referrer). `admin.js` lists and removes entries (restart the service afterwards).
- App: the token request is retried every minute until the server answers, and a rejected finish writes the server's reason to `runs/last_online_error.txt`. A run first played offline for a long time is still rejected (its time exceeds the time since the token), as is play time inflated by fast forward.

### App logo

The launcher icon is `assets/logo.png` (a Metroid with the Triforce inside). `tools/gen_app_icon.py` (run by `build-android.sh`) crops it into the legacy and round mipmaps, the adaptive foreground (kept inside the safe zone, background colour sampled from the logo's black), and a white monochrome layer for themed icons (the Metroid silhouette with the Triforce cut out, its centre triangle solid). The 3DS build still has the default icon.

### Dungeon dots and floors on the mini map

- World map: the Zelda checks that share one spot (the chests of a dungeon, found by grouping by region and marker position) are drawn as one dot. It is the small white "done" dot only when every check of the dungeon is cleared; otherwise it is pending (amber), or dark when what is left is out of logic. Before, the stacked dots overlapped and the last one drawn decided how the dungeon looked.
- The RUN tab's mini map showed only the floor Link is on. When the dungeon has several floors it now has a column of floor chips (top floor first) to look at the others, and a HERE chip to go back to Link's floor. A picked floor lasts until Link changes dungeon.

### Position while changing rooms

On a room change the room number ($A0) and Link's coordinates ($20, $22) are not updated together, so the dungeon map could show Link on the opposite side of the new room for a moment. `Smz3DungeonMaps.live` now keeps the last settled place while the underworld module ($10 = 7) has a submodule running ($11 != 0) in the same dungeon. This is based on the known RAM layout and was not checked against a recording of a transition; if Link seems stuck on the map, the submodule test is the place to look.

### Load choice and monthly expiry

- Continue on a slot used to load the autosave state even when the game save was older. The tracker now hashes the 1 MB save RAM every 500 ms and, when it changes during play, stores the play time and the Zelda/Metroid counts of that save in the run (`saveMs`, `saveZelda`, `saveMetroid`).
- When the run's play time is more than 10 s past the last save, Continue shows `UI_LOADCHOICE` (`ui_load_choice`): a timeline with the game save (orange) and the save state (green), the gap as "+m:ss further on", and a card per option with percent, bars and counts (+N for what the state has beyond the save). Left/Right choose, A loads, B cancels; the default is the save state. "Game save" loads SRAM only (`onGameRestart`), "save state" loads the autosave. The other one is replaced when the game is left, and the dialog says so. Otherwise Continue loads as before.
- Limits: runs created before this have `saveMs = 0` and get no dialog until a save is observed; the save time comes from any change of the save RAM, so a write that is not a player save would also move it. The dialog itself was checked by rendering the command list on the PC, not seen on the device. 3DS is untouched.
- The Monthly mode row shows how long the seed lasts: "Monthly: N days left", "1 day left" or "ends today" (`ui_set_monthly(label, seed, days_left)`, days counted in UTC to the end of the month). A monthly run started near the end of the month still counts for its start month while its token is valid (3 days).

### Shared tracker core (`core/ui/smz3_tracker.{h,c}`)

The per-tick logic of the Kotlin `Smz3Tracker` now exists in C for the PC frontend: `SmzTracker` is fed the work RAM and save RAM once per tick (`smz_tracker_tick(t, wram, sram, dt_ms, speed)`) and keeps the game, started flag, checks cleared, inventory levels, the recent checks and items (newest first), boss and crystal masks, play time (counted only while the frame counters move, scaled by the fast-forward speed), a two-second hold of the last live state over transition frames, and the game's own save (`save_ms/save_zelda/save_metroid`, set when the save RAM changes during play). `smz_tracker_logic_items` builds the ItemType ids for `smz_reachable` from the inventory plus keysanity keys and cards (same maps as `RunLogic`/`RunKeys`). `core/ui/test_tracker.c` drives it with synthetic memory ("tracker ok"). Not ported yet: discovery, dungeon maps, boss marks on the map, and the tab drawing; the Android app still uses its Kotlin tracker, so nothing changes there.

### PC version (`frontends/pc`, `build-pc.sh`)

One program, `build/pc/smz3`: the shared C core (menu, generator, tracker), snes9x through its libretro interface compiled into the binary (`vendor/snes9x_libretro`, cloned at a pinned commit by the build script), SDL2 for window, audio and gamepads, the IPS patches and the font embedded (`tools/gen_ips_c.py`, `tools/gen_pc_font.py`). No data files are needed beside it. No ROM is shipped: the first run asks for Super Metroid (JU) and ALttP (JP v1.0), which are recognised by their MD5 when their files are dropped on the window (or set with `smz3 config --sm F --z3 F`); the randomized ROM of each run is generated into the data folder.

- **Window**: the shared menu is drawn from the same command list as on the Thor (`gfx_ui`, 320x240 canvas scaled to fit; mouse, keyboard and gamepad). Run creation, Cozy/Original/Custom/saved customs, delete, the load choice, 32 slots. Left and right of the game picture (4:3, 16:9 or 8:7 as the run's screen shape says) are two panels: left percent, Zelda and Metroid bars, play time, run name; right crystals, bosses and the last items. Tab (right stick on a pad) opens the tracker over the game with RUN, ITEMS, CHECKS and HELP tabs; P chooses whether the game pauses while it is open. Space (right trigger) fast-forwards, Esc asks to leave the run, F11 is full screen.
- **Saves**: same model as Android. `runs/slotN.run` (run, text file), `slotN.srm` (the game's own save, written when the save RAM changes during play and on exit), `slotN.state` (autosave state, only for runs created with Save states), `roms/` the run's ROM. The tracker is `core/ui/smz3_tracker.c`; its last-save record feeds the same load-choice dialog when the state is more than 10 s ahead of the game save. Data folder `~/.local/share/smz3`, or `data/` beside the program when a `portable.txt` file is there.
- **Command line**: `smz3 gen --out FILE [--preset cozy|original|custom] [--seed N] [--set Key=value]` writes a ROM (same output as the apps for the same seed and options), `smz3 new` makes a run, `smz3 runs`, `smz3 config`, `smz3 play N`, `smz3 selftest ROM [frames]` (headless emulator check). `SMZ3_SHOT`, `SMZ3_SHOT_AFTER`, `SMZ3_QUIT_AFTER`, `SMZ3_TAB` are test hooks.
- **Not in the PC version yet**: the world and dungeon maps (the MAP tab, the discovery tracking and the boss marks), checks in logic and helps, item icons and sprite previews (the tracker lists names), the online ladder and monthly seed, the player profile, music packages. Link and Samus sprites are read from `sprites/link/*.rdc` and `sprites/samus/*.rdc` in the data folder. Only Linux has been built and run; the code keeps to SDL2 and POSIX calls (`/proc/self/exe`, `readlink`, `realpath`) that need a port for Windows. snes9x is under its own non-commercial licence.

### Map in the PC version (`core/ui/smz3_discovery.c`, `frontends/pc/pc_map.c`)

`SmzDiscovery` is the Kotlin `Smz3Discovery` in C: rooms (Super Metroid, accepted after the same room in two samples in normal gameplay), overworld screens and dungeons seen, Samus's and Link's position on the half-size combined map (including the seven-column shift of east Crateria), the rectangles that count as discovered, the area box and the boss marks. Its tables (room sizes, region boxes, location markers) and the map image come from the app's assets through `tools/gen_pc_assets.py` into `build/pc/map_data.c`; the PNG is decoded at run time with stb_image (public domain, `frontends/pc/stb_image.h`). The MAP tab draws the map under a veil, the discovered parts clear (almost black elsewhere when the run uses "discovered only"), one dot per spot (a dungeon's chests are one dot, white when all are done), boss squares (K P D R, ? for dungeon bosses) and Samus/Link; arrows pan, +/- zoom, F follows. Discovery is saved in `runs/slotN.discovery`. `core/ui/test_discovery.c` checks the position maths, the room filter and the save format with synthetic memory. Not done: checks out of logic (dim dots), item icons, dungeon floor maps (they come from the ROM and are not in the repository), notes/marks, enlarge-on-tap.

### Checks in logic in the PC version (`core/ui/smz3_runlogic.c`)

`smz_logic_build` rebuilds a run's seed from the settings JSON stored with it (seed, keysanity, item placement, crystal counts and the `logic` object, applied with `smz_setting_set` exactly like the Android JNI) and `smz_logic_reachable` asks which locations the inventory read from the game (`smz_tracker_logic_items`: items plus keysanity keys and cards) can reach, adding the items the logic assumes (`smz_assumed_items`). Only for runs whose help mode is "all", like on Android; runs made from a plando are not followed. The PC play loop recomputes when the item set changes; the map draws unreachable checks as dark dots with a light rim (a dungeon dot is dark when none of its remaining checks is reachable) and the CHECKS tab dims them. The header is `smz3_runlogic.h` because `core/gen/smz3_logic.h` already exists. `core/ui/test_logic_run.c` builds the seed from `ui_settings_json` and checks that more checks open up with more items. `SMZ3_LOGIC_ALWAYS=1` computes it before the game starts, for screenshots.

### Item icons in the PC version

The 54 inventory icons of the app (`assets/smz3/items`, 32x32 PNG) are embedded by `tools/gen_pc_assets.py` (`PC_ITEM_ICONS`) and decoded on first use (`map_item_icon`). The ITEMS tab is a grid per game (Zelda, then Metroid) with owned items bright, missing ones dim and counts in amber; the right panel's "Last items" shows the icon next to the name. Seen in a screenshot with an empty inventory; owned and count states were not seen.

### Online ladder in the PC version (`frontends/pc/pc_online.c`)

The PC twin of `OnlineLadder.kt`, with the same files in `runs/` (`player.txt` "name<TAB>CC<TAB>mode", `install_id`, `slotN.online/.token/.finish`, `monthly.json`, `last_online_error.txt`; N is the 1-based slot number of the PC run files) and the same server calls (`/api/monthly`, `/api/start`, `/api/finish`). HTTPS goes through the `curl` program started without a shell (fork/exec, body on stdin), so the binary links no TLS library; without curl or a connection the run just stays off the ladder. The window shows the Player row (name keyboard, flag list, consent prompt) from the shared menu, fetches the monthly seed in the background (Monthly row with days left), marks a generated run online when the menu said so, asks for its token every minute while it is played, and on the credits queues the time (only for default-rules runs) and sends it in a thread; unsent finishes are retried when the window opens. `smz3 online` prints the monthly seed and the online slots. Checked against the real server: monthly seed fetched, a token issued for a test slot, a 1 s finish refused with "invalid time" and its files cleared. Not done: showing the ladder in the PC run list or in the tracker (the app shows the cached ladder on the bottom screen), and a finished online run was not played through.

### Weekly run

A third online ladder next to Original and the monthly seed: a short run with a fixed seed for everyone and its own table. Rules are the Original ones with the goals cut down (`WEEKLY` preset in `core/ui/smz3_ui.c`): 2 crystals for Ganon's Tower and Ganon, 1 boss for Tourian. The seed comes from the server: `GET /api/weekly` returns `{week, seed, weeks, endsAt}`; the week is the ISO week in UTC, so it changes every Monday 00:00 UTC, and its seed is derived from the server secret like the monthly one. `POST /api/start` accepts `kind: "weekly"` (the token carries the week where the monthly token carries the month); `GET /api/ladder?kind=weekly&week=YYYY-Www` lists the best time per installation for that week; the web page has a Weekly section with a week selector. Entries store `period`; monthly entries keep `month`.

- Menu (shared C): the mode list gets a "Weekly run" row once the host calls `ui_set_weekly(label, seed, days_left)` ("2026-W44: 6d left, 2 crystals, 1 boss"); with six rows the list uses a smaller pitch. The run's `defaultRules` is true while the options equal the weekly ones (`ui_expected_values`), false as soon as a goal is changed, which is what lets the tracker queue the finish. Online runs of this kind are drawn on a navy-to-teal gradient (`UiSlot.online == 3`).
- Android: `OnlineLadder.weekly()` (cached in `runs/weekly.json`), marker `runs/slotN.online` = `weekly`, JNI `uiSetWeekly`/`uiRunWeekly`. PC: `online_weekly`, `smz3 online` prints it.
- Checked: C test (row, label, seed, goals in the JSON, defaultRules true and false), server started locally and then deployed (the three endpoints answer), PC mode list screenshot and the Thor's New run list both show the row. Not done: a weekly run played to the credits and uploaded (the 10 minute minimum applies), the weekly table inside the apps (only on the web page), 3DS (never shows online rows).

### No save states in monthly and weekly runs; Music package hidden without packages

- Monthly and weekly runs (`runs/slotN.online` = monthly or weekly) allow no save state of any kind. The hotkeys were already off for runs made without "Save states" (the default of both). New: the app's automatic state (written every minute and on exit, loaded by Continue) is switched off for these runs (`Smz3Hotkeys.noAutosave`, used by `GameViewModelSaves.isAutoSaveEnabled`) and Continue loads only the game's own save, with no load choice. Before, an automatic state of up to a minute old could be loaded by killing the app. (Superseded: original-rules ladder runs now refuse states too, see "Tracked runs never allow states".) On PC states are only used when the run was made with Save states, so these runs never use them.
- Honest limit: this is a client rule. Anyone who builds a modified program, edits the save RAM or the time, or plays with an emulator tool can bypass it; the server cannot see states. What it does is keep honest players honest.
- The "Music package" row of the options is hidden while no package is installed (always the case on the 3DS).

## Proof of run (ladder anti-tamper)

A client cannot be trusted (the project is open source, so anyone can build a client that reports what it likes) and hardware
attestation does not fit an APK / 3DS / Linux project, so the server checks that what a client reports is coherent with the seed.

- Each online run records a proof: the checks it cleared (`SmzProof`, `core/ui/smz3_proof.c`) with the play time of the first reading
  that showed each one. PC keeps it in `runs/slotN.proof`, Android in `files/runs/slotN.proof`, and it is sent with `/api/finish`
  (`proof`, plus `seed` for the original ladder, whose seed the server cannot know).
- The server runs `smz3check` (`server/ladder/smz3check.c`, built from the same C core: see `build-check.sh`) with the settings the ladder
  fixes (monthly/weekly: the seed it issues and the weekly goals; original: the client's seed with default rules).
- Refused (400): fewer than 12 checks, a repeated or unknown check, times going backwards, a check after the claimed finish time,
  a run that ends long after its last check, a seed that cannot be rebuilt.
- Held (`hold` on the entry, hidden from the ladder until `node admin.js approve N`): more than 10% of the checks were not in logic
  for the items collected before them in the proof's own order (savestate/edit tricks, or an honest sequence break).
- Not covered: a custom client can still fabricate a proof that is coherent with the seed, and the original ladder only checks
  coherence for the seed the client names. Reviewing the top entries by hand (`admin.js list`, `remove`) stays part of the process.
- Clients without proof support are refused until updated. Test: `core/ui/test_proof.c`.

## Map follow

The Android Map tab has a [FOLLOW] button next to [CENTER]: while on, the map keeps the player in the middle at the current zoom
(so it also recentres when the game switches between ALttP and Super Metroid) and cannot be dragged. PC already had it on `F`.

## Distribution plan: one app per platform, the user brings the two ROMs

Goal: the user downloads one file per platform, points it at their Super Metroid and ALttP ROMs once, and plays. Nothing derived from
a ROM is ever shipped; the generator, patches (IPS), sprites and UI are all in the shared C core, so no extra tool or runtime is needed.

### Common "ROM setup" step (core, shared by every frontend)
- `smz_rom_classify` (`core/gen/smz3_romsetup.c`) -> Super Metroid (JU) / ALttP (JP v1.0) / another revision / unknown, by size (a
  512-byte copier header is stripped) and MD5. The patches only fit those two dumps; other revisions get a plain message.
- Accepts the two ROMs in any order, in any of: a `.sfc/.smc/.zip` file, a folder (scanned), several files dropped at once. A zip is
  read with a small inflate in the core (`smz_zip_entry`, no zlib dependency).
- Result: a "base" file `smz3_base.bin` (the combined 4 MB input the generator needs) in the app data folder. Every run is generated
  from it, so the originals are not needed again and the user can delete them.

### Per platform
- **PC (Linux, Windows, macOS):** one executable per OS, SDL2 and snes9x linked statically, no installer.
  - First start shows a window "Drop your two ROMs here" (SDL drop events) plus a Browse button (native dialog through the OS
    helper: `zenity`/`kdialog` when present, `tinyfiledialogs` single C file otherwise). CLI stays: `smz3 setup <rom> <rom>`.
  - Online features use `curl`, which ships with Windows 10+ and macOS and is on practically every Linux; no libcurl link.
  - Builds: Linux static (musl or AppImage), Windows with MinGW-w64 cross compiler (`build-pc.sh windows`), macOS universal
    through `zig cc` or a GitHub Actions macOS runner (macOS cannot be cross-built cleanly from Linux). Portable mode already exists
    (`portable.txt`, data beside the binary).
  - Still to port: paths (`%APPDATA%`, `~/Library/Application Support`), `fork/exec` curl -> `CreateProcess`/`posix_spawn`,
    `pthread` wrappers, `SDL_GetPrefPath` for the data folder.
- **Thor / Android:** the APK already carries the generator. Setup = Android's file picker (Storage Access Framework, no
  storage permission) with multi-select for the two ROMs, or the existing scan of the usual ROM folders; the result is the base file.
- **3DS:** snes9x_3ds `.3dsx` plus its romfs (IPS patches) in one folder; the user puts the two ROMs on the SD card in `/smz3/` (any
  names) and the app finds them by header and MD5 on first start, with an on-screen list of what is missing. No PC tool is required.
  Optional PC helper: the same `smz3 setup` writes `smz3_base.bin` to the SD card so the 3DS skips the scan. Online features reuse
  the `3dsra_http` client.

### The "final version" tool
`smz3 setup <rom> <rom>` (PC CLI, also the GUI drop window) is the single tool: it validates both ROMs, writes the base file, and
with `--target thor|3ds|pc --out DIR` it lays out the folder to copy to a device (base file in the right place for that app).
A release is therefore always the same few files per platform: `smz3-linux`, `smz3-windows.exe`, `smz3-macos`, `smz3.apk`, `smz3.3dsx`.
It never contains ROM data, so it can be published freely.

### Order of work
1. `smz_rom_identify` + zip reading + `smz3 setup` in the core and PC CLI (testable now, no hardware).
2. PC drop window, then Windows and macOS builds and path/process wrappers.
3. Thor: SAF picker feeding the same base file.
4. 3DS: first-start scan + base file; then online through `3dsra_http`.

### Done: ROM setup on PC (step 1)
`smz3 setup <rom|zip|folder>...` and the window's drag and drop share `setup_path`: each recognised ROM is copied without header to
`base/supermetroid.sfc` and `base/alttp.sfc` in the data folder and `config.txt` points there. Tested with loose files, a deflate zip,
a stored zip, a folder and a wrong revision (USA ALttP is refused with the reason). Android and 3DS still use their own scans.

### Done: Windows build, Android ROM picker (steps 2 and 3)
- `./build-pc.sh windows` cross-builds `build/win/smz3.exe` with MinGW-w64 (SDL2 and snes9x linked statically, SDL2's MinGW files downloaded
  once; console hidden when started without arguments). Under Wine: `setup`, `gen` (byte-identical ROM to the Linux build for the same seed)
  and the window all work; online needs `curl.exe` (Windows 10+). Data folder is `%APPDATA%\smz3`. macOS paths (`~/Library/Application Support`)
  are in place and `.github/workflows/pc.yml` builds the three systems, but macOS has not been built or run.
- Android: when no usable base ROMs exist, creating a run opens the system file picker (multi-select, files or zips); the two ROMs are kept
  in `files/base/` and generation continues by itself. Verified on a tablet: zip picked, run generated.

### Done: 3DS ROM zips and online ladders (step 4)
- The 3DS finds the base ROMs by header and MD5 in `sdmc:/SMZ3/base`, `roms/snes`, `roms`, `ROMs` and the card root, loose or inside a zip
  (`smz_zip_entry` from the core). Verified in Azahar: a run was generated from `base/roms_both.zip`.
- Online (`snes9x_3ds/source/smz3online.cpp`, own blocking httpc client on short threads, certificate check skipped only on a second try):
  Player row (name, flag, consent) saved in `sdmc:/SMZ3/online/player.txt`, monthly and weekly seeds fetched at menu start, per slot files
  `slotN.online/.token/.finish/.proof` like the other frontends, token requested while playing, finish sent with the proof of run.
  The in-game tracker now uses the shared `SmzTracker` (play time counts only while frames move, saved in the run file every 10 s), so the
  slot list shows real time and progress. Monthly and weekly runs refuse save states, manual and automatic (`smz3NoStates`).
- Verified in Azahar (network works there): seeds shown in the mode list (monthly "4 days left", weekly "5d left"), a weekly run generated
  with the online gradient and the player name, and a real token obtained from the server (`debug_token.txt` debug aid). Not verified: a
  finished run uploaded from a 3DS, and real hardware.

### Done: all ladder tables inside the apps (step 5)
- Shared menu: `UI_LADDER` screen with Original / Monthly / Weekly tabs (Left/Right or touch), reached from the run list with Left/Right or the
  Ladder button in the title bar when the host enables it (`ui_ladder_enable`). The host answers `ACT_LADDER` by fetching the three
  `/api/ladder` answers and passing them to `ui_ladder_load_json` (rows: rank, name, flag code, time; "Loading ...", "No connection" and "No finished
  runs yet" states). Test in `core/ui/test_ui.c`.
- PC and the 3DS fetch in a thread; Android does it from `NativeMenuScreen` (`OnlineLadder.ladderJson`). The Android bottom screen's ladder
  button now pages through the original, monthly, weekly and custom-runs tables (cached per table in `files/runs/`).
- Verified: PC against a local server with sample runs (screenshot), 3DS in Azahar against the real server (empty tables), the tablet in
  the "No connection" state. Not verified: the Thor's bottom-screen pages (compile only), a table with rows on Android/3DS.

## Done: limits and loose ends

- **Proof plausibility holds**: besides the hard refusals, a finish is held (hidden until `node admin.js approve N`) when its first check comes in under 15 s or the average is under 6 s per check. A forged but coherent proof can still pass; the top of the ladder is meant to be reviewed by hand.
- **`smz3 flush`** sends a finished run's stored token and proof; `SMZ3_SERVER` points the PC client at another server. Used for an end-to-end test against a local server (forged old token, recorded proof, seed 4242): the run appeared on the ladder.
- **PC sprite previews**: UiCmd kind 2 draws `sprites/<kind>/<name>.pv` with a texture cache.
- **PC music packages** (`pc_music.c`): packages are folders of `.pcm` under `<data>/music/<name>/`; at play time the `.msu` marker and `<rom>-N.pcm` links (symlink, Windows hardlink or copy) are made next to the ROM, and `emu_open` now receives the ROM path so snes9x finds them. `Random` deals every package with the seed like `Smz3Music.prepare`. Checked with a link-level harness only, not by listening to a game.
- **Not done**: dungeon floor maps and the "Original" sprite icons on PC (ROM-derived, not committed); Android's original ladder still allows autosave.
- **macOS**: the CI job on `macos-15-intel` builds the PC app (it found one real problem: make 3.81 does not resolve the generated `ips_data.c`/`map_data.c` through VPATH, fixed with explicit rules). The Apple-silicon images (`macos-14`, `macos-15`) never got a runner for this account and were dropped from the matrix. The binary has not been run on a Mac; the build links SDL2 dynamically (`brew install sdl2`) and is unsigned (see README).

## Done: tracked runs, ROM-derived data on PC, prompt text, Windows music

- **Tracked runs never allow states.** A run on any online ladder (original, monthly, weekly) allows no save state of any kind: no hotkeys, no manual states, no automatic state; Continue loads only the game's own save. Android: `Smz3Hotkeys.noAutosave` is true for `slotKind >= 1` and `saveStatesAllowed` returns false with it; the menu skips the state/save choice. 3DS: `smz3NoStates` for kind >= 1. PC: states are ignored when the slot is on a ladder. Runs that are not on a ladder keep the per-run "Save states" option.
- **PC: data read from the user's ROMs at setup.** `core/gen/smz3_romderived.{h,c}` ports `tools/sm_rooms.py` (Super Metroid room table, 255 rooms) and `tools/gen_original_icons.py` (the Original Link and Samus previews). Both outputs are byte-identical to the Python tools on the test ROMs. `smz3 setup` and the first window start write `derived/sm_rooms.bin` and `sprites/{link,samus}/Original.pv` in the data folder; the map's discovered-room overlay reads the table from there (the built-in one is empty in CI builds). Dungeon floor maps are not ported: the PC map has no floor view to show them.
- **Online prompt text** (`UI_ONLINE`) was wider than the portrait canvas on Android; it is now three short lines and shorter option captions, and says that no save states are allowed while a run counts.
- **Music links on Windows, tested under Wine.** Wine accepts `CreateSymbolicLinkA` but leaves a link that does not resolve; `link_track` now checks the link opens and falls back to a hard link, then a copy, and reports how many tracks really linked (47 for a pool, fixed tracks by name, none when the run keeps the game's music).

## Done: PC controller, settings screen, dungeon maps

- **Controller.** Everything in the PC app is reachable from a pad. The D-pad and the left stick drive the menus (one step, then repeating while held); A, B and the bumpers are choose, back and page. In the menu, Select or Start (F1 on the keyboard) opens the settings screen: *Controller buttons* (assign SNES B, A, Y, X, L, R, Start, Select, the tracker toggle, leave run and fast forward; saved in `pad.cfg` in the data folder; triggers can be assigned; a 6 s countdown cancels), *Find the ROM files* (a file browser over the Downloads folder: folders, `.sfc`/`.smc`/`.zip`, "use this folder"; also opens by itself when generating without ROMs), full screen and quit. `core`-independent: `pc_pad.c` holds the mapping and `pc_settings.c` the screen. Verified by screenshots and by logic only; no physical pad was connected.
- **Dungeon maps on PC.** `core/gen/smz3_dungeonmaps.c` ports `tools/zelda-dungeon-maps.py`; its output (both 640x1120 atlases, floor grids, chest positions) is byte-identical to the Android assets on the USA ROM and on the Japanese v1.0 ROM (the table addresses differ: graphics 0x706 lower, floor tables 0x10 higher; the header's region byte tells them apart). `smz3 setup` writes `derived/dungeons.bin`. The map tab shows the pictures when Link is inside a dungeon and with D / Y otherwise: Left/Right (or D-pad, left stick) pick the dungeon, Up/Down the floor, H / A return to Link's floor; the room quadrants he has visited are drawn in full (everything on all-map runs), with unopened checks at their chests (hollow when out of logic), the boss once the compass is owned, and Link blinking.
- **Windows bug found with Wine:** `rename()` does not replace an existing file on Windows, so every save of an existing file (run saves, config) failed after the first. `store_write` now uses `MoveFileExA(... REPLACE_EXISTING)`.

## Done: PC welcome screen and screen fades

- **Welcome screen** (`pc_welcome.c`), shown first when the base ROMs are missing (also when generating without them, and with `SMZ3_WELCOME=1`): two cards, Super Metroid and A Link to the Past, each naming the exact ROM it needs (Super Metroid (JU), ALttP Japan v1.0) and the expected MD5. A dropped file, zip, folder or a browsed one fills its card: green with the file name and "Checksum matches, saved", or red with its own MD5 and "Right game, wrong version" (a wrong file never replaces a good one; a file that is neither game is reported under the cards). When both are in: sparkle bursts, the Play! panel lights up with a pulsing glow and a light sweep; A, B or a click leaves it with a fade. The ROMs are copied into the data folder at once (`base/`), so the originals can move away. Mouse, keyboard and pad all work (Browse opens the file browser, whose B now closes it).
- **Fades.** Every change of screen (menu screens, settings, welcome) fades in from the background colour (`fx_screen_changed`/`fx_draw`).
- Test hooks: `SMZ3_DROP=<path>`, `SMZ3_PLAY_BUTTON=1`.

## Done: menu and tracker pass (PC, with parts on Android and 3DS)

- **Run list.** Boss tokens beside Samus come from the icons built into the PC binary (`gen_pc_assets.py` packs `rewards/`), dimmed when the boss is not beaten. The percent sits inside a progress ring on hosts that set `u->rings` (PC); the others keep the big digits. Online runs show the country flag (`u->flags`, PC and Android; the 3DS keeps the code). Flags are rendered once from Noto Color Emoji by `tools/gen_flags.py` into `assets/flags/`. The app logo sits beside every "SMZ3" title (core command `logo`: PC built-in, Android `smz3/logo.png`, 3DS `romfs:/smz3/logo.pv`). Two-line rows of the mode list got more room. The settings lose the ROM row: the welcome screen asks again when the ROMs are missing.
- **Tracker.** The Help tab is two columns of controls with the buttons of the device in use, centred between the tab bar and the help bar; the overlay fits between the side panels. Right stick up/down zooms the map. Tourian (Super Metroid area 5, origin 900,84 on the full map) is placed, so rooms and Samus show there (core and Android). Dungeon strip: checks left per dungeon. Interface sounds (`pc_sfx.c`, made on the fly on an audio device of their own). "Leave the run?" says "Save and go to the run list" and the screen closes like an old TV before the run list wipes in.
- **Reachable checks** are a run option (`ShowReachable`, Play tab, off by default, on in Cozy run, not a rule change). Runs made before the option mark them only when they are Cozy runs.
- **Video while playing.** V (or X in the tracker) opens a compact panel over the game: full screen, window size, aspect, filter, minimap side (right, left, off) and size, all kept in `video.cfg`.
- **Fast forward** keeps the music at its pitch: one frame of sound out of four, each blended into the next, and the sound keeps the pace (4x).
- **Final time.** The tracker samples every 100 ms so the end is taken close to the real moment. A run-complete card (PC and Android) shows the time with hundredths and whether it went to the ladder; the ladder tables (apps, web page, admin) show hundredths too (the server already stored milliseconds).
- **Where the run ends.** Not at the credits any more but where control is lost, like the SMZ3 tracker and the ALttPR race rule (the mosaic before the Triforce room): ALttP module `0x7E0010` = `0x19` with Super Metroid done (`0xA17402`), or Samus entering the ship (`0x7E0FB2` = `0xAA4F`) with both games done (`0xA17402`, `0xA17506`); the credits stay as a fallback (`smz3_run_finished`, `smz3_summary.finished`). The tracker and the JNI report game 3 from that moment, the read that sees it still counts its time and the ending scene is not counted. `test_tracker` covers both endings with made-up memory; not seen in a real game yet.
- Test hooks: `SMZ3_LEAVE=<ms>`, `SMZ3_DONE=<ms>`, `SMZ3_VIDEO=1`. Tests: `test_ui` (Next button position, the reachable option), `test_discovery` (a Tourian room).

## Done: 3DS bottom-screen tracker like the Thor's

The 3DS panel was a few text lines; it now has the tabs of the Thor's second screen (`Smz3Presentation.kt`), drawn in software into the 320x240 framebuffer by `smz3TrackerFrame` (`vendor/snes9x_3ds/source/smz3run.cpp`).

- **Tabs.** RUN (big percent, game, clock, Zelda and Metroid bars, items and pace, boss tokens and crystals, recent items with icons, the two regions nearest to done, and a mini map that follows the player; tapping it opens MAP), ITEMS (the 54 icons, missing ones dim, counts), CHECKS (regions with count and bar, a tap opens the checks), MAP (whole map, drag to pan, -, +, FOLLOW, CENTER), HELP (with helps: "Give me a hint" and the hints so far; in a daily: its timed hints, no buttons). A daily has no tab of its own: the RUN tab draws the Thor's daily block (goal box with the players bar, counts of playing now / finished / started with pixel icons, next hint, window), and a heartbeat asks `/api/daily/hints` once a minute for the `players`; every 10 minutes, with two or more playing, the strip says how many for 8 s. In a daily run a two-line strip over the tabs has the window clock, own time from `smz_daily_clock_from`, next hint and the goal (or a new hint for 15 s). Colours follow the game, as on the Thor.
- **Touch.** During a run the touch screen belongs to the tracker; the pause button at the right of the tab row opens the emulator menu that a touch used to open (`smz3TrackerTouch`, called from `input3dsScanInputForEmulation`). The run is read once a second; a touch redraws at once, a drag at most every third frame.
- **Shared code.** Counters, inventory, recent items and the daily clock come from `smz3_tracker`/`smz3_daily`; the map uses `smz3_discovery` (now built for the 3DS) for the player, the seen areas (`slotN.discovery`), discovered-only runs and boss marks, with the Zelda dungeon chests merged into one dot as on PC.
- **Assets.** `tools/gen_3ds_assets.py` (run by `build-3ds.sh`) writes the map data as `source/smz3_mapdata.c` and into romfs the map at 1/2, 1/4 and 1/8 pixel per marker unit as RGB565 (`map0..2.bin`, about 1 MB in all) and the item icons and boss tokens as `.pv` (`icons/`).
- **Gone:** the room, x/y and m/i/e/s debug lines. `debug_panel.txt` still writes `debug_state.txt`. New hook: `sdmc:/SMZ3/debug_touch.txt`, lines `t <x> <y>` (tap) or `d <dx> <dy>` (drag).
- **Checked in Azahar** (flatpak 2126.2, run on an Xvfb display and driven with xdotool, real mouse touches): every tab, tab switching, scrolling the checks, opening a region, map zoom, follow and drag, the hint button, the pause button, the daily strip and tab of a finished attempt (`docs/shots/3ds-tracker-*.png`). Not checked: speed on a real Old 3DS, the daily strip of an attempt still running.
- **Item shortcuts** (`core/ui/smz3_shortcuts.h`), a row over the RUN tab's map as on the Thor: in ALttP five slots kept for every run in `sdmc:/SMZ3/shortcuts.txt` (an empty slot or a held one opens a picker of the items owned, with CLEAR and CLOSE; a tap equips the item on Y), in Super Metroid the beams owned, a tap turns one on or off. The active one is underlined. Only in normal play, silently; no pad buttons on the 3DS. Continue adds the ROM hooks to a run made before them (`smz_rom_shortcut_hooks`, written into the run's file once), and a loaded save state sets `state_loaded` (`smz3StateLoaded`). Checked in Azahar with memory written through `debug_poke.txt` (new `W`/`S` lines write the value as it is): the beams toggle, the picker fills a slot, a tap puts the bow on Y, a hold opens the picker. The game's own picture of the change was not seen (Azahar's picture of snes9x_3ds is garbled), nor real ALttP play.
- **A finished run** (`game == 3`) is not played again: the slot menu says so (its numbers come from `ui_set_slot_stats`) and Continue refuses it, the daily slot too. No save states once it ends (`smz3NoStates`), and it goes to the ladder only in the session where it ends. A card over the bottom screen gives the final time with hundredths and where it went: A goes to the run list through the emulator's menu (`smz3TrackerKeys`, `smz3TakeRunListRequest`), B closes it to watch the credits. Checked in Azahar with the credits faked through `debug_poke.txt` (`S 33fe 11`): the card, A to the run list, the Finished row (`docs/shots/3ds-tracker-finish-card.png`, `3ds-finished-slot.png`).
- **A standalone app, like the Thor's.** It boots into the run menu (`smz3Home` in `3dsmain.cpp`); there is no emulator splash, file browser or emulator menu, and B on the run list does nothing (the app closes from the HOME menu). The pause menu is the Thor's game menu, drawn like the run menu on the top screen (`smz3PauseMenu`: wallpaper, cut-corner rows, amber cursor, button legend; no emulator widgets): Resume, Save state and Load state with Left/Right for the slot (only where the run allows states: `smz3NoStates` now also reads the run's `saveStates`), Back to the runs, Restart, Mute audio, the shortcut rows, Save bar (Shown by default) and Confirm save/load (Off by default), and on a New 3DS Buttons: ZL, ZR and the four C-stick directions, each bound to fast forward (held), save state, load state, the game menu or an item shortcut (A picks the action, then waits for one of those six buttons and names what any other one does; a button in use asks before it is replaced; an action lives on one button; X clears). The save bar sits on the RUN tab under the shortcut rows: SAVE and LOAD on one quick slot (slot 1; "Saved"/"Loaded" for 1.5 s; greyed where the run allows no states) and a fast forward toggle. Fast forward is the 3D slider held up (it overrides the toggle and, back at 0, turns it off; the slider no longer drives stereoscopic 3D), the toggle or a held bound button, never in a daily. These settings are global (`sdmc:/SMZ3/3ds_input.txt`); the old per-shortcut button bindings are gone. B, START, the menu key or the tracker's pause icon go back to the game. No hint here: the HELP tab has the button. Leaving a run writes its play time and progress at once, and the run list opens with the cursor on it. The run's screen filter is applied (CRT and LCD take Balanced: the 3DS has no shaders), and no pictures are drawn around the game or behind the tracker. SMDH: "SMZ3" by doctor-utsugi, with the Thor's launcher icon at 48x48 (made by `build-3ds.sh`).
- **Not on the 3DS yet:** checks in logic (the seed rebuild wants a large stack), dungeon pictures, revealing a region's checks with a help, map marks, pad buttons for the shortcuts.

## To do

- Portal transition (Thor and 3DS): on a world switch, fade the bottom screen out first, then play the portal iris over the new game's tracker.
- PC: the daily's RUN block (goal bar, players now/finished/started, next hint) and the "N players on this daily right now" note, as on the Thor.
