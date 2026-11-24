/* The A Link to the Past daily planner (smz3daily --z3 <z3-YYYY-MM-DD-n>, --z3-stats <prefix> <n>): an Original tournament
 * seed and one goal (item, boss, crystals, dungeon, Agahnim), with smz3daily.c's estimate, hints and output format. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "z3_generate.h"
#include "z3_fill.h"
#include "smz3_z3.h"
#include "smz3_state.h"

#define MIN_EST 30.0          /* accepted search estimate, minutes (as SMZ3's) */
#define MAX_EST 90.0
#define HINTS_DONE_BY 150.0
#define FIRST_HINT 10.0
#define MAX_TRIES 400

enum { G_ITEM, G_BOSS, G_CRYSTALS, G_REACH, G_AGA };

typedef struct { int kind, target, count; char text[160]; } Goal;   /* target: a Z3_LOCS index (item, boss) or a region */
typedef struct { double at; char text[200]; } Hint;
typedef struct {
    char window[64];
    Z3GenSettings s;
    uint32_t seed;
    Goal goal;
    int steps[Z3_LOC_COUNT], n_steps;   /* Z3_LOCS indexes, in collection order */
    double est_search, est_route;
    Hint hints[64];
    int n_hints;
} Z3Challenge;

static Z3Seed g_seed;   /* the candidate's world; static, as smz_z3_rebuild's callers keep it */
#define W (&g_seed.w)

static uint64_t rng_state;
static uint32_t rnd(void) { rng_state ^= rng_state << 13; rng_state ^= rng_state >> 7; rng_state ^= rng_state << 17; return (uint32_t)(rng_state >> 11); }
static int rnd_n(int n) { return (int)(rnd() % (uint32_t)n); }

static uint64_t hash_str(const char *s)
{
    uint64_t h = 1469598103934665603ull;
    for (; *s; s++) h = (h ^ (uint8_t)*s) * 1099511628211ull;
    return h | 1;
}

/* The goal items: one of each in alttpr's normal pool. */
static const struct { int item; const char *name; } MAJOR[] = {
    {Z3_ITEM_Hookshot, "Hookshot"}, {Z3_ITEM_FireRod, "Fire Rod"}, {Z3_ITEM_IceRod, "Ice Rod"}, {Z3_ITEM_Hammer, "Hammer"},
    {Z3_ITEM_Lamp, "Lamp"}, {Z3_ITEM_MoonPearl, "Moon Pearl"}, {Z3_ITEM_Flippers, "Flippers"}, {Z3_ITEM_MagicMirror, "Magic Mirror"},
    {Z3_ITEM_PegasusBoots, "Pegasus Boots"}, {Z3_ITEM_OcarinaInactive, "Flute"}, {Z3_ITEM_BookOfMudora, "Book of Mudora"},
    {Z3_ITEM_CaneOfSomaria, "Cane of Somaria"}, {Z3_ITEM_CaneOfByrna, "Cane of Byrna"}, {Z3_ITEM_Cape, "Magic Cape"},
    {Z3_ITEM_BugCatchingNet, "Bug Net"}, {Z3_ITEM_Shovel, "Shovel"}, {Z3_ITEM_Bombos, "Bombos Medallion"},
    {Z3_ITEM_Ether, "Ether Medallion"}, {Z3_ITEM_Quake, "Quake Medallion"}};

/* An item as the hints name it: the goal items' names, keys by kind, the rest from alttpr's name split at its capitals. */
static const char *item_name(int type)
{
    static char buf[64];
    for (unsigned i = 0; i < sizeof MAJOR / sizeof MAJOR[0]; i++) if (MAJOR[i].item == type) return MAJOR[i].name;
    if (type < 0 || type >= Z3_ITEM_COUNT) return "?";
    const char *n = Z3_ITEMS[type].name;
    if (!strncmp(n, "BigKey", 6)) return "Big Key";
    if (!strncmp(n, "Key", 3)) return "Small Key";
    if (!strncmp(n, "Progressive", 11)) n += 11;
    size_t k = 0;
    for (const char *c = n; *c && k + 2 < sizeof buf; c++) {
        if (c != n && *c >= 'A' && *c <= 'Z') buf[k++] = ' ';
        buf[k++] = *c;
    }
    buf[k] = 0;
    return buf;
}

