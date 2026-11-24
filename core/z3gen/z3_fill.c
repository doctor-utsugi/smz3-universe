/* Randomizer::prepareWorld, fillPrizes and Filler\RandomAssumed from alttp_php_randomizer (sporchia, MIT). Every
   get_random_int, fy_shuffle and Collection::random/randomCollection is a z3_rng draw in the same place and order. */
#include <string.h>
#include "z3_fill.h"
#include "z3_romdata_gen.h"

/* A PHP array of items or locations, kept in order (unset + array_merge never reorders). */
typedef struct { uint8_t v[256]; int n; } List;

static void push(List *l, int x) { l->v[l->n++] = (uint8_t)x; }
static int pop(List *l) { return l->v[--l->n]; }
static void del(List *l, int i) { memmove(l->v + i, l->v + i + 1, (size_t)(--l->n - i)); }
static void append(List *l, const List *o) { for (int i = 0; i < o->n; i++) push(l, o->v[i]); }

static void unshift(List *l, int x)
{
    memmove(l->v + 1, l->v, (size_t)l->n++);
    l->v[0] = (uint8_t)x;
}

/* fy_shuffle */
static void shuffle(List *l, Z3Rng *r)
{
    for (int i = l->n - 1; i >= 0; --i) {
        int j = z3_rng_int(r, 0, i);
        uint8_t t = l->v[i];
        l->v[i] = l->v[j];
        l->v[j] = t;
    }
}

/* Collection::randomCollection: splice out a random element `number` times. */
static void random_collection(const List *in, int number, List *out, Z3Rng *r)
{
    List old = *in;
    out->n = 0;
    while (number-- > 0 && old.n > 0) {
        int k = z3_rng_int(r, 0, old.n - 1);
        push(out, old.v[k]);
        del(&old, k);
    }
}

static void to_items(const List *l, Z3Items *it)
{
    memset(it, 0, sizeof *it);
    for (int i = 0; i < l->n; i++) z3_items_add(it, l->v[i], 1);
}

static void reset_collected(Z3Seed *s) { memset(&s->col, 0, sizeof s->col); }
static bool is_empty(const Z3Seed *s, int loc) { return s->w.item_at[loc] == Z3_ITEM_NONE && z3_loc_exists(&s->w, loc); }
static int cls(int item) { return Z3_ITEMS[item].cls; }

int z3_get_bottle(const Z3World *w, Z3Rng *r, bool filled)
{
    static const uint8_t bottles[] = { Z3_ITEM_Bottle, Z3_ITEM_BottleWithRedPotion, Z3_ITEM_BottleWithGreenPotion,
        Z3_ITEM_BottleWithBluePotion, Z3_ITEM_BottleWithBee, Z3_ITEM_BottleWithGoldBee, Z3_ITEM_BottleWithFairy };
    return bottles[z3_rng_int(r, filled ? 1 : 0, 7 - (w->cfg.rom_CatchableFairies ? 1 : 2))];
}

/* World::getAdvancementItems / getNiceItems / getItemPool / getDungeonPool: config/item.php counts, item.count
   overrides (the hunt goals' TriforcePiece), each BottleWithRandom drawn by getBottle(). */
static void pool(const Z3World *w, Z3Rng *r, const Z3PoolEntry *e, int n, List *out)
{
    out->n = 0;
    for (int i = 0; i < n; i++) {
        int count = e[i].count;
        if (e[i].item == Z3_ITEM_TriforcePiece && w->cfg.goal == Z3_GOAL_TRIFORCE_HUNT) count = 30;
        if (e[i].item == Z3_ITEM_TriforcePiece && w->cfg.goal == Z3_GOAL_GANONHUNT) count = 50;
        for (int k = 0; k < count && k < 216; k++)
            push(out, e[i].item == Z3_ITEM_NONE ? z3_get_bottle(w, r, false) : e[i].item);
    }
}
#define POOL(w, r, name, out) pool(w, r, Z3_POOL_##name, Z3_POOL_##name##_LEN, out)

