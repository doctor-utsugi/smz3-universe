/* Start-with and early item choices (the tracker's ItemSettingOptions). */
#ifndef SMZ3_ITEMOPTS_H
#define SMZ3_ITEMOPTS_H

#include "smz3_fill.h"

/* Index of the option group with that name ("Bow", "Missiles", ...) or -1. */
int smz_itemopt_group(const char *name);
/* Selects an option of a group by its position (0 is "Randomized"). Returns 0, or -1 for an unknown group or option. */
int smz_itemopt_select(SmzSettings *s, const char *group, int option);
/* Item types the player starts with, in group order. Returns how many were written (at most cap). */
int smz_start_items(const SmzSettings *s, int *types, int cap);
/* Item types the filler must place early, in group order. */
int smz_early_items(const SmzSettings *s, int *types, int cap);
/* The memory bytes StartingEquipmentPatch writes: 78 bytes for the ALttP half and, for every Super Metroid word touched,
 * its offset and value. Returns the number of Super Metroid words written to keys/values (at most cap). */
int smz_start_memory(const SmzSettings *s, uint8_t zelda[78], int *keys, int *values, int cap);

#endif
