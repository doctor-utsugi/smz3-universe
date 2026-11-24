/* The A Link to the Past world in C: what is in logic and where the items are. Hand-written; the regions, locations
   and access closures are transpiled from alttpr's PHP into z3_world_gen.{h,c} by tools/z3/port_world_php.py. */
#ifndef Z3_WORLD_H
#define Z3_WORLD_H

#include <stddef.h>
#include "z3_world_gen.h"

/* An ItemCollection: how many of each item (aliases such as UncleSword count apart, as in PHP). */
struct Z3Items {
    uint8_t n[Z3_ITEM_COUNT];
};

struct Z3World {
    Z3Cfg cfg;
    uint8_t item_at[Z3_LOC_COUNT];   /* item at each location, Z3_ITEM_NONE when empty */
    uint8_t boss_of[Z3_REG_COUNT];   /* Region::$boss */
    uint8_t gt_boss[3];              /* Ganon's Tower boss_top, boss_middle, boss_bottom */
    Z3Items pre;                     /* World::getPreCollectedItems() */
};

/* World::__construct's switches (dungeon items, item pool and functionality, basic placement) and the logic preset. */
void z3_cfg_derive(Z3Cfg *c);
/* A world for these settings with alttpr's default bosses and fixed items, nothing placed. */
void z3_world_init(Z3World *w, const Z3Cfg *cfg);

static inline void z3_items_add(Z3Items *it, int item, int n) { it->n[item] = (uint8_t)(it->n[item] + n); }

/* ItemCollection::has(): a count of 0 always passes; any ShopKey makes every Key* item count as held. */
bool z3_has(const Z3Items *it, int item, int at_least);

/* ItemCollection helpers. w may be NULL where PHP's $world is optional (canExtendMagic, canBunnyRevive, canAcquireFairy). */
bool z3_canLiftRocks(const Z3World *w, const Z3Items *it);
bool z3_canLiftDarkRocks(const Z3World *w, const Z3Items *it);
bool z3_canLightTorches(const Z3World *w, const Z3Items *it);
bool z3_canMeltThings(const Z3World *w, const Z3Items *it);
bool z3_canFly(const Z3World *w, const Z3Items *it);
bool z3_canSpinSpeed(const Z3World *w, const Z3Items *it);
bool z3_canAcquireFairy(const Z3World *w, const Z3Items *it);
bool z3_canBunnyRevive(const Z3World *w, const Z3Items *it);
bool z3_canShootArrows(const Z3World *w, const Z3Items *it, int min_level);
bool z3_canBlockLasers(const Z3World *w, const Z3Items *it);
bool z3_canExtendMagic(const Z3World *w, const Z3Items *it, double bars);
bool z3_glitchedLinkInDarkWorld(const Z3World *w, const Z3Items *it);
bool z3_hasHealth(const Z3World *w, const Z3Items *it, double minimum);
bool z3_canKillEscapeThings(const Z3World *w, const Z3Items *it);
bool z3_canKillMostThings(const Z3World *w, const Z3Items *it, int enemies);
bool z3_canBombThings(const Z3World *w, const Z3Items *it);
bool z3_canGetGoodBee(const Z3World *w, const Z3Items *it);
bool z3_hasSword(const Z3World *w, const Z3Items *it, int min_level);
bool z3_hasArmor(const Z3World *w, const Z3Items *it, int min_level);
bool z3_hasBottle(const Z3World *w, const Z3Items *it, int at_least);
bool z3_hasABottle(const Z3World *w, const Z3Items *it);
int z3_bottleCount(const Z3Items *it);

/* Location::canAccess(): its region can be entered and its own requirement holds. */
bool z3_loc_access(const Z3World *w, const Z3Items *it, int loc);
/* Location::canFill(): always-allow, or region and location fill rules plus access; item sits at loc while checked. */
bool z3_loc_can_fill(Z3World *w, const Z3Items *it, int loc, int item, bool check_access);
/* World::collectItems(): pre-collected items plus everything reachable from the current placement. */
void z3_collect(const Z3World *w, Z3Items *out);
/* canAccess($this->world->collectItems()), used by Link's Uncle's fill rule. */
bool z3_loc_access_collected(const Z3World *w, int loc);
/* Location kinds World::getCollectableLocations() leaves out (medallion and fountain slots). */
bool z3_loc_collectable(int loc);

#endif
