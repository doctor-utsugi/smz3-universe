#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "smz3_fill.h"
#include "smz3_world_gen.h"
#include "smz3_itemopts.h"

#define MAXI 640
typedef struct { SmzItem v[MAXI]; int n; } List;
typedef struct { int v[512]; int n; } LocList;

static int region_of_loc[512];
static int loc_weight_explicit[512];
static int loc_index[512];

static void tables(void)
{
    for (int i = 0; i < SMZ_LOC_COUNT; i++) {
        region_of_loc[SMZ_LOCS[i].id] = SMZ_LOCS[i].region;
        loc_weight_explicit[SMZ_LOCS[i].id] = SMZ_LOCS[i].weight;
        loc_index[SMZ_LOCS[i].id] = i;
    }
}

void smz_settings_defaults(SmzSettings *s)
{
    memset(s, 0, sizeof *s);
    s->ganon_crystals = 7;
    s->gt_crystals = 7;
    s->tourian_bosses = 4;
    s->randomized_bottles = 1;
    s->cfg.WallJumpDifficulty = 2;   /* LogicConfig default: Medium */
    s->spazers_in_pool = 40;         /* GameModeOptions defaults */
    s->spazers_required = 30;
}

void smz_settings_normalize(SmzSettings *s)
{
    if (s->game_mode == MODE_AllDungeons || s->game_mode == MODE_SpazerHunt) {
        s->ganon_crystals = 7;
        s->tourian_bosses = 4;
    }
}

/* ---- lists */
static uint16_t next_id;
static void add(List *l, int type, int prog) { SmzItem *i = &l->v[l->n++]; i->type = type; i->progression = prog; i->id = next_id++; }
static void copies(List *l, int n, int type, int prog) { while (n-- > 0) add(l, type, prog); }
static void list_remove_id(List *l, uint16_t id)
{
    for (int i = 0; i < l->n; i++)
        if (l->v[i].id == id) { memmove(&l->v[i], &l->v[i + 1], (l->n - i - 1) * sizeof(SmzItem)); l->n--; return; }
}
static void list_remove_index(List *l, int i) { memmove(&l->v[i], &l->v[i + 1], (l->n - i - 1) * sizeof(SmzItem)); l->n--; }
static int list_find_type(const List *l, int type)
{
    for (int i = 0; i < l->n; i++) if (l->v[i].type == type) return i;
    return -1;
}
static void append(List *to, const List *from) { for (int i = 0; i < from->n; i++) to->v[to->n++] = from->v[i]; }

static void shuffle_items(List *l, SmzRng *r)
{
    int n = l->n;
    while ((n -= 1) > 0) {
        int k = rng_next_max(r, n + 1);
        SmzItem t = l->v[n]; l->v[n] = l->v[k]; l->v[k] = t;
    }
}
static void shuffle_locs(LocList *l, SmzRng *r)
{
    int n = l->n;
    while ((n -= 1) > 0) {
        int k = rng_next_max(r, n + 1);
        int t = l->v[n]; l->v[n] = l->v[k]; l->v[k] = t;
    }
}

/* ---- item pools (WorldItemPools) */
typedef struct { List progression, nice, junk, dungeon, keycards; } Pools;

