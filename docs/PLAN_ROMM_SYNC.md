# Plan: one account, the same runs, saves and states on every device (RomM)

Goal (the owner, 2026-11-26): open SMZ3 on the Thor, then on the PC, the RGDS or the 3DS, and find the same account,
the same runs, the same saves and the same save states, with nothing that only works on some devices. RomM is the
store. Anything worth redesigning for that is redesigned now.

## What each device runs today

| | Emulator | Save state format | SRAM |
|---|---|---|---|
| PC (Linux, Windows, macOS) | snes9x 1.63, libretro, vendored in `vendor/snes9x_libretro` | `#!s9xsnp:0014` | `.srm` |
| Android (Thor, RGDS) | snes9x 1.63 (`5a40cd55`, September 2025), the prebuilt core of `lemuroid-cores` | `#!s9xsnp:0012` | `.srm` |
| 3DS | snes9x_3ds, built on snes9x 1.43 (`vendor/snes9x_3ds/source/Snes9x`) | `#!snes9x:0001`, old APU (`APU`/`ARE`/`ARA`/`SOU`) | `.srm` |

SRAM is the cartridge's battery memory and is the same bytes on every emulator. PC and Android states are the same
family but not the same version: the PC core (libretro master of September 2026, `fae2fea`) writes version 14, the
Android core version 12. The PC loads Android states; Android refuses PC ones (`version > SNAPSHOT_VERSION`). The
3DS has its own, and its APU (sound chip) is the old snes9x one, whose state has a different layout from the new
APU's `SND` block.

The 3DS cannot simply run snes9x 1.63. snes9x_3ds was built on the lighter 1.43 code to run full speed on the 3DS,
and its hardware renderer is woven into that code.

## Decisions

### 1. One canonical state format: snes9x 1.63 (`#!s9xsnp:0014`)

RomM only ever holds version 14 states, the PC's format. Other RomM clients and RetroArch load them with a snes9x
libretro core from 2026-08-07 or later; older cores refuse them.

- Android builds its snes9x core from `vendor/snes9x_libretro`, the same source as the PC, instead of the
  prebuilt one. The two then have the same code and write the same version, which is what lets the Thor and the
  RGDS load PC states at all.
- The 3DS converts. A shared C module (`core/state/`) translates 1.43 ↔ 1.63 and runs on the 3DS itself: it
  converts on upload and on download, and the local file stays in the 3DS's own format.

What the converter maps:

- Straight copies: work RAM (128 KB), VRAM (64 KB), SRAM, OAM, CGRAM, the register file (`FillRAM`), the 65816
  registers, DMA channels, the SPC700 registers, audio RAM (64 KB) and the DSP's 128 registers.
- By hand: CPU timing (H/V counters, IRQ and NMI state, the cycle clock), the PPU's decoded state (rebuilt from
  the registers where 1.63 keeps it differently), the APU timers and the ports between the CPU and the APU.
- Approximate: the DSP's internal voice state (envelope level, BRR position, echo buffer), which the two APUs
  track differently. At worst a few milliseconds of sound are off right after loading. The game's state is
  exact.

