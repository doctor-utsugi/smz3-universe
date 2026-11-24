/* alttpr's InitialSram (app/Support/InitialSram.php): the save file a new game starts from, written at 0x183000. */
#ifndef Z3_SRAM_H
#define Z3_SRAM_H

#include "z3_world.h"

#define Z3_SRAM_SIZE 0x500
#define Z3_SRAM_ROOM_DATA 0x000
#define Z3_SRAM_OVERWORLD_DATA 0x280

typedef struct { uint8_t b[Z3_SRAM_SIZE]; } Z3Sram;

void z3_sram_init(Z3Sram *s);                                  /* InitialSram::__construct */
static inline void z3_sram_set(Z3Sram *s, int idx, int val) { s->b[idx] |= (uint8_t)val; }   /* setValue ORs */

static inline void z3_sram_open_aga_curtains(Z3Sram *s) { z3_sram_set(s, Z3_SRAM_ROOM_DATA + 0x61, 0x80); }
static inline void z3_sram_open_skull_curtains(Z3Sram *s) { z3_sram_set(s, Z3_SRAM_ROOM_DATA + 0x93, 0x80); }
static inline void z3_sram_open_castle_gate(Z3Sram *s) { z3_sram_set(s, Z3_SRAM_OVERWORLD_DATA + 0x1B, 0x20); }
static inline void z3_sram_open_ganons_tower(Z3Sram *s) { z3_sram_set(s, Z3_SRAM_OVERWORLD_DATA + 0x43, 0x20); }
static inline void z3_sram_open_pyramid(Z3Sram *s) { z3_sram_set(s, Z3_SRAM_OVERWORLD_DATA + 0x5B, 0x20); }
static inline void z3_sram_progress_indicator(Z3Sram *s, int v) { z3_sram_set(s, 0x3C5, v); }
static inline void z3_sram_progress_flags(Z3Sram *s, int v) { z3_sram_set(s, 0x3C6, v); }
static inline void z3_sram_starting_entrance(Z3Sram *s, int v) { z3_sram_set(s, 0x3C8, v); }
void z3_sram_swordless_curtains(Z3Sram *s);
void z3_sram_starting_timer(Z3Sram *s, int seconds);

/* setStartingEquipment(): items in item-id order (PHP walks them in insertion order; only bottle slots and
   sword/shield overwrites could tell the two apart). uncle_target is what UncleSword stands for. */
void z3_sram_starting_equipment(Z3Sram *s, const Z3Items *items, int uncle_target, const Z3Cfg *cfg);

/* Rom::writeInitialSram */
void z3_sram_write(const Z3Sram *s, uint8_t *rom);

#endif
