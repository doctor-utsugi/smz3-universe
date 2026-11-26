# Third-party code and data

SMZ3 Universe ports, links or ships parts of these projects. Each keeps its own licence; the notices below are the
ones their repositories carry. The Credits screen (Options > Credits) on every host names them too.

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
| [TheTrackerCouncil/SMZ3CasConfigs](https://github.com/TheTrackerCouncil/SMZ3CasConfigs) | Tracker configs | as published by their authors |

## Emulation

| Project | Used for | Licence |
|---|---|---|
| [Snes9x](https://github.com/snes9xgit/snes9x) (libretro core) | PC emulation | Snes9x licence (non-commercial) |
| [matbo87/snes9x_3ds](https://github.com/matbo87/snes9x_3ds) | 3DS emulator | Snes9x licence (non-commercial) |
| [Swordfish90/Lemuroid](https://github.com/Swordfish90/Lemuroid) | Android app | GPL-3.0 |
| [Swordfish90/LibretroDroid](https://github.com/Swordfish90/LibretroDroid) | Android emulation layer | GPL-3.0 |

## Tools and libraries

| Project | Used for | Licence |
|---|---|---|
| [RPGHacker/asar](https://github.com/RPGHacker/asar) 1.81 | Assembling the Super Metroid base patch (`tools/build_sm_ips.sh`) | LGPL-3.0, by Alcaro, based on xkas by byuu |
| [SDL2](https://www.libsdl.org) | PC window, input and audio | zlib, Copyright (C) Sam Lantinga |
| [stb_image](https://github.com/nothings/stb) v2.30 | PC image loading (`frontends/pc/stb_image.h`) | Public domain, Sean Barrett |
| rcheevos, libctru, citro3d, libpng, zlib | 3DS build (see `vendor/snes9x_3ds/THIRD_PARTY_NOTICES.md`) | MIT (rcheevos), zlib and their own licences |

## Audio

| Project | Used for | Licence |
|---|---|---|
| [mackron/dr_libs](https://github.com/mackron/dr_libs) `dr_mp3.h` (mp3-0.7.4) | Decoding the menu music on PC and 3DS (`core/common/dr_mp3.h`) | Public domain or MIT-0, David Reid |
| `assets/music/smz3_intro.mp3` | Menu music: a mashup of A Link to the Past's Dark World and Super Metroid's Brinstar | by Doctor Utsugi, made with Suno |

Super Metroid and The Legend of Zelda: A Link to the Past are (c) Nintendo; SMZ3 Universe is not made, endorsed or
supported by Nintendo. No ROM is shipped: every host builds its ROMs from the player's own copies of the games.
