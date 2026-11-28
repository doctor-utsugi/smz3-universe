# Three games: SMZ3, A Link to the Past and Super Metroid (plan and progress)

SMZ3 Universe gets two more randomizers next to the combo: A Link to the Past alone (Z3) and Super Metroid alone (SM).
Each one is a full mode with the same spirit as SMZ3: seeds made on the device from the player's own ROM, offline, with
the tracker on the second screen, and its own online ladders, monthly and weekly seeds, daily challenge, races and
world goals. Each game also takes the options and the rules its own community plays with.

This file is the plan to iterate on, in the style of `RACES.md`: decisions in a table, what is still open at the end. Every
stage check below (oracle comparisons, generator batches, runs in an emulator) is run only after the owner says yes.

## What the communities use (surveyed 2026-11-24)

### A Link to the Past

| Project | What it is | Licence | Use here |
|---|---|---|---|
| [sporchia/alttp_php_randomizer](https://github.com/sporchia/alttp_php_randomizer) | The generator behind alttpr.com (PHP/Laravel). Web at v31.2.1 (September 2023), repo last pushed July 2024 | MIT | **The source to port.** Regions per world state (`app/Region/Standard`, `Open`, `Inverted`), locations, filler `RandomAssumed`, `Rom.php` writer |
| [KatDevsGames/z3randomizer](https://github.com/KatDevsGames/z3randomizer) | The base ASM every ALttPR ROM is built on, assembled with asar onto the JP 1.0 ROM | MIT | **The base patch.** Built once at the commit that matches `Rom::BUILD = '2024-02-18'` (`Rom::HASH edc01f3d…`), then embedded as data |
| [Zarby89/Enemizer](https://github.com/Zarby89/Enemizer) | Boss, enemy, damage and health shuffle (C#, the original). alttpr.com runs an external binary built from its forks: `z3/enemizer_linux` 6.1.0.180 from cassidoxa/Enimizer, the macOS one from tcprescott/Enimizer (`composer.json`) | MIT (the forks declare no licence) | Later phase, ported from Zarby89's MIT code |
| [cassidoxa/ALttPEntranceRandomizer](https://github.com/cassidoxa/ALttPEntranceRandomizer) | The entrance shuffle alttpr.com runs (Python), pinned in its `composer.json` as `z3/entrancerandomizer` 31.2 at commit `8a1ac4d4` | MIT | Later phase, maybe never |
| [aerinon/ALttPDoorRandomizer](https://github.com/aerinon/ALttPDoorRandomizer), [codemann8 OWR](https://github.com/codemann8/ALttPDoorRandomizer) | Door and overworld shuffles (Python) | MIT | Out of scope (see Not doing) |

Facts that shape the port:

- **alttpr.com seeds are not reproducible from a number.** `get_random_int()` calls PHP's `random_int`, a CSPRNG
  (`app/Helpers/number.php`); a seed lives as its stored patch, found by hash. So the C port needs its own seeded PRNG,
  unlike the SMZ3 port, which reproduces .NET `Random` and matches the C# seeds; seeds made here never match alttpr.com seeds. Everyone on the same
  version who enters the same number gets the same seed, which is all the ladders need.
- **The "hash" is five icons on the file select screen.** `Rom::setStartScreenHash()` writes five icon indices at
  `0x180215`. Racers compare them to know they play the same seed. Z3 runs show it too.
- **The ROM is 2 MB LoROM** (`Rom::SIZE = 2097152`), from the JP 1.0 ROM SMZ3 already asks for.
- **Options on alttpr.com:** world state Standard, Open, Inverted, Retro; goals Defeat Ganon, Fast Ganon, All Dungeons,
  Pedestal, Triforce Pieces, Ganonhunt, Completionist; tower and Ganon crystals 0 to 7 or random; dungeon items
  Standard, Maps/Compasses, Maps/Compasses/Small Keys, Keysanity; accessibility 100% Inventory, 100% Locations,
  Beatable; item placement Basic or Advanced; swords Randomized, Assured, Vanilla, Swordless; item pool and functionality
  Normal, Hard, Expert; hints on/off; glitches None, Overworld Glitches, Hybrid Major Glitches, Major Glitches, No Logic;
  Enemizer (boss, enemies, damage, health); entrance shuffle (None to Insanity); tournament mode, which in the public
  code writes the tournament flag (`setTournamentType`, two bytes at `0x180213`) and hides the spoiler; its
  `rummageTable()` calls `Tournament::apply()`, an empty stub in the repo, so any extra obfuscation alttpr.com does is
  not public.
- **Racing rules** ([racetime.gg/alttpr](https://racetime.gg/alttpr)): no save states, no fast forward, no ROM or RAM
  inspection beyond autotracking; only the "Competitive" glitches are allowed in no-glitch races (fake flippers, dark
  room navigation, bunny revival). Time stops when the mosaic before the Triforce room starts; on a pedestal goal, when
  the pedestal animation ends. Save & Quit is a normal mechanic (back to Link's House, Sanctuary or the Pyramid); no
  written rule bans it.
- **What is raced now:** the step ladder ([racetime.gg/alttpr-ladder](https://racetime.gg/alttpr-ladder)) cycles presets
  such as Open, Casual Boots, AD Keys, Crosskeys (crossed entrances plus keysanity), "Ludicrous Speed" and Open 7/7
  Spoiler; tournaments in 2026 include the ALTTPR Main Tournament, the Challenge Cup and the Crosskeys Tournament.

### Super Metroid

| Project | What it is | Licence | Use here |
|---|---|---|---|
| [tewtal/SMZ3Randomizer](https://github.com/tewtal/SMZ3Randomizer) `Randomizer.SuperMetroid` | The SM-only generator samus.link serves ("Super Metroid Item Randomizer" v3.1), same author and same architecture as the combo SMZ3 code already ported | MIT | **The source to port.** `World.cs`, `Regions/`, `Filler.cs`, `Patch.cs`, `Config.cs` |
| [tewtal/sm_randomizer_rom](https://github.com/tewtal/sm_randomizer_rom) | The base ASM of that generator (a submodule of the repo above) | MIT | **The base patch** |
| [blkerby/MapRandomizer](https://github.com/blkerby/MapRandomizer) | Map Rando (Rust): rebuilds the map, 12 skill tiers, objectives, door modes. Almost every SM race on racetime in November 2026 (296 of 300 sampled) | MIT, logic data [sm-json-data](https://github.com/vg-json-data/sm-json-data) CC BY 4.0, submodules not checked | Rust crates built for each host ([Map Rando](#map-rando)) |
| [theonlydude/RandomMetroidSolver](https://github.com/theonlydude/RandomMetroidSolver) (VARIA) | Python: area, boss, door colour and escape rando, skill presets, objectives, solver | MIT | Ideas only |
| [dashrando/dash-randomizer](https://github.com/dashrando/dash-randomizer) (DASH) | TypeScript, derived from Total's randomizer | MIT | Ideas only |

Facts that shape the port:

- **It draws from .NET `Random`** (`Randomizer.cs`: `new Random(randoSeed)`, then `new Random(patchSeed)` for the ROM),
  which `core/gen/smz3_rng.c` already reproduces exactly. Unlike Z3, this port can aim for the same seed as samus.link's
  SM generator, checked against the C# like every SMZ3 stage.
- **Its options are few:** Logic Casual or Tournament, Placement Full or Split (major/minor), Goal Defeat Mother Brain,
  Race (no spoiler). Game mode Multiworld is not needed.
- **ROM writes land in LoROM space up to 0x2FFFFF** (item table at `0x1C6000`, seed data at `0x1C4F00`, seed hash at
  `0x2FFF00`, title at `0x007FC0`), so the ROM is at least 3 MB; the exact output size is read from the built base patch.
- **Racing rules** ([racetime.gg/smr](https://racetime.gg/smr)): no out of bounds, underflow, wrong warps, memory
  corruption or fake items; no turbo or macros. Time stops when Samus loses control at the ship (the convention; not
  written on racetime).

### SMZ3 itself

`TheTrackerCouncil/SMZ3Randomizer` (in `vendor/`) is combo only: no single-game ROM layout, ASM or generator. The SM-only
code lives only upstream in tewtal's repo.

## Decisions

the owner's answers of 2026-11-24 are folded in.

| Topic | Proposal |
|---|---|
| Names | SMZ3, A Link to the Past, Super Metroid. Short ids in data: `smz3`, `z3`, `sm`. |
| Where the game is picked | The first screen is a game carousel: the three games turn on a ring with depth (the front one large, the other two smaller and behind), left/right turns it, A enters. A game whose ROM is missing shows dimmed and asks for the ROM when entered. Entering a game opens that game's own run list (its slots), then Cozy / Original / Custom as today; its ladders, daily and championship races hang from that game's screens. Back from the run list returns to the carousel, which remembers the last game. |
| ROMs | Each mode needs only its own ROM: Z3 the ALttP JP 1.0, SM the Super Metroid JU, SMZ3 both, with the same MD5s. |
| Z3 generator | A C port of alttpr.com's PHP generator with its own seeded PRNG, on the z3randomizer base patch. Seeds do not match alttpr.com; the hash icons do show. |
| SM generator | A C port of tewtal's `Randomizer.SuperMetroid`, seed for seed with the C# where the RNG allows. |
| Sharing code | New `core/z3gen/` and `core/smgen/` beside `core/gen/`. RNG, MD5, IPS, text compression, sprite writing and the logic engine move to `core/common/` only where both games need them. The SMZ3 port does not change: its ROMs stay byte-identical. |
| Slots | Per game: 32 runs on the Thor and PC and 4 on the 3DS for each of the three games, plus that game's daily. |
| Ladders | Per game: Original rules, Monthly seed, Weekly run, Daily challenge, Championship races. No table mixes games. |
| Original rules | Z3: alttpr.com's default preset (`config/alttp.php`, preset `default`): Open, Defeat Ganon, 7/7 crystals, Standard dungeon items, 100% Inventory, Advanced placement, Randomized swords, Normal pool, no glitches, hints on. SM: Tournament logic, Split placement. SMZ3 unchanged. |
| Ladder presets | Besides Original, each game gets the presets its community races, each with its own table, built from the community's own definitions (SahasrahBot's `presets/alttpr/*.yaml` for Z3, maprando.com's presets for Map Rando). Counted on 2026-11-24 from racetime race descriptions (the last 1000 alttpr races, 26 Sep to 24 Nov 2026; the last 300 smr races, November 2026). Z3 at launch: Casual Boots (Standard, assured sword, Pegasus Boots; the most raced preset besides tournament formats), Standard, AD Keys (All Dungeons, full keysanity). Z3 later, as each option is ported: Inverted, and Enemizer (the most mentioned of all, needs boss and enemy shuffle). Crosskeys is raced a lot too but needs entrance shuffle, which is not planned. Fast Ganon stays the weekly's goal, without a table (barely raced). SM: the community races only Map Rando (every smr race sampled: the Season 5 league settings, its Item Draft matches, and a few Beginner/chill ones). Tables: Map Rando Season 5 and Map Rando Beginner. tewtal's generator keeps Original (Tournament, Split) as the classic, without preset tables of its own, since nobody races its other options. Item Draft (each player drafts starting items) is a 1v1 format, a candidate for the races. SMZ3: the tables it has today. Custom stays off the ladders. Built and deployed on 2026-11-24 (see Online, Server, Preset tables). |
| Online rules | Same as SMZ3 today, in all three games: no ladder, monthly, weekly, daily or race run allows save states or rewind. Fast forward is allowed on ladder, monthly and weekly runs because the run clock counts the time the game would have taken at normal speed (PC passes the factor to `smz_tracker_tick`, `frontends/pc/pc_main.c:2357`; the 3DS counts fast-forwarded frames at the SNES rate; Android scales the clock with the speed), so it gives no time back. Daily and race runs block it (the `challenge` flag) because their clock is the real one. The racetime ban on fast forward exists because races there are timed in real time; it does not apply to this clock. Z3 allows Save & Quit, SM allows saving and reloading at stations: both are part of the game. |
| End of a run | Z3: the mosaic before the Triforce room; pedestal goal at the end of the pedestal animation; triforce hunt and Ganonhunt where the game ends them. SM: Samus loses control entering the ship. Read from memory, never from a button. |
| Hash on screen | Z3: the five file select icons, also shown in the run row and the race ready room. SM: the seed hash the base patch draws, if it draws one, else the tracker shows a name built from the seed. |
| Tournament mode | Every online run of Z3 is built as a tournament ROM (tournament flag set, no spoiler). SM online runs set Race. Spoilers exist only for offline runs, after the run ends. |
| Cozy | Per game, the same idea: Z3 Open with Assured sword, map/compass shuffle off, dark rooms lit; SM Casual logic; states and rewind on. Stays on the device. |
| World goals | Each game has its own goals each month: SMZ3 from every group, Z3 from zelda and shared, SM from metroid and shared (built 2026-11-25). One rupeergy balance. |

## How it fits together

```
 core/ui (shared menus, runs, tracker, proof, daily, race)     -- gains a `game` everywhere
        |
 dispatch: rando_generate(game, settings, roms) -> ROM bytes, spoiler, hash
        |                       |                        |
 core/gen (SMZ3, unchanged)  core/z3gen (port of alttpr)  core/smgen (port of Randomizer.SuperMetroid)
        \______________________ core/common: rng, md5, ips, text, sprites, logic helpers ______/
        |
 hosts: PC (pc_*), Android/Thor (Lemuroid patch + JNI), 3DS (snes9x_3ds patch)   -- ROM size and memory map by game
        |
 server/ladder: every endpoint and record carries `game`; smz3check/smz3daily dispatch the same way
```

### The `game` field

This change reaches every layer. A missing `game` means `smz3`, so every old run, token, ladder record, daily and race
keeps working.

- Run settings JSON (`ui_settings_json`, `core/ui/smz3_ui_opts.c:216`) gains `"game"`. `smz_run_rom_name`
  (`core/ui/smz3_run.c:60`) hashes it, so the three games never share a ROM file name, and the name prefix becomes
  `smz3_`, `z3_` or `sm_`.
- `SmzRun` (`core/ui/smz3_run.h:7`) gains `int mode` (which randomizer); the existing `game` field stays "which game
  is on screen". `smz_run_summary` prints only the game's own count for Z3 and SM.
- `UiSlot.online` kinds (`smz3_ui.h:53`) stay; the game rides beside them.
- `ui_run_ladder` (`smz3_ui.c:948`) and `ui_is_default` compare against the chosen game's defaults or one of its
  ladder presets.

## Generators

### Super Metroid (`core/smgen/`), first

Smaller and closest to what exists, so it goes first and proves the dispatch, the hosts and the server with one new
game before Z3 doubles the work.

1. **Oracle.** `tools/gen-cli` gains a second project referencing tewtal's `Randomizer.SuperMetroid` (vendored at a
   pinned commit, with `sm_randomizer_rom` as its submodule), with the same verbs SMZ3 has: `vectors`, `worldvectors`,
   `placementbatch`, `schema`.
2. **Logic and world.** `tools/port_world.py` and `tools/cs_regions.py` learn the SM-only `Regions/` (Crateria,
   Brinstar, Norfair Upper and Lower, Wrecked Ship, Maridia) and its `Config` (Casual/Tournament) into
   `core/smgen/sm_world_gen.{h,c}`. Check: reachable sets equal the C# for random inventories.
3. **Pools and filler.** Port `Filler.cs` on `smz3_rng`. Check: placements equal the C# for random seeds and settings.
4. **ROM.** Build `sm_randomizer_rom` once (its own build script, asar), store the result as an IPS against the SM JU ROM
   in `patches/` and embed it as data, like the Cas' IPS today. Port `Patch.cs`: item table, PLMs, seed data, title,
   seed hash, the animal patches. Check: byte-identical ROM to the C# for the same seed.
5. **Options table** from the C# attributes, as `gen-cli schema` does for SMZ3.

### A Link to the Past (`core/z3gen/`)

The big one: 51 region files, three world states, glitched logic in the same classes, and a PHP oracle instead of C#.

1. **Oracle.** The PHP generator at a pinned commit in a local container, plus a small PHP command line next to it
   that answers the same questions as `gen-cli`: reachable locations for an inventory and settings, the item pool for
   settings, and a ROM for a given placement (through the Customizer path, so placement and ROM writing are checked
   apart).
2. **Base patch.** Build z3randomizer at the commit matching alttpr's `Rom::BUILD` with asar, check the build hash
   against `Rom::HASH`, store it as an IPS against the JP 1.0 ROM, embed as data. The ROM writer's offsets come from
   `Rom.php`, so the two must stay at the same build.
3. **Logic and world.** A PHP twin of `port_world.py` (`tools/port_world_php.py`) transpiles `app/Region/**` and the
   location requirement closures. Start with Open and Standard, No Glitches; Inverted and Retro next; glitched logic
   (Overworld, Major) after the Z3 mode has shipped. Check: reachable sets equal the PHP for random inventories and
   settings.
4. **Item pools and filler.** Port `RandomAssumed` and the pool building on a new seeded PRNG (`core/common`). Since the
   RNG differs, the check is not seed parity: every placement must be beatable under both the C and the PHP logic
   (the PHP reads the placement back through its own playthrough), and the pool counts must match exactly.
5. **ROM writer.** Port the `Rom.php` setters the options need: items, prizes, medallions, crystals and goals, world
   state, swords, item functionality, hints and texts, tournament mode, hash, starting equipment, fairy ponds, cosmetic
   options (heart beep, menu speed, quickswap). Check: for one placement, the C ROM equals the PHP ROM byte for byte,
   texts with random variants aside (as SMZ3).
6. **Sprites.** The 517 Link sprites are already shipped; their offsets move from the combo layout to the standalone
   one in `smz3_sprite.c`. alttpr's `.zspr` files load too, so a player's own sprite works as on alttpr.com.

### Hints, plando, spoilers

- **Hints** (`smz3_hints.c`): the tracker hints (where an item is, checks left in a region) work per game. Z3 also keeps
  the game's own hint tiles when hints are on, since that is an alttpr option.
- **Spoiler log**: new for all three games, written after the run ends (or at once on an offline run that asks for it),
  never on online runs before the finish.
- **Plando**: SMZ3 keeps its YAML; Z3 reads alttpr's customizer JSON; SM has none upstream and gets none.

## Hosts and emulator

Every place in `docs/` and the code that assumes a 6 MB combo ROM branches on the run's mode:

| Assumption today | Where | Change |
|---|---|---|
| ROM is 6 MB | `SMZ_ROM_SIZE` (`smz3_patch.h:7`), `SMZ_HOST_ROM_SIZE` (`smz3_host.h:9`), `pc_main.c` writes and the size check at :1862, 3DS `smz3run.cpp` :139/805/868/905, JNI :328, `RunGenerator.kt:243` | Size from the generator's result |
| Both ROMs needed | `smz_generate(sm, z3)`, Android `findBaseRoms`, the 3DS and PC ROM search | Only the mode's own ROM |
| Game on screen from SRAM 0x33FE | `smz3_state.c:5` and everything that calls it | Fixed by the mode in Z3 and SM |
| End of run from the combo flags SRAM 0x3402/0x3506 | `smz3_state.c:36-44` | Per mode, from the standalone ROM's memory (to be found in each base ASM) |
| Checks from WRAM 0x7EF000 (Zelda) and 0x7ED870 (Metroid) | `smz3_state.c:62-91` | Same WRAM in vanilla, so the reads stay; the SRAM copies move to each standalone layout |
| Boss counters read the other game's parked copy in combo SRAM (`room*2+1` for Zelda, `0x2078+area` for Metroid) | `smz3_counters.c:57-69` (`bosses_beaten`) | Z3 and SM read only their own game, from WRAM; the SRAM branch is SMZ3 only |
| Combined offsets in the ROM | sprites, shortcut hooks, `smz_rom_start_sm/z3`, checksum at 0x40FFB0, light dark rooms, SM default controls | LoROM offsets per standalone ROM, or the option is dropped where the base patch has its own |
| Hooks need the 6 MB ROM | 3DS `smz3run.cpp:905`, shortcut hooks | Shortcuts per game need hooks in each standalone ROM: Z3 has its own (`z3_rom_shortcut_hooks`), SM's row stays off until it gets one |

What stays as it is: save states and rewind (`smz3_rewind.c`), the `challenge` flag that blocks them, the enemy, kill
and rupee counters (`smz3_counters.c:82-106` reads only vanilla WRAM, so Zelda and Metroid enemies count the same),
the discovery map logic.

### Tracker

- **Map:** the combined map's Zelda or Metroid half alone, at the same scale, with the game's markers. Z3 locations get
  markers matched by name to the 216 Zelda ones in `data/map_markers.json`; those alttpr has and SMZ3 lacks get new
  points. SM uses the existing 100.
- **Dungeon maps and keys** (Zelda) work as in SMZ3, read from the JP ROM.
- **Logic in reach** calls the mode's own `reachable`.
- **Items view:** Z3 adds what alttpr has and SMZ3 does not (progressive gloves and swords shown as alttpr does, the
  bottle contents, the Triforce piece count); SM shows only Samus's items.
- **Hash** in the tracker header for Z3 and SM.

## Online

### Server (`server/ladder/`)

- **Tokens** add the game: `id.install.kind.period.startedAt.game.sig`. A token with no game segment is `smz3`.
- `POST /api/start {install, kind, game}`; `GET /api/ladder?game=&kind=&preset=`; `/api/monthly?game=` and
  `/api/weekly?game=`. A request with no game is `smz3`, so released apps keep working.
- **Seeds:** `seedOf(period)` stays for SMZ3; Z3 and SM use `hmac('seed:' + game + ':' + period)`, so the three games
  never share a seed number by accident.
- **Records:** `{game, kind, preset, ...}`. Old records read as `smz3` with no preset.
- **Server-side rules check:** the client still decides eligibility, but the finish now carries the settings hash, and
  the server refuses a ladder finish whose hash is not the one of the game's Original or of a listed preset. Today it
  trusts `defaultRules`; three games make that worth fixing.
- **Proof checker:** `smz3check` dispatches on `game` to the mode's logic. The rules stay (12 checks at least, time
  order, out-of-logic share, first check time, rate), with per-game numbers: SM runs are shorter, so `MIN_MS` and the
  minimum check count become per game and per kind.
- **Weekly** keeps its short goal per game: Z3 Fast Ganon with 2 crystals; SM a Split seed (its only goal is Mother
  Brain).
- **Daily challenge:** each window gets one challenge per game, picked and planned the same way (`smz3daily` gains the
  two planners). The run list shows the three dailies; each has its own table and the same rupeergies. Goals per game:
  Z3 a pendant, a crystal, a dungeon, an item, a place; SM a boss, an item, an area.
- **Races:** a room carries its game. Championship Full and Partial per game (Z3 Partial: Fast Ganon 2 or 4 crystals;
  SM Partial: none, since the base patch has one goal). Draft goals per game.
- **World goals** (built and deployed 2026-11-25): each game has its own month, keyed `YYYY-MM` for SMZ3 and
  `z3:YYYY-MM` / `sm:YYYY-MM` for the others. `GET /api/goals?game=` picks the game; a report's game is its run id's
  first letter (`z`, `m`, else SMZ3). Z3 draws from zelda and shared, SM from metroid and shared; `swaps` is SMZ3's
  only. The `daily` goal counts that game's daily windows. Payouts go to the one gem balance.
- **Web** (`smz3.com/ladder`): a game switch at the top of every table.
- **Preset tables** (built and deployed): `PRESETS` in `server.js` lists each game's presets with the settings the
  proof checker regenerates the seed from. `/api/games` lists them; `/api/start` takes `preset` for an original run
  only and adds it to the token (`...startedAt.game.preset.sig`, so SMZ3 and Original tokens are unchanged);
  `/api/ladder?preset=` reads one table; records carry `preset`. The web page shows the tables as buttons in the
  Original section. Z3: `casual_boots` (Standard, assured sword, hints off, boots from the start, as SahasrahBot's
  `casualboots.yaml`), `standard`, `ad_keys`; SM: `mr_s5`, `mr_beginner`. The clients send the preset when the run's
  rules are exactly a preset's (`ui_run_preset`), and the ladder screen's first tab cycles the tables with A.

### Clients

- The ladder screen opens inside the chosen game: Original, presets, Monthly, Weekly of that game only.
- The race lobby, entered from a game, lists only that game's rooms.
- Every online call sends the game.

### New version notice

It stays as it is and moves to the first screen: at start every version asks `/api/version`, and when a newer
release is out the carousel says so, with Options > Update.

- **PC (Windows, macOS, AppImage) and Android/Thor** download and install the release themselves, as today.
- **Linux built from source** says where to get it.
- **3DS** today only says where: `smz3.com/download`, which the server redirects to the latest
  release. Step 1b makes it download itself: the `.3dsx` written over the running file (its path from `argv[0]`),
  the `.cia` downloaded and installed through the AM service (as FBI does), then a restart. If the console refuses,
  the link stays.

## Community options, by phase

### Z3

| Phase | Options |
|---|---|
| First release | World state Open and Standard; goals Defeat Ganon, Fast Ganon, All Dungeons, Pedestal; crystals 0-7 and random; dungeon items all four; accessibility all three; placement Basic/Advanced; swords all four; item pool and functionality Normal/Hard/Expert; hints; tournament mode; hash; sprites; heart beep, menu speed, quickswap |
| Next | Inverted, Retro, Triforce Pieces, Ganonhunt, Completionist; the customizer (plando) |
| Then | Overworld Glitches and Major Glitches logic (in the same PHP regions); No Logic |
| Later | Enemizer: boss shuffle (a table and a few writes), then enemy, damage and health shuffle (a larger port of Zarby89's C#). Enemizer is the most mentioned format in recent alttpr races, so it gets its table once both are in |
| Not planned | Entrance shuffle (see Not doing) |

### SM

| Phase | Options |
|---|---|
| First release | Logic Casual/Tournament, placement Full/Split, Race, hash, Samus sprites |
| Next | Whatever quality-of-life patches `sm_randomizer_rom` already carries, exposed as options if they are switches |
| After the base generator | Map Rando, on the device (below) |

### Map Rando

Map Rando rebuilds Super Metroid's map, and it is what SM racers play. It runs on the device, as the other generators
do, but it is not ported to C: its Rust crates (`rust/maprando`, `maprando-game`, `maprando-logic`) are built for each
host and called through a small C interface, the way `maprando-wasm` wraps them for the browser.

- **Build:** Rust joins the toolchain. PC and Linux handhelds build natively; Android through the NDK targets; the 3DS
  through the tier 3 target `armv6k-nintendo-3ds` (nightly, `build-std`, devkitARM's libctru underneath).
- **Measured** (2026-11-24, one seed, Season 5 and Default presets): PC 5 to 10 s and 740 to 870 MB peak; the Thor
  23 to 40 s and 820 MB, giving the same ROM byte for byte as the PC.
- **Data:** for now the hosts load upstream's own files, packed by `tools/maprando/fetch.sh` (27 MB, 9 MB compressed:
  the logic JSON, presets, IPS and Mosaic base patches, title images and 20 batches of each shipped map pool); a
  compact converted file stays the plan for smaller hosts. Its build only
  downloads the map pools' manifests; a device fetches the one map a seed picks, or ships a fixed subset. Either
  way every device sees the same pool, or the same seed number would pick different maps.
- **Where it runs:**
  - PC, Linux handhelds, Android and the Thor: yes.
  - New 3DS: does not fit as the crate stands, so it takes the server's seed (below) like the old 3DS. Measured on 2026-11-24 (pinned commit `8adf9c9`, `core/maprando`): the
    loaded data takes about 270 MB (`GameData`) and 600 MB once the link data is built, and a seed peaks at 740 to 870 MB,
    while the whole New 3DS has 256 MB (about 124 MB for an app, 178 MB in its extended mode). It goes the old 3DS's way.
  - Old 3DS (268 MHz, less memory): out of reach for the same reason. Making the crate fit (lighter data, fewer
    attempts per seed, maps fetched one at a time) stays step 10.
  - Both 3DS models today: the server runs the same crate, patches a vanilla SM ROM it keeps and sends a delta BPS
    patch (`GET /api/maprando/seed`), which the console applies to the player's ROM with `core/common/smz3_bps.c`
    after stripping a copier header. Map Rando is offered only with a connection; the errors say which of no
    connection, server busy (503), too many requests, a ROM that is not the vanilla one (CRC) or a damaged patch.
- **Seeds:** Map Rando draws from Rust's `StdRng` seeded from a number (`rust/maprando-web/src/main.rs`), so the same
  settings and seed number give the same seed on every device built from the same pinned crates, which the monthly,
  weekly and daily need. Its site caps a seed's attempts by time (60 seconds, `attempts_timeout`); the devices cap
  them by count instead, so a slow console reaches the same seed as a fast one instead of giving up earlier.
- **Ladders:** Map Rando Season 5 and Map Rando Beginner are SM preset tables (see Ladder presets, built and
  deployed). The monthly and weekly stay classic seeds for now.
- **Proof:** the server checks a Map Rando finish against the seed's own logic (built 2026-11-25).
  `smz3-maprando --check` rebuilds the seed (or reads its cached randomization), replays the hub, and walks the run's
  checks in order with Map Rando's traversal; `maprando.js` runs it one at a time and keeps the randomization per
  settings and seed. A run with over 1 in 10 checks out of logic is held, as in the other games; a finish waits 6 s for
  the answer. The time and rate rules stay.
- **Save and items:** Map Rando keeps 0xA00-byte save slots at $0010, $0A10 and $1410 (classic SM: 0x65C), items at
  +0xB0; the hosts pass `SMZ3_MAPRANDO` with the mode. Checked on a generated ROM: the 100 item PLMs keep bit index =
  location id, so the tracker's location table holds.
- **End of run:** Map Rando's `hook_samus_enter_ship` tests event 0x0E and the gunship code at $A2:AA09 still sets
  $0FB2 to $AA4F, so the classic SM check (event 0x0E and $0FB2 == $AA4F) holds unchanged.
- **Licences:** Map Rando is MIT (Copyright (c) 2023 maddo, kyleb); sm-json-data is CC BY 4.0. Both are credited on
  every host's Credits screen and in `THIRD_PARTY.md`.
- **Tracker map:** a Map Rando run's map tab shows the game's own pause map, built from the running game by
  `core/ui/smz3_smmap.c` the way `map_area.asm`, `map_progress_maintain.asm`, `item_dots_disappear.asm` and
  `special_door_reveal.asm` build it: the area tilemaps ($82:964A), tiles ($E2:8000) and pause palette of the ROM,
  explored ($07F7, $7E:CD52), revealed (SRAM $2000) and partial (SRAM $2700) bits, item dots ($83:E000 against
  $7E:D870), the white corners round Samus's tile. It follows Samus's area; AREA (PC `d`, pad Y) steps through the
  areas she has been in (SRAM $2600). Not drawn: the pause screen's sprite icons (ship, bosses) and the room name.
  Checked on 2026-11-24 against the pause screen on PC and on the Thor's bottom screen, over several rooms. Classic
  SM keeps its own map: its ROM has no such tilemaps, and `smz_smmap_rom_ok` refuses it.

#### On each host (step 9)

- **Run settings:** an SM run picks its generator, Classic (tewtal's, the default) or Map Rando, with a preset
  (Beginner = upstream's Default, Season 5 = Community Race Season 5, Mentor Tournament, Expert Challenge) and a
  skill level (the preset's own, or Basic to Beyond). The run's JSON gains
  `"sm":{"generator":"maprando","preset":"<upstream name>"[,"skill":"<name>"]}`; a classic run's JSON and ROM are
  byte-identical to before. The mode screen has a Map Rando row that starts a run with Map Rando picked.
- **PC:** the Makefile builds `core/maprando` with cargo (target dir `build/maprando-target`, MinGW target on
  Windows) when `vendor/maprando` and cargo are there. The data is looked up in `../share/smz3/maprando-data`
  (AppImage), `maprando-data` and `../maprando-data` beside the executable. Generation runs on a thread, with
  "Generating Map Rando seed... try N" and the seconds.
- **Android and the Thor:** CMake runs `cargo ndk` for arm64-v8a and links the crate into `libsmz3core.so`; the data
  ships as `assets/maprando/data.zip` and is unpacked once (a stamp says when to unpack again), then the seed is
  generated in the background with the attempt count on screen.
- **3DS:** the server's BPS patch, as above, with a download progress bar.

## Work in order

Each step ships on every version (PC, Linux handhelds, Android and the Thor, 3DS) before the next starts, as with races.

1. **The `game` field** through runs, settings, ROM names, the hosts' ROM sizes and ROM search, and the server, with
   nothing new to pick yet. Check: SMZ3 ROMs byte-identical, old runs, tokens and records still load.
1b. **3DS self-update** (see New version notice).
2. **SM generator** (stages 1-5 above).
3. **SM on the hosts:** the game carousel and per-game run lists, single ROM, tracker half map, end of run, hash, sprites. Offline only.
4. **SM online:** ladders, presets, monthly, weekly, proof checker, web switch. Then the SM daily, races and goals.
   Deployed: Original, plus the Map Rando Season 5 and Beginner tables; weekly a Split seed, monthly Original; proof
   minimum 10 checks and 5 minutes; daily ids `sm-<date>-<n>`; races Championship Full only (see DAILY.md, RACES.md).
5. **Z3 base patch and oracle** (stages 1-2 above).
6. **Z3 generator** for the first-release options (stages 3-6).
7. **Z3 on the hosts** and **Z3 online**, as steps 3 and 4.
   Hosts done, offline (see Z3.md, step 7): PC, 3DS and Android generate and track Z3 runs. A single-game run shows
   nothing of the other game on the second screen: no portals on its half of the map, no Dungeons button in SM, no
   other game's bosses, crystals, counters or items (SMZ3 54, SM 21, Z3 33), no shortcut rows in SM.
   Online done and deployed: ladders, Original monthly, a Fast Ganon 2-crystal weekly, the proof checker on z3gen's
   logic, the daily (`z3-<date>-<n>`), Championship rooms and world goals; every online Z3 run is a tournament ROM.
8. **Z3 next options**, then glitched logic.
   Generator done (`core/z3gen`, see Z3.md): Inverted, Retro, Triforce Hunt, Ganonhunt, Completionist and the five
   logics, byte-identical to alttpr; offered in `Z3_OPTIONS` and on every host's Custom screen. Boss shuffle places bosses but
   waits for the Enemizer (step 9) to write them; the customizer is not started.
9. **Map Rando** on every host but the old 3DS; then the Enemizer boss shuffle.
   Done (see Map Rando, On each host): PC, Linux handhelds, Android and the Thor generate on the device; both 3DS
   models take the server's BPS patch, with a connection. The ladder tables are built (Online, Server, Preset
   tables). The Enemizer boss shuffle is in `core/z3gen` (Z3.md).
10. **Map Rando on the old 3DS**, after everything else; the server and BPS patch only if it cannot fit.
    Today both 3DS models use the server's BPS patch (step 9); running the crate on the console is still open.

## Not doing

- **Door and overworld randomizers.** Large, fast-moving Python projects; a C port would never keep up.
- **Entrance shuffle** in the first rounds: it reshapes the logic engine, and crossed keysanity (the Crosskeys
  tournament) depends on it. Reconsider after glitched logic.
- **VARIA and DASH ports.** Neither is where SM racing is now; their ideas (presets, quality of life) can be borrowed
  as options where the SM base patch allows.
- **Matching alttpr.com seeds.** Not possible: its generator draws from a CSPRNG.

## Licences

Everything ported is MIT, which needs the copyright notice kept: sporchia/alttp_php_randomizer, KatDevsGames/z3randomizer,
tewtal/SMZ3Randomizer (`Randomizer.SuperMetroid`), tewtal/sm_randomizer_rom, Zarby89/Enemizer (Copyright (c) 2023
Zarby89; its boss shuffle and base patch are in `core/z3gen/z3_enemizer*`, ported from the cassidoxa/Enimizer fork alttpr
runs, which declares no licence of its own). A `THIRD_PARTY.md` lists them with their notices, and the Credits screen
names them. Map Rando's logic data is CC BY 4.0 and needs attribution; its submodules have their own licences, to be
read before any decision. The base patches carry reassembled game code, the same position SMZ3's patches are in today;
no ROM is shipped.

## Risks

- **Z3 size.** The PHP generator is several times SMZ3's Zelda side. The transpiler is what keeps it bounded; if the
  closures resist it, the hand port grows.
- **PHP oracle.** It needs a pinned PHP and Laravel in a container just to run its classes; that setup comes first.
- **The repo is ahead of the site.** The repo's `updates.blade.php` lists v31.2.2 (February 2024), which alttpr.com
  does not show; the port follows the repo, so a seed's behaviour can differ in small ways from what alttpr.com serves.
- **z3randomizer build match.** If the commit for build `2024-02-18` cannot be pinned, the ROM writer follows a newer
  z3randomizer and the oracle comparison loses its reference.
- **3DS memory.** Z3 (2 MB) and SM (at least 3 MB) are smaller than SMZ3's 6 MB, so they fit where SMZ3 fits.
- **End of run in memory.** The combo ASM sets flags SMZ3 reads; the standalone ROMs need their own end signals,
  found in each ASM before the hosts can stop the clock.
- **Ladder trust.** Adding the settings hash check on the server changes what a finish sends; old apps must still be
  accepted for SMZ3 until they update.

## Done on 2026-11-25

- **Map Rando proof** checked against the seed's logic on the server (see [Map Rando](#map-rando)), and Map Rando in the
  macOS CI build (`fetch.sh` copies the logic data with tar, the tarball ships `maprando-data`).
- **Map patches:** single-game maps no longer show SMZ3's portals. SM's four triforce sites are redrawn from the
  vanilla pause map; ALttP's four helmets are replaced with the overworld under them. `tools/map_portals.py` builds
  `data/map_patches.*` once. The PC map opens on the game's own half.
- **Inverted ALttP:** Link's House marker sits on the Dark World's bomb shop, and the position dot inside the house is
  in the Dark World (`smz3_marker_dy`, and Android's own copy in `Smz3Discovery.kt`).
- **World goals per game** (see Online, Server).
- **Menu music:** `assets/music/smz3_intro.mp3` (Doctor Utsugi, made with Suno) plays at half volume on the launcher's menus
  and fades out over half a second when a game starts; Options > Menu music turns it off. PC and 3DS decode it with
  `dr_mp3` (3DS on ndsp channel 5), Android plays it with `MediaPlayer`.
- **Commits** go from the repo root: the vendor trees are carried as `patches/*.patch`, and a commit from inside
  `vendor/` lands in the vendor's own repository instead.

## Settled on 2026-11-24

1. **Game picker:** a carousel of the three games on the first screen; each game has its own runs, ladders and races.
2. **Z3 Original rules:** the classic randomizer, alttpr.com's default preset. Z3 also gets Cozy.
3. **Dailies:** each game keeps the three windows a day it has now.
4. **Map Rando:** on the device, New 3DS included; the old 3DS shows it as not compatible until it is made to fit, with
   the server as the last resort (see [Map Rando](#map-rando)).
5. **Presets:** what each community races, as listed in Ladder presets.
6. **3DS slots:** 4 per game.
7. **Fast forward:** no change. The run clock already discounts it, so all three games follow SMZ3's rule.
