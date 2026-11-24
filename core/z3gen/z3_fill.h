/* Randomizer::prepareWorld and the RandomAssumed filler (alttp_php_randomizer), drawing from z3_rng in the PHP's
   order: pools, shops, medallions, prizes, fountains, prize packs, swords and keys, then the fill. */
#ifndef Z3_FILL_H
#define Z3_FILL_H

#include "z3_world.h"
#include "z3_rng.h"

/* One generated world: the placement plus what the ROM writer needs beside it. */
typedef struct {
    Z3World w;
    Z3Collected col;          /* World::$collected_locations, shared by every sweep of this world */
    uint8_t uncle_target;     /* the sword UncleSword stands for (ItemAlias::setTarget), Z3_ITEM_NONE if unused */
    uint8_t packs[7][8];      /* shufflePrizePacks: sprite bytes of prize packs 0..6 */
} Z3Seed;

/* World::__construct and Randomizer::__construct for cfg (already through z3_cfg_derive): defaults, shops,
   the three heart containers and capacity upgrades every seed starts with. */
void z3_seed_init(Z3Seed *s, const Z3Cfg *cfg);
/* Randomizer::randomize without texts, credits and hints. 0, or -1 prizes could not be placed, -2 an item found no
   location (alttpr's "No Available Locations"), -3 more items than locations. */
int z3_fill(Z3Seed *s, Z3Rng *r);
/* WorldCollection::isWinnable(): a fresh sweep from the pre-collected items reaches the win condition. */
bool z3_seed_winnable(Z3Seed *s);
/* Not alttpr's: every location with an item is reachable (what 100% Locations promises). */
bool z3_seed_all_reachable(Z3Seed *s);
/* World::getBottle(): a random bottle, filled only if asked; the last (fairy) only with catchable fairies. */
int z3_get_bottle(const Z3World *w, Z3Rng *r, bool filled);

#endif
