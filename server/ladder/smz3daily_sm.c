/* The Super Metroid daily challenge planner (smz3daily --sm): the same idea as smz3daily.c's for SMZ3, on the standalone
 * generator (core/smgen). A fresh file at the ship with no starting items (its generator has none), a Tournament + Split
 * seed with Race set, and one goal: defeat a boss, get an item or reach an area. The route is the fewest items that open
 * the goal; the estimate, the hints and the output follow smz3daily.c, so daily.js and the apps read both alike.
 *   smz3daily --sm <window-id>           the challenge of that window (ids sm-YYYY-MM-DD-n)
 *   smz3daily --sm-stats <prefix> <n>    one line per window, for tuning
 *   smz3daily --sm-draft <id>            a Draft race from the ship: a goal of 5 to 15 minutes, printed like smz3daily.c's
 *   smz3daily --sm-draft-stats <prefix> <n> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sm_fill.h"
#include "sm_world.h"
#include "smz3_state.h"

#define MIN_EST 30.0          /* accepted search estimate, minutes (as SMZ3's) */
#define MAX_EST 90.0
#define HINTS_DONE_BY 150.0
#define FIRST_HINT 10.0
#define MAX_TRIES 400
#define CHECK_MIN 1.6         /* a Super Metroid check, as smz3daily.c counts them */

enum { G_ITEM, G_BOSS, G_REACH, G_KILLS };

typedef struct { int kind, target, count; char text[160]; } Goal;   /* target: an SM_LOCS index (item, boss proxy) or a region */
typedef struct { double at; char text[200]; } Hint;
typedef struct {
    char window[64];
    SmGenSettings s;
    SmSeed seed;
    Goal goal;
    int steps[SM_LOC_COUNT], n_steps;   /* SM_LOCS indexes, in collection order */
    double est_search, est_route;
    Hint hints[64];
    int n_hints;
    int draft;
} SmChallenge;

static uint64_t rng_state;
static uint32_t rnd(void) { rng_state ^= rng_state << 13; rng_state ^= rng_state >> 7; rng_state ^= rng_state << 17; return (uint32_t)(rng_state >> 11); }
static int rnd_n(int n) { return (int)(rnd() % (uint32_t)n); }

static uint64_t hash_str(const char *s)
{
    uint64_t h = 1469598103934665603ull;
    for (; *s; s++) h = (h ^ (uint8_t)*s) * 1099511628211ull;
    return h | 1;
}

static const char *const ITEM_NAME[SM_ITEM_COUNT] = {
    "Energy Tank", "Missile", "Super Missile", "Power Bomb", "Bombs", "Charge Beam", "Ice Beam", "Hi-Jump Boots", "Speed Booster",
    "Wave Beam", "Spazer", "Spring Ball", "Varia Suit", "Gravity Suit", "X-Ray Scope", "Plasma Beam", "Grappling Beam",
    "Space Jump", "Screw Attack", "Morphing Ball", "Reserve Tank"};
static const char *item_name(int type) { return type >= 0 && type < SM_ITEM_COUNT ? ITEM_NAME[type] : "?"; }

/* The four bosses: the check right behind each one stands for the fight (the SM logic has no boss of its own), the
 * memory goal the apps read (smboss:<bit>) and where it waits. */
static const struct { const char *name; int loc_id; const char *place; } BOSS[4] = {
    {"Kraid", 48, "Kraid's Lair in Brinstar"},        /* Varia Suit */
    {"Phantoon", 132, "the Wrecked Ship"},            /* Energy Tank, Wrecked Ship: the ship needs its power back */
    {"Draygon", 154, "Maridia"},                      /* Space Jump */
    {"Ridley", 78, "Lower Norfair"}};                 /* Energy Tank, Ridley */
/* Areas the memory tells apart ($079F): the region whose entry opens it, its area number and name. */
static const struct { int region, area; const char *name; } REACH[3] = {
    {SMREG_WreckedShip, 3, "the Wrecked Ship"}, {SMREG_MaridiaOuter, 4, "Maridia"}, {SMREG_NorfairUpperWest, 2, "Norfair"}};

