/* Seed patches of tewtal's standalone Super Metroid randomizer (Patch.cs), written straight into the ROM in the C#
 * order: item PLMs, player name, seed data, seed hash, credits item list, animal surprise, title. */
#ifndef SM_PATCH_H
#define SM_PATCH_H

#include <stddef.h>
#include "sm_fill.h"

#define SM_ROM_SIZE 0x400000   /* the SM ROM padded to 4 MB, as the web client and Randomizer.CLI build it */

/* GUIDs are Guid.NewGuid() in the C#; NULL writes 32 zero bytes (what tools/gen-cli-sm rom writes too). */
typedef struct { const char *seed_guid, *world_guid; } SmPatchIds;

/* rom holds SM_ROM_SIZE bytes with the base patch applied. hash_out (4 bytes, may be NULL) gets the seed hash. */
void sm_patch(uint8_t *rom, const SmSeed *seed, const SmPatchIds *ids, uint8_t *hash_out);
/* Applies an IPS patch to a buffer of len bytes. 0, or -1 when the patch is malformed or writes out of range. */
int sm_apply_ips(uint8_t *rom, size_t len, const uint8_t *ips, size_t ips_len);
/* PLM id Patch.GetSMItemPLM writes for an item type at a location type. */
uint16_t sm_item_plm(int item_type, int loc_type);

#endif