/* The ten dungeon bosses: their location, the room the apps read (zboss:<room>), name and dungeon. */
static const struct { int loc, room; const char *name, *place; } BOSS[10] = {
    {Z3_LOC_Eastern_Palace_Boss, 0xC8, "the Armos Knights", "the Eastern Palace"}, {Z3_LOC_Desert_Palace_Boss, 0x33, "the Lanmolas", "the Desert Palace"},
    {Z3_LOC_Tower_of_Hera_Boss, 0x07, "Moldorm", "the Tower of Hera"}, {Z3_LOC_Palace_of_Darkness_Boss, 0x5A, "Helmasaur King", "the Palace of Darkness"},
    {Z3_LOC_Swamp_Palace_Boss, 0x06, "Arrghus", "the Swamp Palace"}, {Z3_LOC_Skull_Woods_Boss, 0x29, "Mothula", "Skull Woods"},
    {Z3_LOC_Thieves_Town_Boss, 0xAC, "Blind", "Thieves' Town"}, {Z3_LOC_Ice_Palace_Boss, 0xDE, "Kholdstare", "the Ice Palace"},
    {Z3_LOC_Misery_Mire_Boss, 0x90, "Vitreous", "Misery Mire"}, {Z3_LOC_Turtle_Rock_Boss, 0xA4, "Trinexx", "Turtle Rock"}};
/* Dungeons to reach: their region and the dungeon number the apps read ($7E040C, zdungeon:<hex>). */
static const struct { int region, id; const char *name; } REACH[6] = {
    {Z3_REG_Palace_of_Darkness, 0x0C, "the Palace of Darkness"}, {Z3_REG_Swamp_Palace, 0x0A, "the Swamp Palace"},
    {Z3_REG_Ice_Palace, 0x12, "the Ice Palace"}, {Z3_REG_Misery_Mire, 0x0E, "Misery Mire"}, {Z3_REG_Turtle_Rock, 0x18, "Turtle Rock"},
    {Z3_REG_Ganons_Tower, 0x1A, "Ganon's Tower"}};

static int is_dungeon(int reg) { return reg == Z3_REG_Eastern_Palace || reg == Z3_REG_Desert_Palace || reg == Z3_REG_Tower_of_Hera || reg == Z3_REG_Hyrule_Castle_Tower || (reg >= Z3_REG_Palace_of_Darkness && reg <= Z3_REG_Ganons_Tower); }
static double check_cost(int loc) { return is_dungeon(Z3_LOCS[loc].region) ? 1.5 : 0.8; }   /* as smz3daily.c's Zelda checks */
static int is_prize(int loc) { return !strncmp(Z3_LOCS[loc].kind, "Prize", 5); }
/* A check of the tracker: an item location the apps count (not a prize, medallion slot or fountain). */
static int is_check(int loc) { return z3_loc_exists(W, loc) && W->item_at[loc] != Z3_ITEM_NONE && smz_z3_tracker_index(loc) >= 0; }
static int is_crystal(int item) { return item >= Z3_ITEM_Crystal1 && item <= Z3_ITEM_Crystal7; }

/* Every prize and event the items reach, as the logic counts them; how many crystals that makes. */
static int take_prizes(Z3Items *it, uint8_t *got)
{
    int crystals = 0;
    for (int more = 1; more;) {
        more = 0;
        for (int l = 0; l < Z3_LOC_COUNT; l++) {
            if (got[l] || !is_prize(l) || !z3_loc_exists(W, l) || W->item_at[l] == Z3_ITEM_NONE || !z3_loc_access(W, it, l)) continue;
            got[l] = 1; more = 1;
            z3_items_add(it, W->item_at[l], 1);
        }
    }
    for (int l = 0; l < Z3_LOC_COUNT; l++) if (got[l] && is_prize(l) && is_crystal(W->item_at[l])) crystals++;
    return crystals;
}

static int goal_met(const Z3Challenge *c, const Z3Items *it, int crystals)
{
    switch (c->goal.kind) {
    case G_ITEM: case G_BOSS: case G_AGA: return z3_loc_access(W, it, c->goal.target);
    case G_CRYSTALS: return crystals >= c->goal.count;
    case G_REACH: return z3_region_enter(W, it, c->goal.target);
    }
    return 0;
}