static void make_pools(Pools *p, const SmzWorld *w, int keysanity_metroid, const int *start, int n_start)
{
    memset(p, 0, sizeof *p);
    List *g = &p->progression;
    static const int prog1[] = {
        ITEM_ProgressiveShield, ITEM_ProgressiveShield, ITEM_ProgressiveShield, ITEM_ProgressiveSword, ITEM_ProgressiveSword,
        ITEM_Bow, ITEM_Hookshot, ITEM_Mushroom, ITEM_Powder, ITEM_Firerod, ITEM_Icerod, ITEM_Bombos, ITEM_Ether, ITEM_Quake,
        ITEM_Lamp, ITEM_Hammer, ITEM_Shovel, ITEM_Flute, ITEM_Book, ITEM_Bottle, ITEM_Somaria, ITEM_Byrna, ITEM_Cape, ITEM_Mirror,
        ITEM_Boots, ITEM_ProgressiveGlove, ITEM_ProgressiveGlove, ITEM_Flippers, ITEM_MoonPearl, ITEM_HalfMagic,
        ITEM_Grapple, ITEM_Charge, ITEM_Ice, ITEM_Wave, ITEM_Plasma, ITEM_Varia, ITEM_Gravity, ITEM_Morph, ITEM_Bombs,
        ITEM_SpringBall, ITEM_ScrewAttack, ITEM_HiJump, ITEM_SpaceJump, ITEM_SpeedBooster,
        ITEM_Missile, ITEM_Super, ITEM_PowerBomb, ITEM_PowerBomb,
        ITEM_ETank, ITEM_ETank, ITEM_ETank, ITEM_ETank, ITEM_ETank,
        ITEM_ReserveTank, ITEM_ReserveTank, ITEM_ReserveTank, ITEM_ReserveTank,
        ITEM_HeartContainer, ITEM_HeartContainer, ITEM_HeartContainer,
        ITEM_ThreeHundredRupees, ITEM_ThreeHundredRupees,
    };
    for (size_t i = 0; i < sizeof prog1 / sizeof *prog1; i++) add(g, prog1[i], 1);

    List *n = &p->nice;
    add(n, ITEM_ProgressiveTunic, 0); add(n, ITEM_ProgressiveTunic, 0);
    add(n, ITEM_ProgressiveSword, 0); add(n, ITEM_ProgressiveSword, 0);
    add(n, ITEM_SilverArrows, 0); add(n, ITEM_BlueBoomerang, 0); add(n, ITEM_RedBoomerang, 0);
    add(n, ITEM_Bottle, 0); add(n, ITEM_Bottle, 0); add(n, ITEM_Bottle, 0);
    add(n, ITEM_Bugnet, 0); add(n, ITEM_HeartContainerRefill, 0);
    add(n, ITEM_Spazer, 0); add(n, ITEM_XRay, 0);
    if (w->cfg.QuarterMagic) add(n, ITEM_HalfMagic, 0);
    copies(n, 7, ITEM_HeartContainer, 0);

    List *d = &p->dungeon;
    static const int bigkeys[] = { ITEM_BigKeyEP, ITEM_BigKeyDP, ITEM_BigKeyTH, ITEM_BigKeyPD, ITEM_BigKeySP, ITEM_BigKeySW,
                                   ITEM_BigKeyTT, ITEM_BigKeyIP, ITEM_BigKeyMM, ITEM_BigKeyTR, ITEM_BigKeyGT };
    for (size_t i = 0; i < sizeof bigkeys / sizeof *bigkeys; i++) add(d, bigkeys[i], 0);
    copies(d, 1, ITEM_KeyHC, 0); copies(d, 2, ITEM_KeyCT, 0); copies(d, 1, ITEM_KeyDP, 0); copies(d, 1, ITEM_KeyTH, 0);
    copies(d, 6, ITEM_KeyPD, 0); copies(d, 1, ITEM_KeySP, 0); copies(d, 3, ITEM_KeySW, 0); copies(d, 1, ITEM_KeyTT, 0);
    copies(d, 2, ITEM_KeyIP, 0); copies(d, 3, ITEM_KeyMM, 0); copies(d, 4, ITEM_KeyTR, 0); copies(d, 4, ITEM_KeyGT, 0);
    static const int maps[] = { ITEM_MapEP, ITEM_MapDP, ITEM_MapTH, ITEM_MapPD, ITEM_MapSP, ITEM_MapSW, ITEM_MapTT, ITEM_MapIP, ITEM_MapMM, ITEM_MapTR };
    for (size_t i = 0; i < sizeof maps / sizeof *maps; i++) add(d, maps[i], 0);
    if (!keysanity_metroid) {
        add(d, ITEM_MapHC, 0); add(d, ITEM_MapGT, 0);
        static const int compasses[] = { ITEM_CompassEP, ITEM_CompassDP, ITEM_CompassTH, ITEM_CompassPD, ITEM_CompassSP, ITEM_CompassSW,
                                         ITEM_CompassTT, ITEM_CompassIP, ITEM_CompassMM, ITEM_CompassTR, ITEM_CompassGT };
        for (size_t i = 0; i < sizeof compasses / sizeof *compasses; i++) add(d, compasses[i], 0);
    }

    List *j = &p->junk;
    add(j, ITEM_Arrow, 0); add(j, ITEM_OneHundredRupees, 0);
    copies(j, 24, ITEM_HeartPiece, 0); copies(j, 8, ITEM_TenArrows, 0); copies(j, 13, ITEM_ThreeBombs, 0);
    copies(j, 4, ITEM_ArrowUpgrade5, 0); copies(j, 4, ITEM_BombUpgrade5, 0); copies(j, 2, ITEM_OneRupee, 0);
    copies(j, 4, ITEM_FiveRupees, 0); copies(j, 7, ITEM_FiftyRupees, 0); copies(j, 3, ITEM_ThreeHundredRupees, 0);
    copies(j, 9, ITEM_ETank, 0); copies(j, 39, ITEM_Missile, 0); copies(j, 15, ITEM_Super, 0); copies(j, 8, ITEM_PowerBomb, 0);

    List *k = &p->keycards;
    static const int cards[] = { ITEM_CardCrateriaL1, ITEM_CardCrateriaL2, ITEM_CardCrateriaBoss, ITEM_CardBrinstarL1, ITEM_CardBrinstarL2,
                                 ITEM_CardBrinstarBoss, ITEM_CardNorfairL1, ITEM_CardNorfairL2, ITEM_CardNorfairBoss, ITEM_CardMaridiaL1,
                                 ITEM_CardMaridiaL2, ITEM_CardMaridiaBoss, ITEM_CardWreckedShipL1, ITEM_CardWreckedShipBoss,
                                 ITEM_CardLowerNorfairL1, ITEM_CardLowerNorfairBoss };
    for (size_t i = 0; i < sizeof cards / sizeof *cards; i++) add(k, cards[i], 0);

    /* Remove starting inventory items: the first match in progression, nice, junk, dungeon, then keycards */
    for (int i = 0; i < n_start; i++) {
        List *order[] = { &p->progression, &p->nice, &p->junk, &p->dungeon, &p->keycards };
        for (int g = 0; g < 5; g++) {
            int k = list_find_type(order[g], start[i]);
            if (k >= 0) { list_remove_index(order[g], k); break; }
        }
    }

    int count = p->progression.n + p->nice.n + p->dungeon.n + p->junk.n;
    if (keysanity_metroid) count += p->keycards.n;
    if (count < SMZ_LOC_COUNT) copies(j, SMZ_LOC_COUNT - count, ITEM_TwentyRupees, 0);
}


