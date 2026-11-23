# Offline generator: port of the Cas' SMZ3 generator to C

Goal: Create run and the logic view work on the Thor and the 3DS with no PC or server. The C code lives in `core/gen/` and is shared by both frontends. The C# generator (`vendor/SMZ3Randomizer`, `tools/gen-cli`) stays on the PC as the oracle that every stage is checked against. The service in `tools/gen-cli serve` stays only as a stopgap until stage 4 lands.

## Size of the job (measured)
- World data: 48 region files (about 10k lines of C#, most of it location tables and one-expression `access:` lambdas), `Progression.cs` 224 lines, `Logic.cs` 258 lines.
- Generator: `StandardFiller` 432 lines, `Smz3Randomizer` 188, item pools in `WorldItemPools`.
- ROM writing: patches about 2.5k lines (plus 935 lines of one music table), `Rom.cs` 167, `Dialog`/`StringTable` text code, `RomTextService` 322, game modes 4 small files.
- The logic is regular boolean code over a progression (counts of items, rewards, bosses), so it is transpiled by script; the dungeons' key logic and the filler are ported by hand.

## Stages, each with its own check
1. Logic engine. Progression as counters, `Logic.cs`, every region `CanEnter` and location `access`, key logic. Check: for thousands of random inventories, the set of reachable locations equals the C# `Location.IsAvailable` (the `/logic` endpoint is the oracle).
2. World and item pool. Locations (id, patch address, type, vanilla item), prerequisites, rewards, bosses, item pools per game mode and options. Check: pool and location tables equal the C# ones.
3. Filler. Port of `StandardFiller` with its own PRNG (seeds do not have to match the web's). Check: every generated placement is beatable per the C logic and per the C# logic (the C# parses the ROM back with `Smz3RomParser` and runs its playthrough).
4. ROM writing. Locations, rewards, prizes, starting equipment, texts, keysanity, goals, metadata, and the IPS patches embedded as data. Check: for the same placement exported from the C# generator, the C output ROM is byte-identical.
5. Options panel and Create run on device read the options from a table generated from the C# attributes (as `/schema` does today); the app calls the C library over JNI, the 3DS frontend calls it directly.

## Done so far
- `tools/gen_enums.py` writes `core/gen/smz3_enums.h` (ItemType, RewardType, BossType, LocationId, with the tracker's numbering).

## Stage 1a done: Progression and Logic.cs
`tools/port_logic.py` transpiles the Progression properties and the Logic.cs methods into `core/gen/smz3_logic.h`
(plus `smz3_logic_table.h` for the test). `tools/test-logic.sh` replays random inventories and logic settings through
`gen-cli vectors` (C# answers) and the C port: 3000 cases, 45k checks, 0 mismatches. Only `CheckAgahnim` is left
for the region port because it needs the world. Next: region access lambdas (stage 1b), checked against `/logic`.

## Stage 1b done: regions, locations, rewards, bosses
`tools/port_world.py` (with `tools/cs_regions.py` as the parser and `tools/world_overrides.py` for the few hand-written
parts: Ganon's Tower key counts, Ice Palace key waste, Ganon's Tower entry) turns the 40 region files into
`core/gen/smz3_world_gen.{h,c}`: region entry functions, helper methods, per-location access and the 316-location
table (rom address, type, vanilla item, region, memory address/flag, SphereOne weight). `core/gen/smz3_world.c` holds
rewards, bosses, `CheckAgahnim`, and the world defaults. `SmzWorld.item_at[]` carries placed items for the key logic.
`tools/test-world.sh` replays random logic settings, keysanity modes, reward and medallion shuffles, key placements and
inventories through `gen-cli worldvectors`: 3000 cases, 1.28M checks (every location, region entry, reward, boss), 0
mismatches. Tracker-only logic (marked medallions, relevance) is not ported on purpose.
Not ported yet and needed next: `Allow`/`AlwaysAllow`/`CanFill` rules per location (stage 3), item pools (stage 2).

## Stage 2 + 3: item pools and the standard filler (done)

`core/gen/smz3_rng.{h,c}` is the exact .NET `Random` (seeded subtractive generator), `smz3_fill.{h,c}` the item pools
and `StandardFiller` (single world): reward/medallion/bottle setup, dungeon items, front fill of Super and Power Bomb,
AssumedFill with location weighting, Ganon's Tower junk fill, FastFill. `tools/test-fill.py [n] [rngseed]` compares
the placement, rewards and prerequisites of random seeds and settings (keysanity None/Zelda/SM/Both, placement rules,
crystal and Tourian boss counts) with `gen-cli placementbatch`: 90 of 90 seeds are identical to the C# generator.
Default logic options must mirror `LogicConfig` (wall jumps default to Medium). Not ported: Metroid boss token
shuffle, plando, starting-item and early-item preferences, game modes other than standard.

## Stage 4: ROM writing (done) and stage 5: Create run on the device (built, not yet tried on the Thor)

- `smz3_patch.c` ports every seed-dependent `RomPatch` (metadata and RNG block, rewards, prizes, fairy pond, digging
  game, locations and item table, keycards, goals, medallions, dungeon music, uncle equipment, menu/controls options).
  `tools/test-patch.py` compares offset by offset with the C# patcher on random seeds and settings: identical.
  The patcher draws from `Random(patchSeed).Sanitize()` in the C# order (rewards, metadata, digging, prizes, fairy pond).
- `smz3_text.c` compiles dialogs and the 406-entry string table; the entries that do not depend on random text variants are
  byte-identical to the C# ones. Text variants are picked from the seed (the C# picks them with an unseeded Random, so
  byte parity is impossible there). Data comes from `tools/gen_texts.py` (`gen-cli textdata`).
- `smz3_rom.c` combines the ROMs, applies the IPS patches and the checksum; `smz3_generate.c` is the whole pipeline
  (`build/gen_rom` is the command line front end, 27 ms per ROM). The default Cas' IPS patches are applied in the C# order.
- Not ported: hint tiles (`GameHintService`), the playthrough validation, the Etecoons jingle, sprites/MSU, plando, starting
  items, Metroid boss token shuffle and the AllDungeons/SpazerHunt modes. The offered options are limited to what is ported.
- Android: `frontends/android/jni/smz3_jni.c` exposes `generate`, `reachable` and `itemByName`; `RunGenerator` generates
  the ROM on the device from the IPS patches and `schema.json` in the app assets (`tools/prepare_assets.py` copies them;
  `gen-cli schema` writes the schema). `RunLogic` asks the C library for the locations in logic using the run's own seed,
  so medallions, rewards and keys are the real ones. The PC service is no longer used by the app.
- The tracker patch for the Lemuroid fork lives in `patches/lemuroid-tracker.patch`.
- `vendor/SMZ3Randomizer/.../PatcherService.cs` carries a temporary `Trace` list that `gen-cli placement*` dumps; the
  original is not kept in git, restore it with `git -C vendor/SMZ3Randomizer checkout` when the patch tests are not needed.
