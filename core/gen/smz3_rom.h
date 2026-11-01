/* Building the playable ROM: the two game ROMs combined, the IPS patches, the seed's patches and the checksum. */
#ifndef SMZ3_ROM_H
#define SMZ3_ROM_H

#include <stddef.h>
#include <stdint.h>
#include "smz3_patch.h"

#define SMZ_SM_SIZE 0x300000
#define SMZ_Z3_SIZE 0x100000

/* Super Metroid (3 MB, no header) and A Link to the Past (1 MB, no header) into the 6 MB layout. */
void smz_rom_combine(uint8_t *out, const uint8_t *sm, const uint8_t *z3);

/* IPS patch addressed in the combined layout (the base randomizer patch). */
int smz_ips_apply(uint8_t *rom, const uint8_t *ips, size_t len);
/* IPS patch written for the original Super Metroid ROM (the Cas' patches). */
int smz_ips_apply_sm(uint8_t *rom, const uint8_t *ips, size_t len);

void smz_rom_checksum(uint8_t *rom);
/* Hooks the item shortcuts use (core/ui/smz3_shortcuts.h): once a frame each game checks a request bit the host sets and
 * calls its own refresh routine (SM beam tiles and palette, ALttP Y item icon). 0 when written, -1 when the ROM is not the
 * expected one (then it is left as it was). */
int smz_rom_shortcut_hooks(uint8_t *rom);

/* File names of the IPS patches to apply after the seed's patches with the tracker's default options, in order. */
extern const char *const SMZ_DEFAULT_CAS_IPS[];
extern const int SMZ_DEFAULT_CAS_IPS_COUNT;

#endif
