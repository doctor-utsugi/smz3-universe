/* Hand-written half of the Z3 world: ItemCollection's helpers, World::__construct's config switches and the
   Location/World methods the transpiled closures call. Ported from alttp_php_randomizer (sporchia, MIT). */
#include <string.h>
#include "z3_world.h"
#include "z3_romdata_gen.h"

#define H(i) (it->n[Z3_ITEM_##i] > 0)

void z3_cfg_derive(Z3Cfg *c)
{
    switch (c->dungeonItems) {   /* falls through like the PHP switch */
    case Z3_DUNGEONITEMS_FULL: c->region_wildBigKeys = 1; /* fall through */
    case Z3_DUNGEONITEMS_MCS: c->region_wildKeys = 1; /* fall through */
    case Z3_DUNGEONITEMS_MC: c->region_wildMaps = c->region_wildCompasses = 1; break;
    default: break;
    }
    switch (c->item_pool) {
    case Z3_ITEM_POOL_SUPEREXPERT: case Z3_ITEM_POOL_CROWD_CONTROL: c->item_overflow_count_Shield = 0; break;
    case Z3_ITEM_POOL_EXPERT: c->item_overflow_count_Shield = 1; break;
    case Z3_ITEM_POOL_HARD: c->item_overflow_count_Shield = 2; break;
    default: break;
    }
    switch (c->item_functionality) {
    case Z3_ITEM_FUNCTIONALITY_SUPEREXPERT: c->rom_BottleFill_Magic = 0x00; c->rom_CatchableFairies = 0; break;
    case Z3_ITEM_FUNCTIONALITY_EXPERT: c->rom_BottleFill_Magic = 0x20; c->rom_CatchableFairies = 0; break;
    case Z3_ITEM_FUNCTIONALITY_HARD: c->rom_BottleFill_Magic = 0x40; c->rom_CatchableFairies = 0; break;
    default: break;
    }
    c->region_requireBetterBow = c->region_requireBetterSword = c->itemPlacement == Z3_ITEMPLACEMENT_BASIC;
    if (c->mode_weapons == Z3_MODE_WEAPONS_SWORDLESS) c->region_requireBetterBow = 1;
    if (c->itemPlacement == Z3_ITEMPLACEMENT_BASIC) c->region_forceSkullWoodsKey = 1;
    /* config/alttp.php goals.*.item.Goal.Required */
    if (c->goal == Z3_GOAL_TRIFORCE_HUNT) c->item_Goal_Required = 20;
    if (c->goal == Z3_GOAL_GANONHUNT) c->item_Goal_Required = 40;
    z3_cfg_logic(c);
}

void z3_world_init(Z3World *w, const Z3Cfg *cfg)
{
    memset(w, 0, sizeof *w);
    w->cfg = *cfg;
    z3_world_defaults(w);
    for (int s = 0; s < Z3_SHOP_COUNT; s++)   /* the region constructors' default inventories */
        for (int k = 0; k < 3; k++) {
            const Z3ShopSlot *d = &Z3_SHOP_ROM[s].inv[k];
            w->shop[s].item[k] = d->item;
            w->shop[s].max[k] = d->max;
            w->shop[s].price[k] = d->price;
        }
}

bool z3_has(const Z3Items *it, int item, int at_least)
{
    if (at_least <= 0) return true;
    if (H(ShopKey) && strncmp(Z3_ITEMS[item].name, "Key", 3) == 0) return true;
    return it->n[item] >= at_least;
}

bool z3_canLiftRocks(const Z3World *w, const Z3Items *it) { (void)w; return H(PowerGlove) || H(ProgressiveGlove) || H(TitansMitt); }
bool z3_canLiftDarkRocks(const Z3World *w, const Z3Items *it) { (void)w; return H(TitansMitt) || it->n[Z3_ITEM_ProgressiveGlove] >= 2; }
bool z3_canLightTorches(const Z3World *w, const Z3Items *it) { (void)w; return H(FireRod) || H(Lamp); }
bool z3_canSpinSpeed(const Z3World *w, const Z3Items *it) { return H(PegasusBoots) && (z3_hasSword(w, it, 1) || H(Hookshot)); }
bool z3_canBlockLasers(const Z3World *w, const Z3Items *it) { (void)w; return H(MirrorShield) || it->n[Z3_ITEM_ProgressiveShield] >= 3; }
bool z3_canBombThings(const Z3World *w, const Z3Items *it) { (void)w; (void)it; return true; }
bool z3_glitchedLinkInDarkWorld(const Z3World *w, const Z3Items *it) { return H(MoonPearl) || z3_hasABottle(w, it); }

bool z3_canMeltThings(const Z3World *w, const Z3Items *it)
{
    return H(FireRod) || (H(Bombos) && (w->cfg.mode_weapons == Z3_MODE_WEAPONS_SWORDLESS || z3_hasSword(w, it, 1)));
}