/* ---- game modes (GameModeBase.UpdateWorld): item pool swaps, WorldItemPools.SwapItems */
typedef struct { int from, to, nice; } Swap;

static void swap_items(Pools *p, const Swap *sw, int n)
{
    for (int i = 0; i < n; i++) {
        List *order[] = { &p->progression, &p->nice, &p->junk };
        int g, k = -1;
        for (g = 0; g < 3; g++) if ((k = list_find_type(order[g], sw[i].from)) >= 0) break;
        if (k < 0) continue;                       /* the C# throws; the generator never asks for a missing item */
        list_remove_index(order[g], k);
        add(sw[i].nice ? &p->nice : &p->junk, sw[i].to, 0);
    }
}

static int count_type(const List *l, int type) { int n = 0; for (int i = 0; i < l->n; i++) n += l->v[i].type == type; return n; }

static void spazer_hunt_pools(Pools *p, int in_pool)
{
    static Swap sw[512];
    int n = 0;
    int to_spawn = in_pool - (count_type(&p->progression, ITEM_Spazer) + count_type(&p->nice, ITEM_Spazer) +
                              count_type(&p->junk, ITEM_Spazer) + count_type(&p->dungeon, ITEM_Spazer));

    /* first the really useless items people won't miss */
    static const int useless[] = { ITEM_TwentyRupees, ITEM_TwentyRupees2, ITEM_Arrow, ITEM_TenArrows, ITEM_FiveRupees, ITEM_OneRupee };
    for (int i = 0; i < p->junk.n; i++) {
        int t = p->junk.v[i].type, hit = 0;
        for (unsigned u = 0; u < sizeof useless / sizeof *useless; u++) hit |= t == useless[u];
        if (!hit) continue;
        sw[n++] = (Swap){ t, ITEM_Spazer, 1 };
        if (--to_spawn <= 0) break;
    }

    /* then heart pieces become a heart container and three spazers */
    if (to_spawn >= 3) {
        int pieces = count_type(&p->junk, ITEM_HeartPiece);
        for (int i = 0; i < pieces; i += 4) {
            sw[n++] = (Swap){ ITEM_HeartPiece, ITEM_HeartContainer, 0 };
            for (int k = 0; k < 3; k++) sw[n++] = (Swap){ ITEM_HeartPiece, ITEM_Spazer, 1 };
            to_spawn -= 3;
            if (to_spawn < 3) break;
        }
    }

    /* larger rupee and three-bomb drops */
    if (to_spawn > 0) {
        for (int i = 0; i < p->junk.n; i++) {
            int t = p->junk.v[i].type;
            if (t != ITEM_FiftyRupees && t != ITEM_OneHundredRupees && t != ITEM_ThreeBombs) continue;
            sw[n++] = (Swap){ t, ITEM_Spazer, 1 };
            if (--to_spawn <= 0) break;
        }
    }

    /* lastly bomb and arrow upgrades and some missiles */
    static const int last[] = { ITEM_BombUpgrade5, ITEM_ArrowUpgrade5, ITEM_Missile, ITEM_Missile, ITEM_Missile, ITEM_Missile,
                                ITEM_BombUpgrade5, ITEM_ArrowUpgrade5, ITEM_Missile, ITEM_Missile, ITEM_Missile, ITEM_Missile,
                                ITEM_BombUpgrade5, ITEM_ArrowUpgrade5, ITEM_BombUpgrade5, ITEM_ArrowUpgrade5 };
    for (int i = 0; i < to_spawn && i < (int)(sizeof last / sizeof *last); i++) sw[n++] = (Swap){ last[i], ITEM_Spazer, 1 };

    swap_items(p, sw, n);
}