static const char *area_of(int idx)
{
    const char *a = SM_REGIONS[SM_LOCS[idx].region].area;
    return !strcmp(a, "Norfair Upper") ? "Norfair" : !strcmp(a, "Norfair Lower") ? "Lower Norfair" : a;
}

static void prog_add(SmProg *p, const SmChallenge *c, int idx) { int t = c->seed.item_at[idx].type; if (t < SM_ITEM_SLOTS) p->n[t]++; }

static int goal_met(const SmChallenge *c, const SmProg *p)
{
    switch (c->goal.kind) {
    case G_ITEM: case G_BOSS: return sm_loc_available(c->goal.target, c->s.logic, p);
    case G_REACH: return sm_region_can_enter(c->goal.target, c->s.logic, p);
    }
    return 0;
}

/* Collects every available check but the excluded ones, sphere by sphere, until the goal is met (1) or nothing opens (0). */
static int expand(const SmChallenge *c, const uint8_t *excluded, int *order, int *n_order)
{
    SmProg p;
    memset(&p, 0, sizeof p);
    uint8_t got[SM_LOC_COUNT] = {0};
    int no = 0;
    for (int s = 0; s < 80; s++) {
        if (goal_met(c, &p)) { if (n_order) *n_order = no; return 1; }
        int open[SM_LOC_COUNT], k = 0;
        for (int i = 0; i < SM_LOC_COUNT; i++) if (!got[i] && !(excluded && excluded[i]) && sm_loc_available(i, c->s.logic, &p)) open[k++] = i;
        if (!k) break;
        for (int i = 0; i < k; i++) { got[open[i]] = 1; prog_add(&p, c, open[i]); if (order) order[no] = open[i]; no++; }
    }
    if (n_order) *n_order = no;
    return 0;
}

/* The fewest items to the goal: everything collected on the way, then each one dropped, latest first, when the goal is
 * still met without it. -1 when the goal cannot be reached. */
