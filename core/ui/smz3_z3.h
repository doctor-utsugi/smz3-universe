/* A Link to the Past alone on the hosts: generation from a run's settings JSON (core/z3gen), its hash icons and Link sprites. */
#ifndef SMZ3_Z3_H
#define SMZ3_Z3_H

#include <stddef.h>
#include <stdint.h>

#define SMZ_Z3_BASE_SIZE 0x100000   /* the A Link to the Past (JP 1.0) ROM the player gives */
#define SMZ_Z3_ROM_SIZE 0x200000    /* the ROM a run plays */

#include "smz3_proof.h"

#define SMZ_Z3_SEED_TRIES 16        /* seed numbers tried from the one asked for, as alttpr fails some fills (Inverted: 1 in 12) */
#define SMZ_Z3_ERR_SPRITE -9        /* link_rdc is not a Link RDC */

/* Generates a "game":"z3" run into rom_out; a failed fill (-3, -4) tries the next seed number, so json's seed rebuilds anywhere.
 * 0, or a z3_generate error (-6 Enemizer), SMZ_Z3_ERR_SPRITE, or -7 when the settings are not ALttP's. */
int smz_z3_generate(char *json, size_t json_cap, const uint8_t *z3, const uint8_t *link_rdc, size_t link_len,
                    uint8_t *rom_out, size_t cap);

/* The hash of a generated ROM, the names of its five file select icons ("Bow Hookshot Lamp Map Cape"). */
void smz_z3_hash_text(const uint8_t *rom, size_t len, char *out, size_t cap);
/* The icon indices (0-31, the picture z3hash_NN.png) of such a hash text: 5, or 0 when it is not one. */
int smz_z3_hash_icons(const char *text, int out[5]);
/* What a smz_z3_generate error means for the player, NULL for the codes they cannot act on. */
const char *smz_z3_error(int rc);

/* The Z3_LOCS index (core/z3gen) of a Zelda check of smz3_locations, which the tracker and the proof of run count in A Link to
 * the Past alone too (the world's own copy: z3_loc_named), or -1 for a Metroid check. */
int smz_z3_loc_of(int tracker_index);
/* The smz3_locations index of a Z3_LOCS index (either world's copy), or -1. */
int smz_z3_tracker_index(int z3_loc);

/* Counts in v->out_of_logic the checks of a "game":"z3" run the logic did not reach with the items found before them,
 * prizes and Agahnim taken once reachable. 0, or -1 when the seed cannot be rebuilt. */
int smz_z3_proof_logic(const char *json, const SmzProof *p, SmzProofVerdict *v);

#ifdef Z3_FILL_H
/* The world smz_z3_generate builds for these settings and seed number, without the ROM. 0, or -1 when no try fills. */
int smz_z3_rebuild(const Z3GenSettings *s, uint32_t seed, Z3Seed *z);
/* The "z3" options and the seed number of a run's settings JSON. 0, or -1 when they do not parse. */
int smz_z3_settings(const char *json, Z3GenSettings *s, uint32_t *seed);
#endif

#endif