/* Collects every reachable check but the excluded ones, sphere by sphere, until the goal is met (1) or nothing opens (0). */
static int expand(const Z3Challenge *c, const uint8_t *excluded, int *order, int *n_order)
{
    Z3Items it = W->pre;
    uint8_t got[Z3_LOC_COUNT] = {0};
    int no = 0;
    for (int s = 0; s < 80; s++) {
        int crystals = take_prizes(&it, got);
        if (goal_met(c, &it, crystals)) { if (n_order) *n_order = no; return 1; }
        int open[Z3_LOC_COUNT], k = 0;
        for (int l = 0; l < Z3_LOC_COUNT; l++)
            if (!got[l] && !(excluded && excluded[l]) && is_check(l) && z3_loc_access(W, &it, l)) open[k++] = l;
        if (!k) break;
        for (int i = 0; i < k; i++) { got[open[i]] = 1; z3_items_add(&it, W->item_at[open[i]], 1); if (order) order[no] = open[i]; no++; }
    }
    if (n_order) *n_order = no;
    return 0;
}

/* The fewest items to the goal: everything collected on the way, then each one dropped, latest first, when the goal is
 * still met without it. -1 when the goal cannot be reached. */
static int route(Z3Challenge *c)
{
    static int order[Z3_LOC_COUNT];
    int n;
    if (!expand(c, NULL, order, &n)) return -1;
    uint8_t excluded[Z3_LOC_COUNT];
    memset(excluded, 1, sizeof excluded);
    for (int i = 0; i < n; i++) excluded[order[i]] = 0;
    for (int i = n - 1; i >= 0; i--) {
        excluded[order[i]] = 1;
        if (!expand(c, excluded, NULL, NULL)) excluded[order[i]] = 0;   /* needed */
    }
    if (!expand(c, excluded, NULL, NULL)) return -1;
    c->n_steps = 0;
    for (int i = 0; i < n; i++) if (!excluded[order[i]]) c->steps[c->n_steps++] = order[i];
    return 0;
}

static double goal_cost(const Z3Challenge *c)
{
    switch (c->goal.kind) {
    case G_BOSS: case G_AGA: return 8.0;   /* the fight and the rest of the dungeon, as smz3daily.c */
    case G_CRYSTALS: return 8.0 * c->goal.count;
    case G_REACH: return 2.0;
    default: return 0.0;
    }
}

/* As smz3daily.c: at each stage the pool of open unchecked checks is searched until the needed ones turn up (r needed out
 * of P take r(P+1)/(r+1) checks on average, at the pool's mean cost); the route time knows the way. */
static void estimate(Z3Challenge *c)
{
    uint8_t needed[Z3_LOC_COUNT] = {0}, checked[Z3_LOC_COUNT] = {0}, got[Z3_LOC_COUNT] = {0};
    for (int i = 0; i < c->n_steps; i++) needed[c->steps[i]] = 1;
    Z3Items it = W->pre;
    double search = 0, routem = 0;
    for (int s = 0; s < 80; s++) {
        take_prizes(&it, got);
        int pool = 0, r = 0, open[Z3_LOC_COUNT], k = 0;
        double cost = 0, ncost = 0;
        for (int l = 0; l < Z3_LOC_COUNT; l++) if (!checked[l] && is_check(l) && z3_loc_access(W, &it, l)) open[k++] = l;
        for (int i = 0; i < k; i++) { pool++; cost += check_cost(open[i]); if (needed[open[i]]) { r++; ncost += check_cost(open[i]); } }
        if (!r) break;
        search += (cost / pool) * r * (pool + 1.0) / (r + 1.0);
        routem += ncost + 2.5 * r;   /* knowing the way still means walking there */
        for (int i = 0; i < k; i++) { checked[open[i]] = 1; if (needed[open[i]]) z3_items_add(&it, W->item_at[open[i]], 1); }
    }
    c->est_search = search + goal_cost(c);
    c->est_route = routem + goal_cost(c);
}

static int pick_goal(Z3Challenge *c)
{
    Goal *g = &c->goal;
    int roll = rnd_n(100);
    g->count = 0;
    if (roll < 30) {
        int m = rnd_n((int)(sizeof MAJOR / sizeof MAJOR[0]));
        g->kind = G_ITEM; g->target = -1;
        for (int l = 0; l < Z3_LOC_COUNT && g->target < 0; l++) if (is_check(l) && W->item_at[l] == MAJOR[m].item) g->target = l;
        snprintf(g->text, sizeof g->text, "Get the %s", MAJOR[m].name);
    } else if (roll < 60) {
        int b = rnd_n(10);
        g->kind = G_BOSS; g->count = b; g->target = z3_loc_named(W, BOSS[b].loc);
        snprintf(g->text, sizeof g->text, "Defeat %s", BOSS[b].name);
    } else if (roll < 75) {
        g->kind = G_CRYSTALS; g->count = 1 + rnd_n(3); g->target = 0;
        if (g->count == 1) snprintf(g->text, sizeof g->text, "Get a crystal");
        else snprintf(g->text, sizeof g->text, "Collect %d crystals", g->count);
    } else if (roll < 90) {
        int a = rnd_n(6);
        g->kind = G_REACH; g->count = a; g->target = REACH[a].region;
        snprintf(g->text, sizeof g->text, "Reach %s", REACH[a].name);
    } else {
        g->kind = G_AGA; g->target = z3_loc_named(W, Z3_LOC_Agahnim);
        snprintf(g->text, sizeof g->text, "Defeat Agahnim");
    }
    return g->target < 0 ? -1 : 0;
}

