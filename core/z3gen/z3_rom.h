/* alttpr's ROM writer: World::writeToRom and the Rom.php setters it calls. */
#ifndef Z3_ROM_H
#define Z3_ROM_H

#include "z3_fill.h"
#include "z3_generate.h"

/* World::writeToRom on rom (Z3_ROM_SIZE bytes, base patch applied), drawing from r in the PHP's order, after
   z3_texts_randomize. Empty locations get Nothing, also in s->w.item_at, as PHP does. Returns 0, or -1 when the text
   table outgrows its space (Text::getByteArray throws). */
int z3_rom_write(uint8_t *rom, Z3Seed *s, const Z3GenSettings *g, Z3Rng *r);

void z3_rom_set_tournament(uint8_t *rom, bool on);                 /* setTournamentType('standard' / 'none') */
void z3_rom_set_seed_string(uint8_t *rom, const char *s21);        /* setSeedString: 21 bytes at 0x7FC0 */
void z3_rom_set_hash(uint8_t *rom, const uint8_t icons[5]);        /* setStartScreenHash at 0x180215 */
void z3_rom_cosmetics(uint8_t *rom, const Z3GenSettings *g);       /* heart beep, menu speed, quickswap if != -1 */
void z3_rom_update_checksum(uint8_t *rom);                         /* Rom::updateChecksum (LoROM) */

#endif