void z3_seed_init(Z3Seed *s, const Z3Cfg *cfg)
{
    memset(s, 0, sizeof *s);
    z3_world_init(&s->w, cfg);
    s->w.col = &s->col;
    s->uncle_target = Z3_ITEM_NONE;
    s->bow_alt_loc = -1;
    z3_texts_init(&s->tx);
    z3_items_add(&s->w.pre, Z3_ITEM_BossHeartContainer, 3);
    z3_items_add(&s->w.pre, Z3_ITEM_BombUpgrade10, 1);
    z3_items_add(&s->w.pre, Z3_ITEM_ArrowUpgrade10, 3);
}

/* Randomizer::setShops without retro, generic keys and take-anys (which return before the rest). */
static void set_shops(Z3World *w)
{
    for (int i = 0; i < Z3_SHOP_COUNT; i++)
        if (Z3_SHOP_ROM[i].kind != Z3_SHOP_KIND_TAKEANY) w->shop[i].active = true;
    int pool = w->cfg.item_pool;
    if (pool == Z3_ITEM_POOL_HARD || pool == Z3_ITEM_POOL_EXPERT || pool == Z3_ITEM_POOL_SUPEREXPERT)   /* shops.HardMode */
        memset(w->shop[Z3_SHOP_Capacity_Upgrade].item, Z3_ITEM_NONE, 3);
    static const uint8_t dw[] = { Z3_SHOP_Dark_World_Potion_Shop, Z3_SHOP_Dark_World_Lumberjack_Hut_Shop,
                                  Z3_SHOP_Dark_World_Outcasts_Shop, Z3_SHOP_Dark_World_Lake_Hylia_Shop };
    if (w->cfg.item_overflow_count_Shield < 2) {
        Z3ShopState *f = &w->shop[Z3_SHOP_Dark_World_Forest_Shop];
        f->item[0] = Z3_ITEM_TwentyRupees2, f->price[0] = 500, f->max[0] = 0;
    }
    if (w->cfg.item_overflow_count_Shield < 1)
        for (int i = 0; i < 4; i++) {
            Z3ShopState *f = &w->shop[dw[i]];
            f->item[1] = Z3_ITEM_TwentyRupees2, f->price[1] = 50, f->max[1] = 0;
        }
}

static void set_medallions(Z3World *w, Z3Rng *r)
{
    static const uint8_t med[] = { Z3_ITEM_Ether, Z3_ITEM_Bombos, Z3_ITEM_Quake };
    for (int l = 0; l < Z3_LOC_COUNT; l++)
        if (Z3_LOCS[l].region == Z3_REG_Medallions && w->item_at[l] == Z3_ITEM_NONE)
            w->item_at[l] = med[z3_rng_int(r, 0, 2)];
}

static void set_fountains(Z3World *w, Z3Rng *r)
{
    for (int l = 0; l < Z3_LOC_COUNT; l++)
        if (Z3_LOCS[l].region == Z3_REG_Fountains && w->item_at[l] == Z3_ITEM_NONE)
            w->item_at[l] = (uint8_t)z3_get_bottle(w, r, true);
}

/* shufflePrizePacks: the seven vanilla packs in a random order (Sprite\Droppable bytes). */
static void shuffle_prize_packs(Z3Seed *s, Z3Rng *r)
{
    enum { H = 0xD8, RG = 0xD9, RB = 0xDA, RR = 0xDB, B1 = 0xDC, B4 = 0xDD, B8 = 0xDE, MS = 0xDF, MF = 0xE0,
           A5 = 0xE1, A10 = 0xE2, F = 0xE3 };
    static const uint8_t vanilla[7][8] = {
        { H, H, H, H, RG, H, H, RG }, { RB, RG, RB, RR, RB, RG, RB, RB }, { MF, MS, MS, RB, MF, MS, H, MS },
        { B1, B1, B1, B4, B1, B1, B8, B1 }, { A5, H, A5, A10, A5, H, A5, A10 }, { MS, RG, H, A5, MS, B1, RG, H },
        { H, F, MF, RR, B8, H, RR, A10 },
    };
    List order = { .n = 0 };
    for (int i = 0; i < 7; i++) push(&order, i);
    shuffle(&order, r);
    for (int i = 0; i < 7; i++) memcpy(s->packs[i], vanilla[order.v[i]], 8);
}

