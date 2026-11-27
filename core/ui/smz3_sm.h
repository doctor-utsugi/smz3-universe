/* Super Metroid alone on the hosts: generation from a run's settings JSON (core/smgen), its seed hash and Samus sprites. */
#ifndef SMZ3_SM_H
#define SMZ3_SM_H

#include <stddef.h>
#include <stdint.h>
#include "smz3_proof.h"

#define SMZ_SM_BASE_SIZE 0x300000   /* the Super Metroid (JU) ROM the player gives */
#define SMZ_SM_ROM_SIZE 0x400000    /* the LoROM a run plays */

/* The size of the ROM a run of this mode (SMZ_MODE_*) plays: 6 MB for SMZ3, 2 MB for A Link to the Past, 4 MB for Super Metroid. */
size_t smz_mode_rom_size(int mode);

/* Generates the run of a "game":"sm" settings JSON (ui_settings_json) from the JU ROM in sm into rom_out (cap at least
 * SMZ_SM_ROM_SIZE), with the Samus sprite of samus_rdc (NULL: the original; a Map Rando seed takes none). The seed hash the file select screen shows is
 * added to json ("hash", and the first "diff" line), which has room for json_cap bytes. 0, or a negative sm_generate error
 * (-5 the settings are not Super Metroid's, -6 the sprite is not a Samus RDC). */
int smz_sm_generate(char *json, size_t json_cap, const uint8_t *sm, const uint8_t *samus_rdc, size_t samus_len,
                    uint8_t *rom_out, size_t cap);

/* Map Rando (blkerby/MapRandomizer) as each host has it: the crate on PC and Android, the server's BPS patch on the 3DS.
 * settings is the run's "sm" object without "generator" ({"preset","skill"}, upstream names), sm the JU ROM; the seed's
 * 4 MB ROM goes to out and its info JSON ({"hash",...}, or {"error"}) to info. 0, or -1. */
typedef int (*SmzMapRandoFn)(const char *settings, uint64_t seed, const uint8_t *sm, size_t sm_len, uint8_t *out, size_t cap,
                             char *info, size_t info_cap);
enum { SMZ_MR_NONE, SMZ_MR_READY, SMZ_MR_OFFLINE };   /* not on this host; ready; needs a connection the host lacks now */
#define SMZ_SM_ERR_MAPRANDO -10                         /* smz_sm_generate: Map Rando failed, smz_sm_error says why */
void smz_sm_set_maprando(SmzMapRandoFn fn, int state);
int smz_sm_maprando_state(void);
/* The folder of the seeds the game comes with (assets/maprando), used when Map Rando cannot be built or downloaded now.
 * smz_sm_maprando_pool says whether it is there; smz_sm_maprando_note, after a generate, when one was used ("" otherwise). */
void smz_sm_set_maprando_pool(const char *dir);
int smz_sm_maprando_pool(void);
/* Off for a race or an online run: the others play the asked seed, never one of the set. */
void smz_sm_maprando_pool_allow(int ok);
const char *smz_sm_maprando_note(void);
/* Whether a run's settings JSON is a Map Rando seed ("sm":{"generator":"maprando",...}). */
int smz_sm_is_maprando(const char *json);
/* What a smz_sm_generate error means for the player, NULL for the codes they cannot act on. */
const char *smz_sm_error(int rc);

/* The seed hash of a generated ROM, the four words of its file select screen ("GEEMER RIPPER ATOMIC POWAMP"). */
void smz_sm_hash_text(const uint8_t *rom, size_t len, char *out, size_t cap);

/* The SM_LOCS index (core/smgen) of a check of smz3_locations, which the tracker and the proof of run count in Super Metroid
 * alone as well (their item bit, $7ED870 + id / 8, is the location id): -1 for a Zelda check or an index out of range. */
int smz_sm_loc_of(int tracker_index);
/* The smz3_locations index of an SM_LOCS index, or -1. */
int smz_sm_tracker_index(int sm_index);

/* The proof of run of a "game":"sm" settings JSON against its seed, rebuilt with sm_fill: counts in v->out_of_logic the
 * checks the Super Metroid logic did not reach with the items found before them (a Zelda check counts as one). 0, or -1
 * when the seed cannot be rebuilt. */
int smz_sm_proof_logic(const char *json, const SmzProof *p, SmzProofVerdict *v);

#endif
