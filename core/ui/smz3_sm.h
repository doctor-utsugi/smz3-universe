/* Super Metroid alone on the hosts: generation from a run's settings JSON (core/smgen), its seed hash and Samus sprites. */
#ifndef SMZ3_SM_H
#define SMZ3_SM_H

#include <stddef.h>
#include <stdint.h>

#define SMZ_SM_BASE_SIZE 0x300000   /* the Super Metroid (JU) ROM the player gives */
#define SMZ_SM_ROM_SIZE 0x400000    /* the LoROM a run plays */

/* The size of the ROM a run of this mode (SMZ_MODE_*) plays: 6 MB for SMZ3, 4 MB for Super Metroid. */
size_t smz_mode_rom_size(int mode);

/* Generates the run of a "game":"sm" settings JSON (ui_settings_json) from the JU ROM in sm into rom_out (cap at least
 * SMZ_SM_ROM_SIZE), with the Samus sprite of samus_rdc (NULL: the original). The seed hash the file select screen shows is
 * added to json ("hash", and the first "diff" line), which has room for json_cap bytes. 0, or a negative sm_generate error
 * (-5 the settings are not Super Metroid's, -6 the sprite is not a Samus RDC). */
int smz_sm_generate(char *json, size_t json_cap, const uint8_t *sm, const uint8_t *samus_rdc, size_t samus_len,
                    uint8_t *rom_out, size_t cap);

/* The seed hash of a generated ROM, the four words of its file select screen ("GEEMER RIPPER ATOMIC POWAMP"). */
void smz_sm_hash_text(const uint8_t *rom, size_t len, char *out, size_t cap);

#endif