/* World::checkWinCondition($collected): collectItems($collected), which adds the pre-collected items again. */
static bool check_win(Z3Seed *s, const Z3Items *collected)
{
    Z3Items all;
    z3_collect_items(&s->w, collected, &all);
    return z3_win_condition(&s->w, &all);
}

static bool is_crystal_prize(int loc) { return strcmp(Z3_LOCS[loc].kind, "Prize_Crystal") == 0; }
static bool is_pendant_prize(int loc) { return strcmp(Z3_LOCS[loc].kind, "Prize_Pendant") == 0; }

/* One half of fillPrizes: each empty location takes the last prize the assumed items can reach it with. Returns
   false where PHP resets and retries (or throws once out of attempts). */
static bool place_prizes(Z3Seed *s, Z3Rng *r, const List *empty, List *prizes)
{
    for (int i = 0; i < empty->n; i++) {
        int loc = empty->v[i], total = prizes->n, prize = 0;
        Z3Items assumed;
        for (int k = 0; k < total; k++) {
            prize = pop(prizes);
            reset_collected(s);
            List in, adv;
            POOL(&s->w, r, DUNGEON, &in);
            POOL(&s->w, r, ADVANCEMENT, &adv);
            append(&in, &adv);
            append(&in, prizes);
            Z3Items base;
            to_items(&in, &base);
            z3_collect_items(&s->w, &base, &assumed);
            if (z3_loc_access(&s->w, &assumed, loc)) break;
            unshift(prizes, prize);
        }
        if (total == prizes->n) continue;
        s->w.item_at[loc] = (uint8_t)prize;
        if (!check_win(s, &assumed)) return false;
    }
    return true;
}

static bool any_empty(const Z3Seed *s, const List *locs)
{
    for (int i = 0; i < locs->n; i++)
        if (s->w.item_at[locs->v[i]] == Z3_ITEM_NONE) return true;
    return false;
}

static void clear_locs(Z3Seed *s, const List *locs)
{
    for (int i = 0; i < locs->n; i++) s->w.item_at[locs->v[i]] = Z3_ITEM_NONE;
}

/* Randomizer::fillPrizes with shuffleCrystals, shufflePendants and crossWorld on (their defaults). */
static int fill_prizes(Z3Seed *s, Z3Rng *r, int attempts)
{
    static const uint8_t all[] = { Z3_ITEM_Crystal1, Z3_ITEM_Crystal2, Z3_ITEM_Crystal3, Z3_ITEM_Crystal4,
        Z3_ITEM_Crystal5, Z3_ITEM_Crystal6, Z3_ITEM_Crystal7, Z3_ITEM_PendantOfCourage, Z3_ITEM_PendantOfPower,
        Z3_ITEM_PendantOfWisdom };
    List prize_locs = { .n = 0 }, order, crystal = { .n = 0 }, pendant = { .n = 0 }, remaining = { .n = 0 };
    for (int l = 0; l < Z3_LOC_COUNT; l++)
        if (strncmp(Z3_LOCS[l].kind, "Prize", 5) == 0) push(&prize_locs, l);   /* Prize\Event is a Prize too */
    random_collection(&prize_locs, 15, &order, r);
    bool placed[Z3_ITEM_COUNT] = { false };
    for (int i = 0; i < order.n; i++) {
        int l = order.v[i];
        if (is_crystal_prize(l)) push(&crystal, l);
        if (is_pendant_prize(l)) push(&pendant, l);
        if (s->w.item_at[l] != Z3_ITEM_NONE) placed[s->w.item_at[l]] = true;
    }
    for (int i = 0; i < 10; i++)
        if (!placed[all[i]]) push(&remaining, all[i]);   /* array_diff against the placed prizes */
    shuffle(&remaining, r);

    List empty = { .n = 0 };
    for (int i = 0; i < crystal.n; i++)
        if (s->w.item_at[crystal.v[i]] == Z3_ITEM_NONE) push(&empty, crystal.v[i]);
    if (!place_prizes(s, r, &empty, &remaining) || any_empty(s, &crystal)) {
        if (attempts <= 0) return -1;
        clear_locs(s, &empty);
        return fill_prizes(s, r, attempts - 1);
    }
    empty.n = 0;
    for (int i = 0; i < pendant.n; i++)
        if (s->w.item_at[pendant.v[i]] == Z3_ITEM_NONE) push(&empty, pendant.v[i]);
    if (!place_prizes(s, r, &empty, &remaining) || any_empty(s, &pendant)) {
        if (attempts <= 0) return -1;
        clear_locs(s, &empty);
        return fill_prizes(s, r, attempts - 1);
    }
    return 0;
}