static int route(SmChallenge *c)
{
    int order[SM_LOC_COUNT], n;
    if (!expand(c, NULL, order, &n)) return -1;
    uint8_t excluded[SM_LOC_COUNT];
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

static double goal_cost(const SmChallenge *c) { return c->goal.kind == G_BOSS ? 6.0 : c->goal.kind == G_REACH ? 2.0 : 0.0; }

/* As smz3daily.c: at each stage the pool of open unchecked checks is searched until the needed ones turn up (r needed out
 * of P take r(P+1)/(r+1) checks on average); the route time knows the way. One game, so no switches. */
static void estimate(SmChallenge *c)
{
    uint8_t needed[SM_LOC_COUNT] = {0}, checked[SM_LOC_COUNT] = {0};
    for (int i = 0; i < c->n_steps; i++) needed[c->steps[i]] = 1;
    if (c->draft && c->goal.kind == G_ITEM) needed[c->goal.target] = 1;   /* a Draft's goal item is searched for like the rest */
    SmProg p;
    memset(&p, 0, sizeof p);
    double search = 0, routem = 0;
    for (int s = 0; s < 80; s++) {
        int pool = 0, r = 0, open[SM_LOC_COUNT], k = 0;
        for (int i = 0; i < SM_LOC_COUNT; i++) if (!checked[i] && sm_loc_available(i, c->s.logic, &p)) open[k++] = i;
        for (int i = 0; i < k; i++) { pool++; r += needed[open[i]]; }
        if (!r) break;
        search += CHECK_MIN * r * (pool + 1.0) / (r + 1.0);
        routem += (CHECK_MIN + 2.5) * r;
        for (int i = 0; i < k; i++) { checked[open[i]] = 1; if (needed[open[i]]) prog_add(&p, c, open[i]); }
    }
    c->est_search = search + goal_cost(c);
    c->est_route = routem + goal_cost(c);
}

static int item_count(const SmChallenge *c, int type)
{
    int n = 0;
    for (int i = 0; i < SM_LOC_COUNT; i++) n += c->seed.item_at[i].type == type;
    return n;
}

static int pick_goal(SmChallenge *c)
{
    Goal *g = &c->goal;
    int roll = rnd_n(100);
    g->count = 0;
    if (roll < 35) {
        /* a major item there is one of, the X-Ray Scope aside (the logic never asks for it) */
        int cand[SM_LOC_COUNT], nc = 0;
        for (int i = 0; i < SM_LOC_COUNT; i++) {
            int t = c->seed.item_at[i].type;
            if (t != SM_ITEM_ETank && t != SM_ITEM_ReserveTank && t != SM_ITEM_Missile && t != SM_ITEM_Super && t != SM_ITEM_PowerBomb
                && t != SM_ITEM_XRay && item_count(c, t) == 1) cand[nc++] = i;
        }
        if (!nc) return -1;
        g->kind = G_ITEM; g->target = cand[rnd_n(nc)];
        snprintf(g->text, sizeof g->text, "Get the %s", item_name(c->seed.item_at[g->target].type));
    } else if (roll < 75) {
        int b = rnd_n(4);
        g->kind = G_BOSS; g->count = b; g->target = sm_loc_index(BOSS[b].loc_id);
        snprintf(g->text, sizeof g->text, "Defeat %s", BOSS[b].name);
    } else {
        int a = rnd_n(3);
        g->kind = G_REACH; g->count = a; g->target = REACH[a].region;
        snprintf(g->text, sizeof g->text, "Reach %s", REACH[a].name);
    }
    return g->target < 0 ? -1 : 0;
}

static void add_hint(SmChallenge *c, const char *fmt, const char *a, const char *b)
{
    if (c->n_hints >= 64) return;
    snprintf(c->hints[c->n_hints++].text, sizeof c->hints[0].text, fmt, a, b);
}

static void make_hints(SmChallenge *c)
{
    static const char *const ORD[] = {"", "first", "second", "third", "fourth"};
    c->n_hints = 0;
    for (int i = 0; i < c->n_steps; i++) {
        int idx = c->steps[i], type = c->seed.item_at[idx].type, nth = 1, total = 0;
        for (int j = 0; j < c->n_steps; j++) if (c->seed.item_at[c->steps[j]].type == type) { total++; if (j < i) nth++; }
        char what[96], where[160];
        if (total > 1) snprintf(what, sizeof what, "%s %s", nth < 5 ? ORD[nth] : "next", item_name(type));
        else snprintf(what, sizeof what, "%s", item_name(type));
        snprintf(where, sizeof where, "%s (%s)", SM_LOCS[idx].name, SM_REGIONS[SM_LOCS[idx].region].name);
        add_hint(c, "You will need the %s. Look in %s.", what, area_of(idx));
        add_hint(c, "The %s is at: %s.", what, where);
    }
    if (c->goal.kind == G_ITEM) add_hint(c, "The goal, the %s, is in %s.", item_name(c->seed.item_at[c->goal.target].type), SM_REGIONS[SM_LOCS[c->goal.target].region].name);
    else if (c->goal.kind == G_BOSS) add_hint(c, "%s waits in %s.", BOSS[c->goal.count].name, BOSS[c->goal.count].place);
    double last = (c->n_steps ? CHECK_MIN : 0) + goal_cost(c);
    double step = c->n_hints > 1 ? (HINTS_DONE_BY - FIRST_HINT - last) / (c->n_hints - 1) : 0;
    if (step < 5) step = 5;
    if (step > 25) step = 25;
    for (int i = 0; i < c->n_hints; i++) c->hints[i].at = FIRST_HINT + i * step;
}

/* 0 accepted, 1 in the logic but out of range, -1 no use. */
static int try_candidate(SmChallenge *c, int attempt)
{
    char key[96];
    snprintf(key, sizeof key, "%s#%d", c->window, attempt);
    rng_state = hash_str(key);
    sm_settings_defaults(&c->s);
    c->s.logic = SM_LOGIC_Tournament; c->s.placement = SM_PLACEMENT_Split; c->s.race = true;   /* the Original rules, as online runs */
    c->s.seed = (int32_t)(rnd() & 0x7FFFFFFF);
    if (sm_fill(&c->seed, &c->s) || !sm_playthrough_ok(&c->seed)) return -1;
    if (pick_goal(c)) return -1;
    if (route(c) || c->n_steps < 2) return -1;   /* unreachable, or open from the start */
    estimate(c);
    make_hints(c);
    if (c->n_hints && c->hints[c->n_hints - 1].at + c->est_route / c->n_steps > 180) return 1;
    return c->est_search >= MIN_EST && c->est_search <= MAX_EST ? 0 : 1;
}

/* The goal as the apps read it from memory (smz3_goal.h): loc:<n> counts over smz3_locations, whose Super Metroid checks are
 * the standalone game's too (the item bit of location id n is $7ED870 + n / 8). */
static void goal_check(const SmChallenge *c, char *out, size_t cap)
{
    const Goal *g = &c->goal;
    if (g->kind == G_BOSS) { snprintf(out, cap, "smboss:%d", g->count); return; }
    if (g->kind == G_REACH) { snprintf(out, cap, "smarea:%d", REACH[g->count].area); return; }
    if (g->kind == G_KILLS) { snprintf(out, cap, "kills:%d", g->count); return; }
    int id = SM_LOCS[g->target].id, idx = -1;
    for (unsigned j = 0; j < smz3_location_count && idx < 0; j++) {
        const smz3_location *l = &smz3_locations[j];
        if (l->game != SMZ3_GAME_ZELDA && l->addr == (unsigned)(id / 8) && l->flag == 1u << (id % 8)) idx = (int)j;
    }
    snprintf(out, cap, "loc:%d", idx);
}

static void json_str(const char *s)
{
    putchar('"');
    for (; *s; s++) { if (*s == '"' || *s == '\\') putchar('\\'); putchar(*s); }
    putchar('"');
}

/* The settings in the apps' Super Metroid run format (smz3_sm.c reads "seed" and the "sm" object; smz3check too). */
static void settings_json(const SmChallenge *c, char *out, size_t cap)
{
    char check[32];
    goal_check(c, check, sizeof check);
    snprintf(out, cap, "{\"game\":\"sm\",\"seed\":\"%d\",\"sm\":{\"logic\":\"tournament\",\"placement\":\"split\",\"race\":\"true\"},"
             "\"saveStates\":false,\"defaultRules\":false,\"challenge\":true,\"goal\":\"%s\"}", (int)c->s.seed, check);
}

static void print_challenge(const SmChallenge *c, int attempt)
{
    static const char *const KIND[] = {"item", "boss", "reach", "kills"};
    char js[512], check[32];
    settings_json(c, js, sizeof js);
    goal_check(c, check, sizeof check);
    printf("{\"window\":");
    json_str(c->window);
    printf(",\"game\":\"sm\",\"attempt\":%d,\"seed\":%d,\"settings\":", attempt, (int)c->s.seed);
    json_str(js);
    printf(",\"start\":[],\"goal\":{\"kind\":\"%s\",\"target\":%d,\"count\":%d,\"check\":\"%s\",\"text\":", KIND[c->goal.kind], c->goal.target, c->goal.count, check);
    json_str(c->goal.text);
    printf("},\"estimate\":{\"searchMin\":%.0f,\"routeMin\":%.0f},\"steps\":[", c->est_search, c->est_route);
    for (int i = 0; i < c->n_steps; i++) {
        int idx = c->steps[i];
        printf("%s{\"item\":", i ? "," : "");
        json_str(item_name(c->seed.item_at[idx].type));
        printf(",\"location\":");
        json_str(SM_LOCS[idx].name);
        printf(",\"region\":");
        json_str(SM_REGIONS[SM_LOCS[idx].region].name);
        printf(",\"game\":\"sm\"}");
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
 * the one whose estimate came closest to it is kept. 0, or 1 when no candidate could be made at all. */
int sm_daily_main(int argc, char **argv)
{
    static SmChallenge c, best;
    int stats = !strcmp(argv[1], "--sm-stats"), n = stats && argc >= 4 ? atoi(argv[3]) : 1;
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
            if (off < best_off) { best_off = off; best = c; best_a = a; }
        }
        if (ok != 0) {
            if (best_a < 0) { fprintf(stderr, "no candidate after %d tries\n", MAX_TRIES); if (!stats) return 1; continue; }
            c = best; a = best_a + 1;
        }
        if (stats) printf("%s tries=%d ok=%d kind=%d steps=%d search=%.0f route=%.0f hints=%d last=%.0f goal=\"%s\"\n", c.window, a, ok == 0, c.goal.kind,
                          c.n_steps, c.est_search, c.est_route, c.n_hints, c.n_hints ? c.hints[c.n_hints - 1].at : 0, c.goal.text);
        else print_challenge(&c, a - 1);
        fflush(stdout);
    }
    return 0;
}

/* ---------- Draft races ---------- */

#define DRAFT_MIN 5.0
#define DRAFT_MAX 15.0

/* kind: 0 kills, 1 an item, 2 a boss, 3 an area. */
static int pick_draft_goal(SmChallenge *c, int kind)
{
    Goal *g = &c->goal;
    if (kind == 0) {
        g->kind = G_KILLS; g->count = 15 + rnd_n(5) * 5; g->target = 0;
        snprintf(g->text, sizeof g->text, "Defeat %d enemies", g->count);
        return 0;
    }
    int want = kind == 1 ? G_ITEM : kind == 2 ? G_BOSS : G_REACH;
    for (;;) { int r = pick_goal(c); if (r || g->kind == want) return r; }   /* the daily's goals, until one of that kind */
}

/* 0 when the candidate is accepted; kind as pick_draft_goal. */
static int try_draft(SmChallenge *c, int attempt, int kind)
{
    char key[96];
    snprintf(key, sizeof key, "smdraft:%s#%d", c->window, attempt);
    rng_state = hash_str(key);
    sm_settings_defaults(&c->s);
    c->s.logic = SM_LOGIC_Tournament; c->s.placement = SM_PLACEMENT_Split; c->s.race = true;
    c->s.seed = (int32_t)(rnd() & 0x7FFFFFFF);
    c->draft = 1;
    c->n_steps = c->n_hints = 0;
    if (sm_fill(&c->seed, &c->s) || !sm_playthrough_ok(&c->seed)) return -1;
    if (pick_draft_goal(c, kind)) return -1;
    if (c->goal.kind == G_KILLS) { c->est_search = c->est_route = 0.4 * c->goal.count + 1.0; return 0; }
    if (route(c) || c->n_steps > 3) return 1;
    estimate(c);
    return c->est_search >= DRAFT_MIN && c->est_search <= DRAFT_MAX ? 0 : 1;
}

static void print_draft(const SmChallenge *c, int attempt)
{
    char js[512], check[32];
    settings_json(c, js, sizeof js);
    goal_check(c, check, sizeof check);
    printf("{\"draft\":");
    json_str(c->window);
    printf(",\"game\":\"sm\",\"attempt\":%d,\"seed\":%d,\"settings\":", attempt, (int)c->s.seed);
    json_str(js);
    printf(",\"spot\":\"the Landing Site\",\"start\":[],\"goal\":{\"check\":\"%s\",\"text\":", check);
    json_str(c->goal.text);
    printf("},\"estimateMin\":%.0f}\n", c->est_search);
}

/* A Draft, or n of them for tuning. The kind of goal comes from the id; when no seed fits it, the Draft counts enemies. */
int sm_draft_main(int argc, char **argv)
{
    static SmChallenge c;
    int stats = !strcmp(argv[1], "--sm-draft-stats"), n = stats && argc >= 4 ? atoi(argv[3]) : 1;
    if (argc < 3) return 1;
    for (int w = 0; w < n; w++) {
        if (stats) snprintf(c.window, sizeof c.window, "%s-%d", argv[2], w);
        else snprintf(c.window, sizeof c.window, "%s", argv[2]);
        char kk[96];
        snprintf(kk, sizeof kk, "smdraft:%s#kind", c.window);
        int roll = (int)((hash_str(kk) >> 20) % 100), kind = roll < 35 ? 0 : roll < 70 ? 1 : roll < 85 ? 2 : 3;
        int a, ok = -1;
        for (a = 0; a < MAX_TRIES && ok != 0; a++) ok = try_draft(&c, a, kind);
        if (ok != 0) { a = 0; try_draft(&c, a++, 0); }
        if (stats) printf("%s tries=%d steps=%d est=%.0f goal=\"%s\"\n", c.window, a, c.n_steps, c.est_search, c.goal.text);
        else print_draft(&c, a - 1);
        fflush(stdout);
    }
    return 0;
}
