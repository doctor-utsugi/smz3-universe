/* Link and Samus sprites in the RDC container format, written into the combined ROM like the C# Rdc blocks. */
#ifndef SMZ3_SPRITE_H
#define SMZ3_SPRITE_H

#include <stddef.h>
#include <stdint.h>

/* Applies every sprite block found in the RDC file (Link type 1, Samus type 4) to rom. Returns the number of blocks
 * applied (0..2) or -1 when the data is not a valid RDC file or a block is cut short. */
int smz_sprite_apply_rdc(uint8_t *rom, size_t rom_size, const uint8_t *rdc, size_t len);
/* Author string of the RDC file (empty when unreadable). */
const char *smz_rdc_author(const uint8_t *rdc, size_t len);

#endif
