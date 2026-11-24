# Third-party code and data

SMZ3 Universe ports, links or ships parts of these projects. Each keeps its own licence; the notices below are the
ones their repositories carry. The About screen on every host names them too.

## Randomizers

| Project | Used for | Licence and notice |
|---|---|---|
| [tewtal/SMZ3Randomizer](https://github.com/tewtal/SMZ3Randomizer) | SMZ3 and Super Metroid generators, ported to C (`core/gen`, `core/smgen`) | MIT, Copyright (c) 2020 Thomas Backmark |
| [tewtal/sm_randomizer_rom](https://github.com/tewtal/sm_randomizer_rom) | Super Metroid base patch | MIT |
| [sporchia/alttp_php_randomizer](https://github.com/sporchia/alttp_php_randomizer) | A Link to the Past generator, ported to C (`core/z3gen`) | MIT, Copyright (c) 2016 sporchia |
| [KatDevsGames/z3randomizer](https://github.com/KatDevsGames/z3randomizer) | A Link to the Past base patch; the hash icons in `data/icons/z3hash_*.png` | MIT, Copyright 2016, 2017 Equilateral IT |
| [Zarby89/Enemizer](https://github.com/Zarby89/Enemizer) (the cassidoxa fork alttpr runs) | Boss shuffle and its base patch (`core/z3gen/z3_enemizer*`) | MIT, Copyright (c) 2023 Zarby89 |
| [blkerby/MapRandomizer](https://github.com/blkerby/MapRandomizer) (Map Rando) | Super Metroid map randomizer, linked as Rust crates (`core/maprando`) and run by the server for the 3DS | MIT, Copyright (c) 2023 maddo, kyleb |
| [vg-json-data/sm-json-data](https://github.com/vg-json-data/sm-json-data) | Map Rando's logic data, shipped in `maprando-data` | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/): sm-json-data, developed by the A Link to the Past Randomizer & Super Metroid Randomizer communities |
| [TheTrackerCouncil/SMZ3Randomizer](https://github.com/TheTrackerCouncil/SMZ3Randomizer) | IPS patches and map icons (copied at build time) | MIT, Copyright (c) 2022 Laura Verdoes |
| [TheTrackerCouncil/SMZ3CasSprites](https://github.com/TheTrackerCouncil/SMZ3CasSprites) | Player sprites and item icons | as published by their authors |

## Emulation

| Project | Used for | Licence |
|---|---|---|
| [Snes9x](https://github.com/snes9xgit/snes9x) (libretro core) | PC emulation | Snes9x licence (non-commercial) |
| [matbo87/snes9x_3ds](https://github.com/matbo87/snes9x_3ds) | 3DS emulator | Snes9x licence (non-commercial) |
| [Swordfish90/Lemuroid](https://github.com/Swordfish90/Lemuroid) | Android app | GPL-3.0 |
| [Swordfish90/LibretroDroid](https://github.com/Swordfish90/LibretroDroid) | Android emulation layer | GPL-3.0 |

No ROM is shipped: every host builds its ROMs from the player's own copies of the games.