/* Moves every item of class c from dungeon to adv (the wild dungeon item switches); KeyH2 stays in Standard. */
static void move_class(const Z3Cfg *c, List *dungeon, List *adv, int ic)
{
    for (int i = 0; i < dungeon->n;) {
        int it = dungeon->v[i];
        bool keep = ic == Z3_IC_Key && c->mode_state == Z3_MODE_STATE_STANDARD && it == Z3_ITEM_KeyH2
            && c->logic != Z3_LOGIC_NOLOGIC;
        if (cls(it) == ic && !keep) {
            push(adv, it);
            del(dungeon, i);
        } else {
            i++;
        }
    }
}

/* Randomizer::prepareWorld: pools, the world's fixed parts, then swords, bottles and keys sorted into the pools. */
static int prepare_world(Z3Seed *s, Z3Rng *r, List *dungeon, List *adv, List *nice, List *trash)
{
    Z3World *w = &s->w;
    const Z3Cfg *c = &w->cfg;
    if (c->goal == Z3_GOAL_COMPLETIONIST) w->cfg.accessibility = Z3_ACCESSIBILITY_LOCATIONS;
    if (c->goal == Z3_GOAL_PEDESTAL) w->item_at[Z3_LOC_Master_Sword_Pedestal] = Z3_ITEM_Triforce;
    else if (c->goal != Z3_GOAL_TRIFORCE_HUNT) w->item_at[Z3_LOC_Ganon] = Z3_ITEM_Triforce;

    POOL(w, r, DUNGEON, dungeon);
    POOL(w, r, ADVANCEMENT, adv);
    POOL(w, r, NICE, nice);
    POOL(w, r, JUNK, trash);

    if (c->logic != Z3_LOGIC_NOGLITCHES) {   /* glitched logic starts with the boots */
        z3_items_add(&w->pre, Z3_ITEM_PegasusBoots, 1);
        for (int i = 0; i < adv->n; i++)
            if (adv->v[i] == Z3_ITEM_PegasusBoots) {
                del(adv, i);
                push(trash, Z3_ITEM_TwentyRupees);
                break;
            }
    }
    if (c->mode_state != Z3_MODE_STATE_STANDARD) z3_items_add(&w->pre, Z3_ITEM_RescueZelda, 1);

    set_shops(w);
    set_medallions(w, r);
    /* placeBosses with bossShuffle none: z3_world_defaults holds the dungeon bosses; it ends with these two. */
    w->boss_of[Z3_REG_Hyrule_Castle_Tower] = Z3_BOSS_Agahnim;
    w->boss_of[Z3_REG_Ganons_Tower] = Z3_BOSS_Agahnim2;
    if (fill_prizes(s, r, 5)) return -1;
    set_fountains(w, r);
    shuffle_prize_packs(s, r);
    w->item_at[Z3_LOC_Pyramid_Fairy_Bow] = Z3_ITEM_BowAndArrows;

    List swords = { .n = 0 }, bottles = { .n = 0 }, health = { .n = 0 }, armors = { .n = 0 };
    for (int i = 0; i < adv->n;) {
        int it = adv->v[i];
        if (it == Z3_ITEM_SilverArrowUpgrade) push(nice, it);
        else if (cls(it) == Z3_IC_Sword) push(&swords, it);
        else if (cls(it) == Z3_IC_Bottle) push(&bottles, it);
        else { i++; continue; }
        del(adv, i);
    }
    for (int i = 0; i < nice->n;) {
        int it = nice->v[i];
        if (cls(it) == Z3_IC_Sword) push(&swords, it);
        else if (cls(it) == Z3_IC_Upgrade_Health) push(&health, it);
        else if (cls(it) == Z3_IC_Armor) push(&armors, it);
        else { i++; continue; }
        del(nice, i);
    }
    for (int i = 0; i < trash->n;) {
        if (cls(trash->v[i]) == Z3_IC_Upgrade_Health) {
            push(&health, trash->v[i]);
            del(trash, i);
        } else {
            i++;
        }
    }
    List *dst = c->itemPlacement == Z3_ITEMPLACEMENT_BASIC ? adv : nice;
    append(dst, &health);
    append(dst, &armors);

    if (c->mode_weapons == Z3_MODE_WEAPONS_SWORDLESS) {
        for (int i = 0; i < swords.n; i++) push(nice, Z3_ITEM_TwentyRupees2);
        Z3Items have, with_adv;
        z3_collect(w, &have);
        with_adv = have;
        for (int i = 0; i < adv->n; i++) z3_items_add(&with_adv, adv->v[i], 1);
        if (!z3_has(&with_adv, Z3_ITEM_ProgressiveBow, 2) && !have.n[Z3_ITEM_SilverArrowUpgrade]
            && !have.n[Z3_ITEM_BowAndSilverArrows]) {
            bool in_nice = false;
            for (int i = 0; i < nice->n; i++) in_nice |= nice->v[i] == Z3_ITEM_SilverArrowUpgrade;
            if (!in_nice) push(adv, Z3_ITEM_SilverArrowUpgrade);
        }
    } else if (c->mode_weapons == Z3_MODE_WEAPONS_VANILLA) {
        s->uncle_target = (uint8_t)pop(&swords);
        w->item_at[Z3_LOC_Links_Uncle] = Z3_ITEM_UncleSword;
        w->item_at[Z3_LOC_Pyramid_Fairy_Left] = (uint8_t)pop(&swords);
        w->item_at[Z3_LOC_Blacksmith] = (uint8_t)pop(&swords);
        if (w->item_at[Z3_LOC_Master_Sword_Pedestal] != Z3_ITEM_Triforce) {
            w->item_at[Z3_LOC_Master_Sword_Pedestal] = (uint8_t)pop(&swords);
        } else {
            pop(&swords);
            push(trash, Z3_ITEM_TwentyRupees);
        }
    } else {
        if (swords.n) {   /* Uncle's sword: held from the start (assured) or placed */
            s->uncle_target = (uint8_t)pop(&swords);
            if (c->mode_weapons == Z3_MODE_WEAPONS_ASSURED) {
                z3_items_add(&w->pre, Z3_ITEM_UncleSword, 1);
                push(trash, Z3_ITEM_FiftyRupees);
            } else {
                push(adv, Z3_ITEM_UncleSword);
            }
        }
        if (swords.n) push(adv, pop(&swords));   /* the Master Sword */
        if (c->region_requireBetterSword && swords.n) push(adv, pop(&swords));
        append(nice, &swords);
    }
    if (bottles.n) push(adv, pop(&bottles));
    append(nice, &bottles);

    if (c->region_forceSkullWoodsKey)
        for (int i = 0; i < dungeon->n; i++)
            if (dungeon->v[i] == Z3_ITEM_KeyD3) {
                del(dungeon, i);
                w->item_at[Z3_LOC_Skull_Woods_Pinball_Room] = Z3_ITEM_KeyD3;
                break;
            }
    if (c->region_wildBigKeys) move_class(c, dungeon, adv, Z3_IC_BigKey);
    if (c->region_wildKeys) move_class(c, dungeon, adv, Z3_IC_Key);
    if (c->region_wildMaps) move_class(c, dungeon, adv, Z3_IC_Map);
    if (c->region_wildCompasses) move_class(c, dungeon, adv, Z3_IC_Compass);

    shuffle(adv, r);
    shuffle(nice, r);
    shuffle(trash, r);
    memcpy(s->adv, adv->v, (size_t)adv->n);
    s->n_adv = adv->n;
    return 0;
}