The ROMs SMZ3 makes (ALttP, Super Metroid, SMZ3's ExHiROM) use no special chips (SA-1, SuperFX, SPC7110), so
the converter only needs the base hardware.

Proof it works, on real devices: a state from the PC, loaded on the 3DS (Azahar and a real 3DS) after
conversion, and the other way round, in the middle of a boss fight and in a room with music.

### 2. Each run has its own id; its ROM is a ROM in RomM

A run gets a run id (its game letter and a UUID) when it is made, and that id is its identity on every device. The
ROM name from `smz_run_rom_name` (`<game>_<seed>_<hash>.sfc`) names the ROM, not the run: two runs with the same
seed and the same ROM settings share one ROM, and differ in their tracker options, help count, saves and states. In
RomM they share the ROM and keep everything else apart by run id.

For the ROM to be found on every device, every device must compute its name from the same final settings. Today
the 3DS names daily and race ROMs before it adds the challenge or race fields (the PC and Android name them after),
so the 3DS changes to the same order.

| SMZ3 data | In RomM |
|---|---|
| The run's ROM | A ROM on the `snes` platform, uploaded when the run is made |
| The run (id, name, settings, seed), its tracker (map, marks, goal counters, help) and its sync record | A private note on the ROM (`/api/roms/{id}/notes`), title `smz3-run <run id>`, content JSON |
| The in-game save (SRAM) | A save with emulator `snes9x`, slot `<run id>`, file `<run name>.srm` |
| The resume point and the 4 quick states | States with emulator `snes9x`, in 1.63 format, labels `<run id>` and `resume`, `q1`..`q4` |
| A paused online race: its state, and its token, room code and resume time | A state labelled `<run id>` and `race`; the rest in the run note |

Any RomM client sees an ordinary SNES game with its save and states; the run file is a note, so it never shows up
as a save. SMZ3 sees the whole run.

Every device keeps the same state slots: the resume point, 4 quick states and the paused race. The PC and Android
have 4 quick states already; the 3DS has 5 (`SAVESLOTS_MAX` in `vendor/snes9x_3ds/source/3dssettings.h`) and goes
down to 4.

### 3. The run list comes from RomM, not from slots

Local slot numbers (`slot1.run`, 4 per game on the 3DS) stop being the identity. Each device lists the account's
runs from RomM, per game. A run that is not on this device yet shows as "in the cloud", and opening it downloads it.

The 3DS keeps its limit of runs stored on the SD card; older ones go back to "in the cloud" and the list itself has
no limit.

### 4. One account on every device

- RomM login with a device code: the console shows a short code, you approve it on the RomM page (phone or PC).
  No typing passwords on a 3DS. RomM 5.4 has this (`/api/auth/device/*`).
- The SMZ3 player (name, gems, world goals, ladder) is one player on every device: the SMZ3 server links install
  ids into one player. Right after the RomM login, a new device shows a code that another device of the same player
  approves. Today every install is its own player.

### 5. Sync rules

- Opening a run downloads whatever is newer in RomM; leaving it uploads the save, the resume state and the run
  file. Quick states upload when they are made.
- RomM tracks the device and a content hash for saves, not for states. So SMZ3 keeps its own sync record in the
  run note: a revision number and, for the `.srm` and every state, its hash and the device and time that made it.
  Every upload, quick states included, bumps the revision. If the note's revision moved since this device last
  synced and the device also changed the run, the app asks which to keep and shows the time and device of each.
- With no connection, the device plays its local copy and syncs on the next start.

### 6. Online runs and what counts

A ROM in RomM can be played in any emulator, so nothing that arrives from RomM is trusted on its own.

- World goals and gems: the server keeps a run's latest totals, not what changed, so loading an older state takes
  back what it undid. That model stays across devices: reports are keyed by player and run id (the run id keeps
  its game letter, `z` or `m`, which the server reads), and the tracker's goal counters travel in the run note and
  with each state. Every device then reports the run's real totals. A save or state whose hash the SMZ3 server does
  not know (made outside SMZ3) starts from what the game already shows done, and only progress made after it counts.
- Ladder, daily and races rank a whole finish time, so a starting point is not enough there. Trust starts when
  SMZ3's own emulator writes a save or state: SMZ3 keeps its hash in the run's sync record at that moment. Before an
  upload it checks the file against that record; a file that does not match (put in the run folder from another
  emulator, or edited) goes up marked unranked. The SMZ3 server records the hash of every matching upload, for any
  run; for an online run also its play time and the run's proof up to that point. Cheating then means changing
  SMZ3's own records, the same bar the proof of run sets today. On download, SMZ3 checks the hash with the server: a
  known one carries on ranked, with its play time and proof; an unknown one (made in another emulator, or edited)
  carries on as an unranked run. An honest player never sees the difference.
- The SMZ3 server hands an online run to one device at a time, so two devices cannot race the same run. A paused
  race resumes on any of the player's devices: the server matches the rejoin by player, not by install. With no
  connection, only the device that holds the run can play it.

### 7. Server

RomM moves to the home server, with its own subdomain and HTTPS, so every device reaches it outside home too. The test
install on a test machine (port 8095) goes away.

## Order of work

1. RomM on the home server (subdomain, HTTPS, backups of the database and the assets).
2. SMZ3 server: installs linked into one player; the hash of every save and state an SMZ3 device uploads whose hash
   matches its sync record, for any run (for online runs also the play time and proof); world goal reports and race
   rejoins keyed by player; online runs handed to one device at a time.
3. `core/romm/`: shared C client (login with device code, list runs, run notes, upload and download with resume),
   used by the PC and the 3DS; Android calls it through JNI like the rest of the shared code.
4. PC: run ids (existing runs get one), run list from RomM, player link, sync on open and leave, conflict screen.
5. Android: the snes9x core built from `vendor/snes9x_libretro`; then the same as the PC.
6. `core/state/`: the 1.43 ↔ 1.63 converter, checked state by state on the PC and on Azahar.
7. 3DS: 4 state slots instead of 5, ROM names from the final settings; then the same as the PC, with the converter.

Each step ships on its own: after step 5, PC, Thor and RGDS share the player, runs, saves and states; after step 7
the 3DS does too.