bool z3_canFly(const Z3World *w, const Z3Items *it)
{
    if (H(OcarinaActive)) return true;
    if (!H(OcarinaInactive)) return false;
    if (w->cfg.mode_state == Z3_MODE_STATE_INVERTED)   /* World\Inverted: the bird needs the Light World portal */
        return H(MoonPearl) && (H(DefeatAgahnim) || (H(Hammer) && z3_canLiftRocks(w, it)) || z3_canLiftDarkRocks(w, it));
    return true;
}

bool z3_canAcquireFairy(const Z3World *w, const Z3Items *it)
{
    (void)it;
    return !w || w->cfg.rom_CatchableFairies;
}

bool z3_canBunnyRevive(const Z3World *w, const Z3Items *it)
{
    return z3_hasABottle(w, it) && H(BugCatchingNet) && (!w || z3_canAcquireFairy(w, it));
}

bool z3_canShootArrows(const Z3World *w, const Z3Items *it, int min_level)
{
    bool rupee = w->cfg.rom_rupeeBow;
    if (min_level == 2)
        return H(BowAndSilverArrows) || (it->n[Z3_ITEM_ProgressiveBow] >= 2 && (!rupee || H(ShopArrow)))
            || (H(SilverArrowUpgrade) && (H(Bow) || H(BowAndArrows) || H(ProgressiveBow)));
    return ((H(Bow) || H(ProgressiveBow)) && (!rupee || H(ShopArrow) || H(SilverArrowUpgrade)))
        || H(BowAndArrows) || H(BowAndSilverArrows);
}

int z3_bottleCount(const Z3Items *it)
{
    int n = 0;
    for (int i = 0; i < Z3_ITEM_COUNT; i++)
        if (Z3_ITEMS[i].cls == Z3_IC_Bottle) n += it->n[i];
    return n;
}

bool z3_canExtendMagic(const Z3World *w, const Z3Items *it, double bars)
{
    double mod = 1.0;
    if (w) {
        mod = w->cfg.rom_BottleFill_Magic / (double)0x80;
        if (mod > 1) mod = 1.0;
    }
    double base = H(QuarterMagic) ? 4 : H(HalfMagic) ? 2 : 1;
    return base + base * z3_bottleCount(it) * mod >= bars;
}

bool z3_hasHealth(const Z3World *w, const Z3Items *it, double minimum)
{
    (void)w;
    double h = 0.25 * it->n[Z3_ITEM_PieceOfHeart] + it->n[Z3_ITEM_HeartContainerNoAnimation]
        + it->n[Z3_ITEM_BossHeartContainer] + it->n[Z3_ITEM_HeartContainer];
    return h >= minimum;
}

bool z3_canKillEscapeThings(const Z3World *w, const Z3Items *it)
{
    bool hp = w->cfg.enemizer_enemyHealth == Z3_ENEMIZER_ENEMYHEALTH_DEFAULT;
    return H(UncleSword) || H(CaneOfSomaria) || (H(TenBombs) && hp) || (H(CaneOfByrna) && hp)
        || z3_canShootArrows(w, it, 1) || H(Hammer) || H(FireRod) || w->cfg.ignoreCanKillEscapeThings;
}

bool z3_canKillMostThings(const Z3World *w, const Z3Items *it, int enemies)
{
    bool hp = w->cfg.enemizer_enemyHealth == Z3_ENEMIZER_ENEMYHEALTH_DEFAULT;
    return z3_hasSword(w, it, 1) || H(CaneOfSomaria) || (z3_canBombThings(w, it) && enemies < 6 && hp)
        || (H(CaneOfByrna) && (enemies < 6 || z3_canExtendMagic(NULL, it, 2)) && hp)
        || z3_canShootArrows(w, it, 1) || H(Hammer) || H(FireRod);
}

bool z3_canGetGoodBee(const Z3World *w, const Z3Items *it)
{
    return H(BugCatchingNet) && z3_hasABottle(w, it) && (H(PegasusBoots) || (z3_hasSword(w, it, 1) && H(Quake)));
}

bool z3_hasSword(const Z3World *w, const Z3Items *it, int min_level)
{
    (void)w;
    int prog = it->n[Z3_ITEM_ProgressiveSword], uncle = H(UncleSword);
    switch (min_level) {
    case 4: return prog >= 4 || (uncle && prog >= 3) || H(L4Sword);
    case 3: return prog >= 3 || (uncle && prog >= 2) || H(L3Sword) || H(L4Sword);
    case 2: return prog >= 2 || (uncle && prog >= 1) || H(L2Sword) || H(MasterSword) || H(L3Sword) || H(L4Sword);
    default:
        return prog >= 1 || uncle || H(L1Sword) || H(L1SwordAndShield) || H(L2Sword) || H(MasterSword) || H(L3Sword) || H(L4Sword);
    }
}

bool z3_hasArmor(const Z3World *w, const Z3Items *it, int min_level)
{
    (void)w;
    if (min_level == 2) return it->n[Z3_ITEM_ProgressiveArmor] >= 2 || H(RedMail);
    return H(ProgressiveArmor) || H(BlueMail) || H(RedMail);
}