static void empty_of(const Z3Seed *s, const List *in, List *out)
{
    out->n = 0;
    for (int i = 0; i < in->n; i++)
        if (s->w.item_at[in->v[i]] == Z3_ITEM_NONE) push(out, in->v[i]);
}

/* RandomAssumed::fillItemsInLocations: each item goes to the first location (a random one for maps and compasses)
   it can fill assuming every item not yet placed, plus base, is in hand. */
static int fill_items(Z3Seed *s, Z3Rng *r, const List *items, const List *locs, const List *base)
{
    Z3World *w = &s->w;
    const Z3Cfg *c = &w->cfg;
    List empty;
    empty_of(s, locs, &empty);
    if (items->n > empty.n) return -3;
    Z3Items remaining;
    to_items(items, &remaining);
    for (int i = 0; i < items->n; i++) {
        int item = items->v[i];
        if (remaining.n[item]) remaining.n[item]--;   /* removeItem by name */
        Z3Items assumed = remaining;
        for (int k = 0; base && k < base->n; k++) z3_items_add(&assumed, base->v[k], 1);
        reset_collected(s);
        for (int k = 0; k < Z3_ITEM_COUNT; k++) z3_items_add(&assumed, k, w->pre.n[k]);
        for (int found = 0;;) {   /* one world: collectOtherItems until the collected count stops growing */
            Z3Items more;
            z3_collect_other(w, &assumed, &more);
            for (int k = 0; k < Z3_ITEM_COUNT; k++) z3_items_add(&assumed, k, more.n[k]);
            if (found == s->col.count) break;
            found = s->col.count;
        }
        int ic = cls(item);
        bool skip = c->accessibility == Z3_ACCESSIBILITY_NONE && (ic != Z3_IC_Key || c->region_wildKeys)
            && (ic != Z3_IC_BigKey || c->region_wildBigKeys) && (ic != Z3_IC_Map || c->region_wildMaps)
            && (ic != Z3_IC_Compass || c->region_wildCompasses) && z3_win_condition(w, &assumed);
        bool pick_random = ic == Z3_IC_Compass || ic == Z3_IC_Map;
        List ok = { .n = 0 };
        for (int k = 0; k < locs->n; k++) {
            int l = locs->v[k];
            if (w->item_at[l] != Z3_ITEM_NONE || !z3_loc_can_fill(w, &assumed, l, item, !skip)) continue;
            push(&ok, l);
            if (!pick_random) break;   /* the rest of the filter has no lasting effect */
        }
        if (!ok.n) return -2;
        w->item_at[ok.v[pick_random ? z3_rng_int(r, 0, ok.n - 1) : 0]] = (uint8_t)item;
    }
    return 0;
}