/* ---- progression helpers */
static void prog_add(SmzProg *p, int type) { p->items[type]++; }

typedef struct { const SmzSettings *s; SmzSeed *seed; SmzWorld *w; SmzRng *rng; int fail; } Ctx;

static bool item_dungeon(int t) { return item_is_dungeon(t); }

static bool matches_rule(const Ctx *c, int region, int type, int progression)
{
    const SmzWorld *w = c->w;
    uint32_t cat = SMZ_ITEM_CATS[type];
    int is_key = (cat & ICAT_SmallKey) != 0, is_big = (cat & ICAT_BigKey) != 0, is_card = (cat & ICAT_Keycard) != 0;
    int rule = c->s->placement_rule;
    if (rule == RULE_Anywhere || (!progression && !is_key && !is_card && !is_big) || (!w->zelda_keysanity && (is_key || is_big))) return true;
    int sm = SMZ_REGIONS[region].kind == RK_SM;
    switch (rule) {
    case RULE_DungeonsAndMetroid: return (!sm && !SMZ_REGIONS[region].overworld) || sm;
    case RULE_CrystalDungeonsAndMetroid:
        return (SMZ_REGIONS[region].has_reward && (w->reward_of[region] == REWARD_CrystalBlue || w->reward_of[region] == REWARD_CrystalRed)) || sm;
    case RULE_OppositeGame: return ((cat & ICAT_Zelda) && sm) || ((cat & ICAT_Metroid) && !sm);
    case RULE_SameGame: return ((cat & ICAT_Zelda) && !sm) || ((cat & ICAT_Metroid) && sm);
    default: return true;
    }
}

static bool region_can_fill(const Ctx *c, int region, int type, int progression)
{
    return (c->w->zelda_keysanity || !item_dungeon(type) || region_has_item(region, type)) && matches_rule(c, region, type, progression);
}

static bool loc_can_fill(Ctx *c, int loc, const SmzItem *item, const SmzProg *wp)
{
    SmzWorld *w = c->w;
    uint8_t old = w->item_at[loc], oldp = c->seed->item_prog[loc];
    w->item_at[loc] = item->type;
    c->seed->item_prog[loc] = item->progression;
    int region = region_of_loc[loc];
    uint32_t cat = SMZ_ITEM_CATS[item->type];
    int custom = (item->progression || (item_dungeon(item->type) && w->zelda_keysanity) || ((cat & ICAT_Keycard) && w->metroid_keysanity)) &&
                 c->s->placement_rule != RULE_Anywhere && loc_weight_explicit[loc] != 0 && loc_weight_explicit[loc] <= -10;
    bool fillable = loc_always_allow(w, loc, item->type, wp) ||
                    ((region_can_fill(c, region, item->type, item->progression) || custom) && loc_allow(w, loc, item->type, wp) && loc_available(w, loc, wp));
    if (item->type == ITEM_BigKeyGT && w->zelda_keysanity && c->s->gt_bigkey_in_gt) fillable = fillable && region == REG_GanonsTower;
    w->item_at[loc] = old;
    c->seed->item_prog[loc] = oldp;
    return fillable;
}

/* CollectItems: everything reachable when the given items are owned and every filled location is opened in turn. */
static int g_inv[1024], g_ninv;