static void add_hint(Z3Challenge *c, const char *fmt, const char *a, const char *b)
{
    if (c->n_hints >= 64) return;
    snprintf(c->hints[c->n_hints++].text, sizeof c->hints[0].text, fmt, a, b);
}

/* The world part for the overworld ("Light World"), the dungeon's own name inside one. */
static const char *region_name(int loc) { int r = Z3_LOCS[loc].region; return is_dungeon(r) ? Z3_REGIONS[r].key : Z3_REGIONS[r].name; }

static void make_hints(Z3Challenge *c)
{
    static const char *const ORD[] = {"", "first", "second", "third", "fourth"};
    c->n_hints = 0;
    for (int i = 0; i < c->n_steps; i++) {
        int loc = c->steps[i], type = W->item_at[loc], nth = 1, total = 0;
        char name[64], what[96];
        snprintf(name, sizeof name, "%s", item_name(type));
        for (int j = 0; j < c->n_steps; j++) if (!strcmp(item_name(W->item_at[c->steps[j]]), name)) { total++; if (j < i) nth++; }
        if (total > 1) snprintf(what, sizeof what, "%s %s", nth < 5 ? ORD[nth] : "next", name);
        else snprintf(what, sizeof what, "%s", name);
        add_hint(c, "You will need the %s. Look in %s.", what, region_name(loc));
        add_hint(c, "The %s is at: %s.", what, Z3_LOCS[loc].name);
    }
    if (c->goal.kind == G_ITEM) add_hint(c, "The goal, the %s, is in %s.", item_name(W->item_at[c->goal.target]), region_name(c->goal.target));
    else if (c->goal.kind == G_BOSS) add_hint(c, "%s waits in %s.", BOSS[c->goal.count].name, BOSS[c->goal.count].place);
    double last = (c->n_steps ? 1.0 : 0) + goal_cost(c);
    double step = c->n_hints > 1 ? (HINTS_DONE_BY - FIRST_HINT - last) / (c->n_hints - 1) : 0;
    if (step < 5) step = 5;
    if (step > 25) step = 25;
    for (int i = 0; i < c->n_hints; i++) c->hints[i].at = FIRST_HINT + i * step;
}

/* 0 accepted, 1 in the logic but out of range, -1 no use. */
static int try_candidate(Z3Challenge *c, int attempt)
{
    char key[96];
    snprintf(key, sizeof key, "%s#%d", c->window, attempt);
    rng_state = hash_str(key);
    z3_settings_default(&c->s);
    c->s.tournament = true;   /* the Original rules, as every online run */
    c->seed = 1 + rnd() % 999999999u;
    if (smz_z3_rebuild(&c->s, c->seed, &g_seed)) return -1;
    g_seed.w.col = NULL;   /* every sweep fresh */
    if (pick_goal(c)) return -1;
    if (route(c) || c->n_steps < 2) return -1;   /* unreachable, or open from the start */
    estimate(c);
    make_hints(c);
    if (c->n_hints && c->hints[c->n_hints - 1].at + c->est_route / c->n_steps > 180) return 1;
    return c->est_search >= MIN_EST && c->est_search <= MAX_EST ? 0 : 1;
}

/* The goal as the apps read it from memory (smz3_goal.h); loc:<n> counts over smz3_locations, whose Zelda checks are the
 * standalone game's too. */
static void goal_check(const Z3Challenge *c, char *out, size_t cap)
{
    const Goal *g = &c->goal;
    switch (g->kind) {
    case G_BOSS: snprintf(out, cap, "zboss:%x", BOSS[g->count].room); return;
    case G_CRYSTALS: snprintf(out, cap, "crystals:%d", g->count); return;
    case G_REACH: snprintf(out, cap, "zdungeon:%x", REACH[g->count].id); return;
    case G_AGA: snprintf(out, cap, "aga"); return;
    default: snprintf(out, cap, "loc:%d", smz_z3_tracker_index(g->target)); return;
    }
}

