/* The Enemizer's boss shuffle as alttpr runs it (Enemizer::randomize with UseManualBosses and nothing else on).
   Port of Zarby89/Enemizer (MIT; the alttpr-com branch of cassidoxa/Enimizer, built as z3/enemizer_linux 6.1.0.180). */
#ifndef Z3_ENEMIZER_H
#define Z3_ENEMIZER_H

#include <stdint.h>
#include "z3_world.h"

/* The Enemizer's seed for a seed number: alttpr draws random_int(1, 999999999); here a stream of its own. */
uint32_t z3_enemizer_seed(uint32_t seed);

/* What prepSeed's Enemizer run writes for w's bosses (cfg.enemizer_bossShuffle not none) onto rom (Z3_ROM_SIZE
   bytes, after z3_rom_write): its base patch, then the writes it logs. Returns 0, or -1 for a boss it has no data for. */
int z3_enemize(uint8_t *rom, const Z3World *w, uint32_t enemizer_seed);

#endif