static void collect_items(Ctx *c, const SmzItem *items, int n, SmzProg *out)
{
    memset(out, 0, sizeof *out);
    g_ninv = 0;
    for (int i = 0; i < n; i++) { prog_add(out, items[i].type); g_inv[g_ninv++] = items[i].type; }
    int remaining[512], nr = 0;
    for (int i = 0; i < SMZ_LOC_COUNT; i++) if (c->w->item_at[SMZ_LOCS[i].id] != ITEM_Nothing) remaining[nr++] = SMZ_LOCS[i].id;
    for (;;) {
        int found[512], nf = 0, keep[512], nk = 0;
        for (int i = 0; i < nr; i++) {
            if (loc_available(c->w, remaining[i], out)) found[nf++] = remaining[i];
            else keep[nk++] = remaining[i];
        }
        if (nf == 0) break;
        for (int i = 0; i < nf; i++) { prog_add(out, c->w->item_at[found[i]]); g_inv[g_ninv++] = c->w->item_at[found[i]]; }
        memcpy(remaining, keep, nk * sizeof(int));
        nr = nk;
    }
}

static bool golden(int b) { return b == BOSS_Kraid || b == BOSS_Phantoon || b == BOSS_Draygon || b == BOSS_Ridley; }

static void collect_rewards_bosses(Ctx *c, SmzProg *items_only, SmzProg *with_rewards)
{
    const SmzWorld *w = c->w;
    SmzProg rp = *items_only;
    memset(rp.rewards, 0, sizeof rp.rewards);
    memset(rp.bosses, 0, sizeof rp.bosses);
    for (int r = 0; r < REG_COUNT; r++)
        if (SMZ_REGIONS[r].has_reward && region_can_retrieve_reward(w, r, &rp, false)) with_rewards->rewards[w->reward_of[r]]++;
    SmzProg bp = *items_only;
    memset(bp.bosses, 0, sizeof bp.bosses);
    memcpy(bp.rewards, with_rewards->rewards, sizeof bp.rewards);
    for (int r = 0; r < REG_COUNT; r++)
        if (SMZ_REGIONS[r].default_boss != BOSS_None && golden(w->boss_of[r]) && region_can_beat_boss(w, r, &bp, false)) with_rewards->bosses[w->boss_of[r]] = 1;
}

static void place(Ctx *c, int loc, const SmzItem *it)
{
    if (getenv("SMZ_TRACE")) fprintf(stderr, "T %d %d\n", loc, it->type);
    c->w->item_at[loc] = it->type;
    c->seed->item_prog[loc] = it->progression;
}

/* AssumedFill: returns 0, or -1 when an item cannot be placed anywhere after many attempts. */
static int assumed_fill(Ctx *c, List *pool, const List *initial, LocList *locations)
{
    List todo = *pool;
    int failed[MAXI] = {0};
    static uint16_t unused;
    (void)unused;
    while (todo.n > 0) {
        SmzItem item = todo.v[0];
        memmove(&todo.v[0], &todo.v[1], (todo.n - 1) * sizeof(SmzItem));
        todo.n--;

        SmzItem inv[MAXI * 2];
        int ni = 0;
        for (int i = 0; i < todo.n; i++) inv[ni++] = todo.v[i];
        for (int i = 0; i < initial->n; i++) inv[ni++] = initial->v[i];
        SmzProg base;
        collect_items(c, inv, ni, &base);
        /* rewards and bosses that can be retrieved with this inventory */
        SmzProg env = base;
        memset(env.rewards, 0, sizeof env.rewards);
        memset(env.bosses, 0, sizeof env.bosses);
        collect_rewards_bosses(c, &base, &env);

        if (getenv("SMZ_INV")) {
            int srt[1024]; memcpy(srt, g_inv, g_ninv * sizeof(int));
            for (int a = 1; a < g_ninv; a++) { int v = srt[a], b = a - 1; while (b >= 0 && srt[b] > v) { srt[b+1] = srt[b]; b--; } srt[b+1] = v; }
            fprintf(stderr, "INV %d |", item.type);
            for (int a = 0; a < g_ninv; a++) fprintf(stderr, " %d", srt[a]);
            fprintf(stderr, " |");
            { int ids[512], k = 0; for (int a = 0; a < SMZ_LOC_COUNT; a++) if (c->w->item_at[SMZ_LOCS[a].id] != ITEM_Nothing) ids[k++] = SMZ_LOCS[a].id;
              for (int a = 1; a < k; a++) { int v = ids[a], b = a - 1; while (b >= 0 && ids[b] > v) { ids[b+1] = ids[b]; b--; } ids[b+1] = v; }
              fprintf(stderr, " |"); for (int a = 0; a < k; a++) fprintf(stderr, " %d", ids[a]); }
            fprintf(stderr, "\n");
        }
        SmzProg with_item = env;
        prog_add(&with_item, item.type);

        int chosen = -1;
        for (int i = 0; i < locations->n; i++) {
            int loc = locations->v[i];
            if (c->w->item_at[loc] != ITEM_Nothing) continue;
            bool cf = loc_can_fill(c, loc, &item, &env), av = loc_available(c->w, loc, &with_item);
            if (getenv("SMZ_WHY") && atoi(getenv("SMZ_WHY")) == loc)
                fprintf(stderr, "why: placing item %d at %d: can_fill=%d available_with_item=%d (index %d in list)\n", item.type, loc, cf, av, i);
            if (cf && av) { chosen = loc; break; }
        }
        if (chosen < 0) {
            todo.v[todo.n++] = item;
            int idx = item.id % MAXI;
            if (++failed[idx] > 500) { if (getenv("SMZ_DEBUG")) fprintf(stderr, "cannot place item type %d (todo %d)\n", item.type, todo.n); return -1; }
            continue;
        }
        place(c, chosen, &item);
        list_remove_id(pool, item.id);
        if ((SMZ_ITEM_CATS[item.type] & ICAT_BigKey)) {
            int more = 0;
            for (int i = 0; i < todo.n; i++) if (SMZ_ITEM_CATS[todo.v[i].type] & ICAT_BigKey) more = 1;
            if (!more) {   /* the C# reshuffles a copy and drops it; only the random numbers matter */
                LocList copy = *locations;
                shuffle_locs(&copy, c->rng);
            }
        }
    }
    return 0;
}