static void json_str(const char *s)
{
    putchar('"');
    for (; *s; s++) { if (*s == '"' || *s == '\\') putchar('\\'); putchar(*s); }
    putchar('"');
}

/* The settings in the apps' A Link to the Past run format (smz3_z3.c reads "seed" and the "z3" object; smz3check too). */
static void settings_json(const Z3Challenge *c, char *out, size_t cap)
{
    char check[32];
    goal_check(c, check, sizeof check);
    snprintf(out, cap, "{\"game\":\"z3\",\"seed\":\"%u\",\"z3\":{\"tournament\":\"true\"},"
             "\"saveStates\":false,\"defaultRules\":false,\"challenge\":true,\"goal\":\"%s\"}", (unsigned)c->seed, check);
}

static void print_challenge(const Z3Challenge *c, int attempt)
{
    static const char *const KIND[] = {"item", "boss", "crystals", "reach", "aga"};
    char js[512], check[32];
    settings_json(c, js, sizeof js);
    goal_check(c, check, sizeof check);
    printf("{\"window\":");
    json_str(c->window);
    printf(",\"game\":\"z3\",\"attempt\":%d,\"seed\":%u,\"settings\":", attempt, (unsigned)c->seed);
    json_str(js);
    printf(",\"start\":[],\"goal\":{\"kind\":\"%s\",\"target\":%d,\"count\":%d,\"check\":\"%s\",\"text\":", KIND[c->goal.kind], c->goal.target, c->goal.count, check);
    json_str(c->goal.text);
    printf("},\"estimate\":{\"searchMin\":%.0f,\"routeMin\":%.0f},\"steps\":[", c->est_search, c->est_route);
    for (int i = 0; i < c->n_steps; i++) {
        int loc = c->steps[i];
        printf("%s{\"item\":", i ? "," : "");
        json_str(item_name(W->item_at[loc]));
        printf(",\"location\":");
        json_str(Z3_LOCS[loc].name);
        printf(",\"region\":");
        json_str(region_name(loc));
        printf(",\"game\":\"z3\"}");
    }
    printf("],\"hints\":[");
    for (int i = 0; i < c->n_hints; i++) {
        printf("%s{\"atMin\":%.0f,\"text\":", i ? "," : "", c->hints[i].at);
        json_str(c->hints[i].text);
        printf("}");
    }
    printf("]}\n");
}

/* The planner of a window, or of n windows for tuning. A window always gets a challenge: when no candidate lands in range,
 * the one whose estimate came closest to it is rebuilt and kept. 0, or 1 when no candidate could be made at all. */
int z3_daily_main(int argc, char **argv)
{
    static Z3Challenge c;
    int stats = !strcmp(argv[1], "--z3-stats"), n = stats && argc >= 4 ? atoi(argv[3]) : 1;
    if (argc < 3) return 1;
    for (int w = 0; w < n; w++) {
        if (stats) snprintf(c.window, sizeof c.window, "%s-%d", argv[2], w);
        else snprintf(c.window, sizeof c.window, "%s", argv[2]);
        int a, ok = -1, best_a = -1;
        double best_off = 1e9;
        for (a = 0; a < MAX_TRIES && ok != 0; a++) {
            ok = try_candidate(&c, a);
            if (ok < 0) continue;
            double off = c.est_search < MIN_EST ? MIN_EST - c.est_search : c.est_search > MAX_EST ? c.est_search - MAX_EST : 0;
            if (ok == 1 && c.n_hints && c.hints[c.n_hints - 1].at + c.est_route / c.n_steps > 180) off += 1000;
            if (off < best_off) { best_off = off; best_a = a; }
        }
        if (ok != 0) {
            if (best_a < 0) { fprintf(stderr, "no candidate after %d tries\n", MAX_TRIES); if (!stats) return 1; continue; }
            try_candidate(&c, best_a);   /* the world is static: the best one is made again */
            a = best_a + 1;
        }
        if (stats) printf("%s tries=%d ok=%d kind=%d steps=%d search=%.0f route=%.0f hints=%d last=%.0f goal=\"%s\"\n", c.window, a, ok == 0, c.goal.kind,
                          c.n_steps, c.est_search, c.est_route, c.n_hints, c.n_hints ? c.hints[c.n_hints - 1].at : 0, c.goal.text);
        else print_challenge(&c, a - 1);
        fflush(stdout);
    }
    return 0;
}
