/* Randomizer::prepareWorld and the RandomAssumed filler (alttp_php_randomizer), drawing from z3_rng in the PHP's
   order: pools, shops, medallions, prizes, fountains, prize packs, swords and keys, then the fill. */
#ifndef Z3_FILL_H
#define Z3_FILL_H

#include "z3_world.h"
#include "z3_rng.h"
#include "z3_text.h"

/* One generated world: the placement plus what the ROM writer needs beside it. */
typedef struct {
    Z3World w;
    Z3Collected col;          /* World::$collected_locations, shared by every sweep of this world */
    uint8_t uncle_target;     /* the sword UncleSword stands for (ItemAlias::setTarget), Z3_ITEM_NONE if unused */
    uint8_t packs[7][8];      /* shufflePrizePacks: sprite bytes of prize packs 0..6 */
    uint8_t adv[256];         /* Randomizer::$advancement_items as prepareWorld left them (HintService reads it) */
    int n_adv;
    int16_t bow_alt_loc;      /* setTexts' Progressive Bow that writes as ProgressiveBowAlternate (0x65), -1 none */
    Z3Texts tx;               /* World::$texts and $credits */
    bool start_boots;         /* Z3GenSettings.start_boots, set after z3_seed_init */
} Z3Seed;

/* World::__construct and Randomizer::__construct for cfg (already through z3_cfg_derive): defaults, shops,
   the three heart containers and capacity upgrades every seed starts with. */
void z3_seed_init(Z3Seed *s, const Z3Cfg *cfg);
/* Randomizer::randomize up to the fill (z3_texts_randomize does the rest). 0, or -1 prizes could not be placed, -2 an item found no
   location (alttpr's "No Available Locations"), -3 more items than locations. */
int z3_fill(Z3Seed *s, Z3Rng *r);
/* The rest of Randomizer::randomize after the fill: setTexts, randomizeCredits and HintService::applyHints (hints
   is spoil.Hints == 'on'), in that order (z3_text.c). */
void z3_texts_randomize(Z3Seed *s, Z3Rng *r, bool hints);
/* WorldCollection::isWinnable(): a fresh sweep from the pre-collected items reaches the win condition. */
bool z3_seed_winnable(Z3Seed *s);
/* Not alttpr's: every location with an item is reachable (what 100% Locations promises). */
bool z3_seed_all_reachable(Z3Seed *s);
/* World::getBottle(): a random bottle, filled only if asked; the last (fairy) only with catchable fairies. */
int z3_get_bottle(const Z3World *w, Z3Rng *r, bool filled);

#endif