static void fast_fill(Ctx *c, List *pool, const LocList *locations)
{
    List snapshot = *pool;
    int k = 0;
    for (int i = 0; i < locations->n && k < snapshot.n; i++) {
        int loc = locations->v[i];
        if (c->w->item_at[loc] != ITEM_Nothing) continue;
        place(c, loc, &snapshot.v[k]);
        list_remove_id(pool, snapshot.v[k].id);
        k++;
    }
}

static void fill_item_at(Ctx *c, List *pool, int type, int loc)
{
    int i = list_find_type(pool, type);
    place(c, loc, &pool->v[i]);
    list_remove_id(pool, pool->v[i].id);
}

static int front_fill(Ctx *c, List *pool, int type)
{
    int i = list_find_type(pool, type);
    SmzProg p;
    memset(&p, 0, sizeof p);
    for (int l = 0; l < SMZ_LOC_COUNT; l++) prog_add(&p, c->w->item_at[SMZ_LOCS[l].id]);
    p.items[ITEM_Nothing] = 0;
    int cand[512], n = 0;
    for (int l = 0; l < SMZ_LOC_COUNT; l++) {
        int id = SMZ_LOCS[l].id;
        if (c->w->item_at[id] == ITEM_Nothing && loc_available(c->w, id, &p)) cand[n++] = id;
    }
    if (n == 0) return -1;
    int loc = cand[rng_next_max(c->rng, n)];
    place(c, loc, &pool->v[i]);
    list_remove_id(pool, pool->v[i].id);
    return 0;
}

/* World.Setup: medallions, rewards, bottles. */
static void world_setup(Ctx *c, Pools *pools)
{
    SmzWorld *w = c->w;
    for (int r = 0; r < REG_COUNT; r++) {
        if (r != REG_MiseryMire && r != REG_TurtleRock) continue;
        static const int med[] = { ITEM_Bombos, ITEM_Ether, ITEM_Quake };
        w->prereq[r] = med[rng_next_max(c->rng, 3)];
    }
    int rewards[10], n = 0;
    static const int types[] = { REWARD_PendantGreen, REWARD_PendantRed, REWARD_PendantBlue, REWARD_CrystalRed, REWARD_CrystalRed,
                                 REWARD_CrystalBlue, REWARD_CrystalBlue, REWARD_CrystalBlue, REWARD_CrystalBlue, REWARD_CrystalBlue };
    for (size_t i = 0; i < 10; i++) rewards[n++] = types[i];
    {
        int m = n;
        while ((m -= 1) > 0) {
            int k = rng_next_max(c->rng, m + 1);
            int t = rewards[m]; rewards[m] = rewards[k]; rewards[k] = t;
        }
    }
    int next = 0;
    for (int r = 0; r < REG_COUNT; r++)
        if (SMZ_REGIONS[r].has_reward && SMZ_REGIONS[r].shuffled == 1) w->reward_of[r] = rewards[next++];

    if (c->s->randomized_bottles) {
        static const int bottles[] = { ITEM_Bottle, ITEM_BottleWithBee, ITEM_BottleWithFairy, ITEM_BottleWithBluePotion,
                                       ITEM_BottleWithGoldBee, ITEM_BottleWithGreenPotion, ITEM_BottleWithRedPotion };
        List *order[] = { &pools->progression, &pools->nice, &pools->junk, &pools->dungeon, &pools->keycards };
        for (int g = 0; g < 5; g++)
            for (int i = 0; i < order[g]->n; i++)
                if (order[g]->v[i].type == ITEM_Bottle) order[g]->v[i].type = bottles[rng_next_max(c->rng, 7)];
    }
}

