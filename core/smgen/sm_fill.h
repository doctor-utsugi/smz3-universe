/* Item pools and filler of tewtal's standalone Super Metroid randomizer (Item.cs pools, Filler.cs, Randomizer.cs seed
 * handling), drawing from the same .NET Random as the C# so seeds can match. Single world only. */
#ifndef SM_FILL_H
#define SM_FILL_H

#include "sm_world_gen.h"
#include "../gen/smz3_rng.h"

typedef struct {
    int32_t seed;      /* 0..2147483647, as the C# "seed" option */
    int logic;         /* SM_LOGIC_* (default Tournament) */
    int placement;     /* SM_PLACEMENT_* (default Split) */
    bool race;         /* no spoiler; the ROM shows seed 0 */
} SmGenSettings;

typedef struct {
    uint8_t type, cls;
    const char *name;  /* Item.Name: "Progression Missile" and "Missile" are told apart in the credits */
    uint16_t id;
} SmItem;

typedef struct {
    SmGenSettings s;
    SmItem item_at[SM_LOC_COUNT];   /* by SM_LOCS index */
    int32_t patch_seed;             /* rnd.Next() after filling; seeds the patcher's Random */
} SmSeed;

void sm_settings_defaults(SmGenSettings *s);
/* Fills a seed like Randomizer.GenerateSeed up to the patch seed. 0, or -1 when an item had no location left
 * (the C# throws), -2 when a pass made no progress (the C# would loop forever). */
int sm_fill(SmSeed *out, const SmGenSettings *s);
/* Playthrough.Generate's check: every placed item can be collected. 1 when beatable. */
int sm_playthrough_ok(const SmSeed *seed);
/* Inventory (counts per ItemType) of the items listed. */
void sm_prog_of(SmProg *p, const SmItem *items, int n);

#endif