/* Filler::fastFillItemsInLocations: array_pop onto each empty location until the items run out. */
static void fast_fill(Z3Seed *s, List *items, const List *locs)
{
    for (int i = 0; i < locs->n; i++) {
        if (s->w.item_at[locs->v[i]] != Z3_ITEM_NONE) continue;
        if (!items->n) break;
        s->w.item_at[locs->v[i]] = (uint8_t)pop(items);
    }
}

/* World::getGanonsTowerJunkFillRange (floors of the crystal counts). */
static void gt_junk_range(const Z3Cfg *c, int *lo, int *hi)
{
    *lo = *hi = 0;
    if (c->logic == Z3_LOGIC_NOLOGIC || (c->mode_state != Z3_MODE_STATE_INVERTED && c->logic != Z3_LOGIC_NOGLITCHES))
        return;
    if (c->goal == Z3_GOAL_TRIFORCE_HUNT || c->goal == Z3_GOAL_PEDESTAL) {
        *lo = 15 * c->crystals_tower / 7;
        *hi = 25 * c->crystals_tower / 7;
    } else {
        *hi = 15 * c->crystals_tower / 7;
    }
}

int z3_fill(Z3Seed *s, Z3Rng *r)
{
    static List dungeon, adv, nice, trash;   /* static: the 3DS stack is small */
    static List order, tmp, gt, gt_empty, part;
    int e = prepare_world(s, r, &dungeon, &adv, &nice, &trash);
    if (e) return e;

    List all = { .n = 0 };
    for (int l = 0; l < Z3_LOC_COUNT; l++)
        if (is_empty(s, l)) push(&all, l);
    random_collection(&all, all.n, &order, r);

    List base = adv;
    append(&base, &nice);
    if ((e = fill_items(s, r, &dungeon, &order, &base))) return e;

    int lo, hi;
    gt_junk_range(&s->w.cfg, &lo, &hi);
    gt_empty.n = 0;
    for (int l = 0; l < Z3_LOC_COUNT; l++)
        if (Z3_LOCS[l].region == Z3_REG_Ganons_Tower && is_empty(s, l)) push(&gt_empty, l);
    random_collection(&gt_empty, z3_rng_int(r, lo, hi), &gt, r);
    shuffle(&trash, r);
    part.n = 0;   /* array_splice($extra, 0, count): the first gt.n items */
    for (int i = 0; i < gt.n && trash.n; i++) {
        push(&part, trash.v[0]);
        del(&trash, 0);
    }
    fast_fill(s, &part, &gt);

    empty_of(s, &order, &tmp);
    for (int i = 0; i < tmp.n; i++) order.v[i] = tmp.v[tmp.n - 1 - i];   /* ->reverse() */
    order.n = tmp.n;
    shuffle(&adv, r);
    if ((e = fill_items(s, r, &adv, &order, NULL))) return e;

    empty_of(s, &order, &tmp);
    random_collection(&tmp, tmp.n, &order, r);
    shuffle(&nice, r);
    fast_fill(s, &nice, &order);
    shuffle(&trash, r);
    empty_of(s, &order, &tmp);
    fast_fill(s, &trash, &tmp);
    return 0;
}

/* The fresh sweep of WorldCollection::isWinnable, leaving s->col marked with every location reached. */
static void sweep_from_start(Z3Seed *s, Z3Items *assumed)
{
    reset_collected(s);
    *assumed = s->w.pre;
    for (int found = 0;;) {
        Z3Items more;
        z3_collect_other(&s->w, assumed, &more);
        for (int k = 0; k < Z3_ITEM_COUNT; k++) z3_items_add(assumed, k, more.n[k]);
        if (found == s->col.count) break;
        found = s->col.count;
    }
}

bool z3_seed_winnable(Z3Seed *s)
{
    Z3Items assumed;
    sweep_from_start(s, &assumed);
    return z3_win_condition(&s->w, &assumed);
}

bool z3_seed_all_reachable(Z3Seed *s)
{
    Z3Items assumed;
    sweep_from_start(s, &assumed);
    for (int l = 0; l < Z3_LOC_COUNT; l++)
        if (s->w.item_at[l] != Z3_ITEM_NONE && z3_loc_collectable(l) && z3_loc_exists(&s->w, l) && !s->col.loc[l])
            return false;
    return true;
}