void smz_seed_init(SmzSeed *out, const SmzSettings *s)
{
    tables();
    memset(out, 0, sizeof *out);
    SmzWorld *w = &out->world;
    smz_world_init(w);
    w->cfg = s->cfg;
    w->zelda_keysanity = s->keysanity == KEYS_Zelda || s->keysanity == KEYS_Both;
    w->metroid_keysanity = s->keysanity == KEYS_SuperMetroid || s->keysanity == KEYS_Both;
    w->ganon_crystals = s->ganon_crystals;
    w->gt_crystals = s->gt_crystals;
    w->tourian_bosses = s->tourian_bosses;
    out->game_mode = s->game_mode;
}

int smz_fill(SmzSeed *out, const SmzSettings *s_in)
{
    SmzSettings eff = *s_in;
    smz_settings_normalize(&eff);
    const SmzSettings *s = &eff;
    next_id = 0;
    smz_seed_init(out, s);
    SmzWorld *w = &out->world;
    out->n_start = smz_start_items(s, out->start_items, SMZ_MAX_START_ITEMS);

    rng_init(&out->rng, s->seed);
    Ctx c = { s, out, w, &out->rng, 0 };
    Pools pools;
    make_pools(&pools, w, w->metroid_keysanity, out->start_items, out->n_start);
    if (s->game_mode == MODE_SpazerHunt) spazer_hunt_pools(&pools, s->spazers_in_pool);

    rng_sanitize(c.rng);
    world_setup(&c, &pools);

    List nice = pools.nice, junk = pools.junk;
    shuffle_items(&nice, c.rng);
    shuffle_items(&junk, c.rng);

    List dungeon = pools.dungeon, progression = pools.progression;
    List keycards = w->metroid_keysanity ? pools.keycards : (List){ .n = 0 };
    List assumed = { .n = 0 }, progression_items = { .n = 0 };
    for (int i = 0; i < out->n_start; i++) add(&assumed, out->start_items[i], 0);

    /* ApplyItemPoolPreferences: items the player asked to see early go to random locations reachable with what was
     * placed so far (the locations-with-items preferences of the C# config are not supported) */
    List preference = { .n = 0 };
    {
        int early[SMZ_MAX_START_ITEMS], ne = smz_early_items(s, early, SMZ_MAX_START_ITEMS);
        for (int m = ne; (m -= 1) > 0;) {   /* Shuffle(Random) */
            int k = rng_next_max(c.rng, m + 1);
            int t = early[m]; early[m] = early[k]; early[k] = t;
        }
        int added[SMZ_MAX_START_ITEMS * 2], na = 0;
        for (int i = 0; i < out->n_start; i++) added[na++] = out->start_items[i];
        for (int i = 0; i < ne; i++) {
            int type = early[i];
            List *from = list_find_type(&progression, type) >= 0 ? &progression : list_find_type(&nice, type) >= 0 ? &nice :
                         list_find_type(&junk, type) >= 0 ? &junk : NULL;
            if (!from) continue;
            SmzProg p;
            memset(&p, 0, sizeof p);
            for (int a = 0; a < na; a++) prog_add(&p, added[a]);
            LocList acc = { .n = 0 };
            for (int l = 0; l < SMZ_LOC_COUNT; l++) {
                int id = SMZ_LOCS[l].id;
                if (w->item_at[id] == ITEM_Nothing && loc_available(w, id, &p)) acc.v[acc.n++] = id;
            }
            if (acc.n == 0) return -2;
            shuffle_locs(&acc, c.rng);
            fill_item_at(&c, from, type, acc.v[0]);
            add(&preference, type, 0);
            added[na++] = type;
        }
    }

    /* InitialFillInOwnWorld */
    fill_item_at(&c, &dungeon, ITEM_KeySW, LOC_SkullWoodsPinballRoom);
    {
        int has_super = 0, has_pb = 0;
        for (int i = 0; i < out->n_start; i++) { has_super |= out->start_items[i] == ITEM_Super; has_pb |= out->start_items[i] == ITEM_PowerBomb; }
        if (!has_super && front_fill(&c, &progression, ITEM_Super) < 0) return -2;
        if (!has_pb && front_fill(&c, &progression, ITEM_PowerBomb) < 0) return -2;
    }

    List metroid_items = pools.keycards;   /* world.ItemPools.MetroidKeysanityItems */
    if (!w->zelda_keysanity) {
        LocList locs = { .n = 0 };
        for (int i = 0; i < SMZ_LOC_COUNT; i++) if (w->item_at[SMZ_LOCS[i].id] == ITEM_Nothing) locs.v[locs.n++] = SMZ_LOCS[i].id;
        shuffle_locs(&locs, c.rng);
        List initial = progression;
        append(&initial, &metroid_items);
        append(&initial, &assumed);
        append(&initial, &preference);
        if (assumed_fill(&c, &dungeon, &initial, &locs) < 0) return -3;
    } else if (s->gt_bigkey_in_gt) {
        LocList locs = { .n = 0 };
        for (int i = 0; i < SMZ_LOC_COUNT; i++) if (w->item_at[SMZ_LOCS[i].id] == ITEM_Nothing) locs.v[locs.n++] = SMZ_LOCS[i].id;
        shuffle_locs(&locs, c.rng);
        int k = list_find_type(&dungeon, ITEM_BigKeyGT);
        List gt = { .n = 0 };
        gt.v[gt.n++] = dungeon.v[k];
        list_remove_id(&dungeon, dungeon.v[k].id);
        List initial = progression;
        append(&initial, &metroid_items);
        append(&initial, &assumed);
        append(&initial, &preference);
        append(&initial, &dungeon);
        if (assumed_fill(&c, &gt, &initial, &locs) < 0) return -3;
    }

    if (w->metroid_keysanity) append(&progression_items, &keycards);
    else append(&assumed, &metroid_items);
    append(&progression_items, &dungeon);
    append(&progression_items, &progression);
    shuffle_items(&progression_items, c.rng);

    LocList locs = { .n = 0 };
    for (int i = 0; i < SMZ_LOC_COUNT; i++) if (w->item_at[SMZ_LOCS[i].id] == ITEM_Nothing) locs.v[locs.n++] = SMZ_LOCS[i].id;
    shuffle_locs(&locs, c.rng);
    {   /* ApplyLocationWeighting: stable order by index minus weight */
        int key[512] = {0}, idx[512] = {0};
        for (int i = 0; i < locs.n; i++) {
            int loc = locs.v[i];
            int weight = loc_weight_explicit[loc] ? loc_weight_explicit[loc] : SMZ_REGIONS[region_of_loc[loc]].weight;
            key[i] = i - weight;
            idx[i] = loc;
        }
        for (int i = 1; i < locs.n; i++) {   /* insertion sort is stable */
            int k = key[i], v = idx[i], j = i - 1;
            while (j >= 0 && key[j] > k) { key[j + 1] = key[j]; idx[j + 1] = idx[j]; j--; }
            key[j + 1] = k; idx[j + 1] = v;
        }
        memcpy(locs.v, idx, locs.n * sizeof(int));
    }

    {   /* GanonTowerFill */
        LocList gt = { .n = 0 };
        for (int i = 0; i < SMZ_LOC_COUNT; i++)
            if (SMZ_LOCS[i].region == REG_GanonsTower && w->item_at[SMZ_LOCS[i].id] == ITEM_Nothing) gt.v[gt.n++] = SMZ_LOCS[i].id;
        shuffle_locs(&gt, c.rng);
        int num = (int)(15.0 * s->gt_crystals / 7);
        if (num < gt.n) gt.n = num;
        fast_fill(&c, &junk, &gt);
    }

    if (getenv("SMZ_LIST")) { fprintf(stderr, "LISTORDER"); for (int i = 0; i < locs.n; i++) fprintf(stderr, " %d", locs.v[i]); fprintf(stderr, "\n"); }
    if (assumed_fill(&c, &progression_items, &assumed, &locs) < 0) return -4;
    fast_fill(&c, &nice, &locs);
    fast_fill(&c, &junk, &locs);
    return 0;
}
