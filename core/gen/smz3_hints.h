/* In-game hints: the pool of hints, which ones a seed gets, and what is known about each. Follows the C# GameHintService
 * (Cas generator) so the same seed gives the same hint choices; the wording is picked from the seed (see smz3_text.c). */
#ifndef SMZ3_HINTS_H
#define SMZ3_HINTS_H

#include "smz3_generate.h"
#include "smz3_patch.h"

#define SMZ_MAX_HINTS 15

enum { HINT_Location, HINT_LocationGroup, HINT_Room, HINT_Region, HINT_Dungeon, HINT_Requirement };
enum { USE_Mandatory, USE_Goal, USE_Key, USE_Sword, USE_NiceToHave, USE_Useless, USE_None = -1 };

typedef struct {
    int tile;                     /* index of the hint tile (SMZ_HINT_TILES) that shows it, or -1 when it is not on a tile */
    int type;                     /* HINT_* */
    int loc;                      /* HINT_Location: the location id */
    int region;                   /* HINT_Dungeon, HINT_Region, HINT_Requirement: region index */
    int def;                      /* index in SMZ_HINT_DUNGEONS or SMZ_HINT_LOCATIONS, -1 for the others */
    int usefulness;               /* USE_*, USE_None for single locations and requirements */
    int medallion;                /* HINT_Requirement: item type of the medallion */
} SmzHint;

typedef struct { int n; SmzHint h[SMZ_MAX_HINTS]; } SmzHints;

/* The hints the seed gets for `count` tiles (0..15). hint_seed is the second value drawn after filling; the tile
 * assignment is the C# one (tile i shows hint i modulo the number of hints). Returns the number of tiles filled. */
int smz_hints_generate(const SmzSeed *seed, const SmzPatchCfg *cfg, int32_t hint_seed, int count, SmzHints *out);

/* How useful the locations are, as the C# CheckIfLocationsAreImportant: USE_Mandatory when the seed cannot be
 * completed without them and so on. extra/nextra are items the player holds (used when checking keys). */
int smz_locations_usefulness(const SmzSeed *seed, const SmzPatchCfg *cfg, const uint16_t *locs, int n);

/* The hint seed the generator draws after filling (the value smz_generate gives the patches). */
int32_t smz_hint_seed(const SmzSeed *seed);

#endif