bool z3_hasBottle(const Z3World *w, const Z3Items *it, int at_least) { (void)w; return z3_bottleCount(it) >= at_least; }

bool z3_hasABottle(const Z3World *w, const Z3Items *it)
{
    (void)w;
    return H(BottleWithBee) || H(BottleWithFairy) || H(BottleWithRedPotion) || H(BottleWithGreenPotion)
        || H(BottleWithBluePotion) || H(Bottle) || H(BottleWithGoldBee);
}

bool z3_loc_access(const Z3World *w, const Z3Items *it, int loc)
{
    return z3_region_enter(w, it, Z3_LOCS[loc].region) && z3_loc_req(w, it, loc);
}

/* Region::canFill: dungeon items stay home unless wild, and Standard keeps the sewers key in the castle. */
static bool region_can_fill(const Z3World *w, int reg, int item)
{
    const Z3Cfg *c = &w->cfg;
    int cls = Z3_ITEMS[item].cls;
    bool bound = (!c->region_wildKeys && cls == Z3_IC_Key) || (!c->region_wildBigKeys && cls == Z3_IC_BigKey)
        || (item == Z3_ITEM_KeyH2 && c->mode_state == Z3_MODE_STATE_STANDARD && c->logic != Z3_LOGIC_NOLOGIC)
        || (!c->region_wildMaps && cls == Z3_IC_Map) || (!c->region_wildCompasses && cls == Z3_IC_Compass);
    return !bound || z3_region_item(reg, item);
}

bool z3_loc_can_fill(Z3World *w, const Z3Items *it, int loc, int item, bool check_access)
{
    uint8_t old = w->item_at[loc];
    w->item_at[loc] = (uint8_t)item;
    bool ok = z3_loc_always(w, it, loc, item)
        || (region_can_fill(w, Z3_LOCS[loc].region, item) && z3_loc_fill(w, it, loc, item)
            && (!check_access || z3_loc_access(w, it, loc)));
    w->item_at[loc] = old;
    return ok;
}

bool z3_loc_collectable(int loc)
{
    const char *k = Z3_LOCS[loc].kind;
    return strcmp(k, "Medallion") != 0 && strcmp(k, "Fountain") != 0;
}

bool z3_shop_access(const Z3World *w, const Z3Items *it, int shop)
{
    return z3_region_enter(w, it, Z3_SHOPS[shop].region) && z3_shop_req(w, it, shop);
}

/* The do/while of World::collectItems and collectOtherItems: each pass tests every location against the items held
   when the pass began, as PHP's filter does. Shop inventories count as locations (getCollectableLocations). */
static void sweep(const Z3World *w, Z3Items *have, Z3Items *found, Z3Collected *col)
{
    Z3Collected fresh;
    if (!col) {
        memset(&fresh, 0, sizeof fresh);
        col = &fresh;
    }
    for (int n = 1; n;) {
        Z3Items before = *have;
        n = 0;
        for (int l = 0; l < Z3_LOC_COUNT; l++) {
            int item = w->item_at[l];
            if (col->loc[l] || item == Z3_ITEM_NONE || !z3_loc_collectable(l) || !z3_loc_exists(w, l)) continue;
            if (!z3_loc_access(w, &before, l)) continue;
            col->loc[l] = true;
            col->count++;
            z3_items_add(have, item, 1);
            if (found) z3_items_add(found, item, 1);
            n++;
        }
        for (int s = 0; s < Z3_SHOP_COUNT; s++)
            for (int k = 0; k < 3; k++) {
                int item = w->shop[s].item[k];
                if (col->shop[s][k] || item == Z3_ITEM_NONE || !z3_shop_access(w, &before, s)) continue;
                col->shop[s][k] = true;
                col->count++;
                z3_items_add(have, item, 1);
                if (found) z3_items_add(found, item, 1);
                n++;
            }
    }
}

void z3_collect_items(const Z3World *w, const Z3Items *in, Z3Items *out)
{
    if (in) *out = *in;
    else memset(out, 0, sizeof *out);
    for (int i = 0; i < Z3_ITEM_COUNT; i++) z3_items_add(out, i, w->pre.n[i]);
    sweep(w, out, NULL, w->col);
}

void z3_collect_other(const Z3World *w, const Z3Items *in, Z3Items *found)
{
    Z3Items have = *in;
    memset(found, 0, sizeof *found);
    sweep(w, &have, found, w->col);
}

void z3_collect(const Z3World *w, Z3Items *out)
{
    z3_collect_items(w, NULL, out);
}

bool z3_loc_access_collected(const Z3World *w, int loc)
{
    Z3Items all;
    z3_collect(w, &all);
    return z3_loc_access(w, &all, loc);
}

bool z3_win_condition(const Z3World *w, const Z3Items *it)
{
    return z3_has(it, Z3_ITEM_Triforce, 1)
        || (z3_region_enter(w, it, Z3_REG_North_East_Light_World) && w->cfg.goal == Z3_GOAL_TRIFORCE_HUNT
            && z3_has(it, Z3_ITEM_TriforcePiece, w->cfg.item_Goal_Required));
}
