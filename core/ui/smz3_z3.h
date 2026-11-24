/* A Link to the Past alone on the hosts: generation from a run's settings JSON (core/z3gen), its hash icons and Link sprites. */
#ifndef SMZ3_Z3_H
#define SMZ3_Z3_H

#include <stddef.h>
#include <stdint.h>

#define SMZ_Z3_BASE_SIZE 0x100000   /* the A Link to the Past (JP 1.0) ROM the player gives */
#define SMZ_Z3_ROM_SIZE 0x200000    /* the ROM a run plays */

/* Generates a "game":"z3" run from the JP ROM into rom_out and adds its hash to json. 0, or a negative z3_generate error
 * (-6 link_rdc is not a Link RDC, -7 the settings are not ALttP's). */
int smz_z3_generate(char *json, size_t json_cap, const uint8_t *z3, const uint8_t *link_rdc, size_t link_len,
                    uint8_t *rom_out, size_t cap);

/* The hash of a generated ROM, the names of its five file select icons ("Bow Hookshot Lamp Map Cape"). */
void smz_z3_hash_text(const uint8_t *rom, size_t len, char *out, size_t cap);

#endif
