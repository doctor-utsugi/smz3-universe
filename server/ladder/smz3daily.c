/* Daily timed challenge generator: for a window id, tries seeds with random starting items and a random goal, finds the
 * shortest item route to the goal with the generator's logic, turns it into timed hints and estimates how long it takes.
 * Prints the first candidate whose estimate is in range as one JSON object (see docs/DAILY.md).
 *   smz3daily <window-id>             the challenge for that window
 *   smz3daily --stats <prefix> <n>    one line per window <prefix>-0 .. <prefix>-(n-1), for tuning the estimate */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smz3_generate.h"
#include "smz3_world_gen.h"
#include "smz3_itemopts.h"
#include "smz3_itemopts_data.h"
#include "smz3_itemnames.h"
#include "smz3_texts_data.h"
#define SMZ_LOC_NAMES HINT_LOC_NAMES   /* the hint texts name the locations too, under the same symbol */
#include "smz3_hints_data.h"
#undef SMZ_LOC_NAMES
#include "smz3_state.h"
#include "smz3_runlogic.h"

#define MIN_EST 30.0          /* accepted search estimate, minutes */
#define MAX_EST 90.0
#define HINTS_DONE_BY 150.0   /* with every hint read the goal should fall by then */
#define FIRST_HINT 10.0
#define MAX_TRIES 400

enum { G_ITEM, G_BOSS, G_CRYSTALS, G_SMBOSSES, G_REACH };

typedef struct { int kind, target, count; char text[160]; } Goal;
typedef struct { int loc, item, game; } Step;
typedef struct { double at; char text[200]; } Hint;

typedef struct {
    char window[64];
    SmzSettings s;
    SmzSeed seed;
    Goal goal;
    Step steps[64];
    int n_steps;
    double est_search, est_route;
    Hint hints[160];
    int n_hints;
    int start_types[SMZ_MAX_START_ITEMS], n_start_types;
    int dead;   /* a mid-game start: the Super Metroid bosses already dead (bit 0 Kraid, 1 Phantoon, 2 Draygon, 3 Ridley) */
} Challenge;

static uint64_t rng_state;
static uint32_t rnd(void) { rng_state ^= rng_state << 13; rng_state ^= rng_state >> 7; rng_state ^= rng_state << 17; return (uint32_t)(rng_state >> 11); }
static int rnd_n(int n) { return (int)(rnd() % (uint32_t)n); }

/* ---------- names ---------- */

static void camel(const char *in, char *out, size_t cap)
{
    size_t n = 0;
    for (const char *c = in; *c && n + 2 < cap; c++) {
        if (c != in && ((*c >= 'A' && *c <= 'Z' && !(c[-1] >= 'A' && c[-1] <= 'Z')) || (*c >= '0' && *c <= '9' && !(c[-1] >= '0' && c[-1] <= '9')))) out[n++] = ' ';
        out[n++] = *c;
    }
    out[n] = 0;
}

static const char *item_name(int type)
{
    if (type > 0 && type < 256 && SMZ_ITEM_TEXT[type].name.n) {
        const char *n = SMZ_ITEM_TEXT[type].name.v[0];
        return !strcmp(n, "Boh") ? "Bow" : n;   /* the game's hint texts joke about the bow's name */
    }
    static char buf[64];
    for (unsigned i = 0; i < sizeof SMZ_ITEM_NAMES / sizeof SMZ_ITEM_NAMES[0]; i++)
        if (SMZ_ITEM_NAMES[i].id == type) { camel(SMZ_ITEM_NAMES[i].name, buf, sizeof buf); return buf; }
    return "?";
}

/* The location as the in-game hints call it ("Blind's Hideout - Far Left"), falling back on the tracker's name. */
static const char *loc_name(int loc)
{
    if (loc >= 0 && loc < 512 && HINT_LOC_NAMES[loc].n) return HINT_LOC_NAMES[loc].v[0];
    const smz3_location *li = NULL;
    for (int q = 0; q < SMZ_LOC_NAME_COUNT && !li; q++)
        if (SMZ_LOC_NAMES[q].id == loc)
            for (unsigned j = 0; j < smz3_location_count; j++)
                if (!strcmp(SMZ_LOC_NAMES[q].name, smz3_locations[j].id)) { li = &smz3_locations[j]; break; }
    return li ? li->name : "?";
}

static int loc_region(int loc)
{
    for (int i = 0; i < SMZ_LOC_COUNT; i++) if (SMZ_LOCS[i].id == loc) return SMZ_LOCS[i].region;
    return -1;
}

static const char *region_name(int r)
{
    return r >= 0 && r < REG_COUNT ? SMZ_REGION_TEXT[r].name : "?";
}

/* The coarse place used by the first hint of a step: a dungeon, Light or Dark World, or a Super Metroid area. */
static const char *area_name(int r)
{
    const char *n = SMZ_REGIONS[r].name;
    if (SMZ_REGIONS[r].kind == RK_Z3) {
        if (!SMZ_REGIONS[r].overworld && r != REG_HyruleCastle) return region_name(r);
        if (r == REG_HyruleCastle) return "Hyrule Castle";
        return !strncmp(n, "Dark", 4) ? "the Dark World" : "the Light World";
    }
    if (strstr(n, "Crateria")) return "Crateria";
    if (strstr(n, "Brinstar") || r == REG_KraidsLair) return "Brinstar";
    if (strstr(n, "LowerNorfair")) return "Lower Norfair";
    if (strstr(n, "Norfair")) return "Norfair";
    if (strstr(n, "Maridia")) return "Maridia";
    return "the Wrecked Ship";
}

static const char *boss_name(int b)
{
    static const char *const N[] = {"", "Kraid", "Phantoon", "Draygon", "Ridley", "Mother Brain", "", "the Armos Knights", "the Lanmolas",
                                    "Moldorm", "Helmasaur King", "Arrghus", "Blind", "Mothula", "Kholdstare", "Vitreous", "Trinexx", "Agahnim", "Ganon"};
    return b > 0 && b < BOSS_COUNT_ ? N[b] : "?";
}

static int boss_region(const SmzWorld *w, int boss)
{
    for (int g = 0; g < REG_COUNT; g++) if (w->boss_of[g] == boss) return g;
    return -1;
}

/* ---------- logic ---------- */

static int is_progression_loc(const SmzSeed *sd, int loc)
{
    int t = sd->world.item_at[loc];
    if (t <= 0) return 0;
    if (SMZ_ITEM_CATS[t] & (ICAT_SmallKey | ICAT_BigKey | ICAT_Map | ICAT_Compass | ICAT_Keycard)) return 0;   /* keysanity is off: assumed */
    return sd->item_prog[loc] != 0;
}

/* Junk the logic never asks for: rupees, arrows, bombs refills and the like (energy and hearts may count). */
static int counts_in_logic(int t)
{
    return !(SMZ_ITEM_CATS[t] & (ICAT_Junk | ICAT_Scam | ICAT_NeverProgression)) || (SMZ_ITEM_CATS[t] & ICAT_PossibleProgression);
}

static int goal_met(const Challenge *c, const SmzProg *p, const uint8_t *reach)
{
    const SmzWorld *w = &c->seed.world;
    switch (c->goal.kind) {
    case G_ITEM: return reach[c->goal.target];
    case G_BOSS: { int g = boss_region(w, c->goal.target); return g >= 0 && region_can_beat_boss(w, g, p, false); }
    case G_CRYSTALS: return p->rewards[REWARD_CrystalBlue] + p->rewards[REWARD_CrystalRed] >= c->goal.count;
    case G_SMBOSSES: return p->bosses[BOSS_Kraid] + p->bosses[BOSS_Phantoon] + p->bosses[BOSS_Draygon] + p->bosses[BOSS_Ridley] >= c->goal.count;
    case G_REACH: return region_can_enter(w, c->goal.target, p, false);
    }
    return 0;
}

/* Collects every reachable location except the excluded ones until nothing new opens; 1 when the goal is met on the way.
 * order/sphere (optional) receive the collection order. */
static int expand(const Challenge *c, const uint8_t *excluded, int *order, int *sphere, int *n_order)
{
    int items[1200], ni = smz_assumed_items(&c->seed, items, 600), ids[512];
    uint8_t got[512] = {0}, reach[512];
    int no = 0;
    for (int s = 0; s < 80; s++) {
        SmzProg p;
        int k = smz_reachable_prog(&c->seed, items, ni, ids, 512, &p);
        memset(reach, 0, sizeof reach);
        for (int i = 0; i < k; i++) reach[ids[i]] = 1;
        if (goal_met(c, &p, reach)) { if (n_order) *n_order = no; return 1; }
        int added = 0;
        for (int i = 0; i < k; i++) {
            int l = ids[i];
            if (got[l] || (excluded && excluded[l])) continue;
            got[l] = 1;
            int t = c->seed.world.item_at[l];
            if (t > 0 && ni < 1200) items[ni++] = t;
            if (order) { order[no] = l; sphere[no] = s; }
            no++;
            added++;
        }
        if (!added) break;
    }
    if (n_order) *n_order = no;
    return 0;
}

/* The fewest items to reach the goal: start from everything collected up to the goal and drop, latest first, each
 * progression item the goal does not need. Fills c->steps in collection order; -1 when the goal cannot be reached. */
static int route(Challenge *c)
{
    static int order[512], sphere[512];
    int n;
    if (!expand(c, NULL, order, sphere, &n)) return -1;
    uint8_t excluded[512];
    memset(excluded, 1, sizeof excluded);   /* only what was collected on the way may stay */
    for (int i = 0; i < n; i++) excluded[order[i]] = 0;
    /* items the logic never counts go first in one pass, then the rest one by one (energy tanks and the like do count) */
    for (int i = 0; i < n; i++) if (!c->seed.world.item_at[order[i]] || !(c->seed.item_prog[order[i]] || counts_in_logic(c->seed.world.item_at[order[i]]))) excluded[order[i]] = 1;
    if (!expand(c, excluded, NULL, NULL, NULL)) for (int i = 0; i < n; i++) excluded[order[i]] = 0;
    for (int i = n - 1; i >= 0; i--) {
        int l = order[i];
        if (excluded[l]) continue;
        excluded[l] = 1;
        if (!expand(c, excluded, NULL, NULL, NULL)) excluded[l] = 0;   /* needed */
    }
    if (!expand(c, excluded, NULL, NULL, NULL)) return -1;
    c->n_steps = 0;
    for (int i = 0; i < n && c->n_steps < 64; i++) {
        int l = order[i];
        int t = c->seed.world.item_at[l];
        if (excluded[l] || (SMZ_ITEM_CATS[t] & (ICAT_SmallKey | ICAT_BigKey | ICAT_Map | ICAT_Compass | ICAT_Keycard))) continue;
        int r = loc_region(l);
        c->steps[c->n_steps++] = (Step){l, c->seed.world.item_at[l], r >= 0 ? SMZ_REGIONS[r].kind : RK_Z3};
    }
    return 0;
}

/* ---------- estimate ---------- */

static double check_cost(int loc)
{
    int r = loc_region(loc);
    if (r < 0) return 1.0;
    if (SMZ_REGIONS[r].kind == RK_SM) return 1.6;
    return SMZ_REGIONS[r].overworld || r == REG_HyruleCastle ? 0.8 : 1.5;
}

static double goal_cost(const Challenge *c)
{
    switch (c->goal.kind) {
    case G_BOSS: return c->goal.target <= BOSS_Ridley ? 6.0 : 8.0;   /* the fight plus, for Zelda, the rest of the dungeon */
    case G_CRYSTALS: return 8.0 * c->goal.count;
    case G_SMBOSSES: return 6.0 * c->goal.count;
    case G_REACH: return 2.0;
    default: return 0.0;
    }
}

/* Search time for a player who does not know the seed: with only the route's items, at each stage the pool of reachable
 * unchecked locations is explored until the needed ones turn up (r needed out of P take r(P+1)/(r+1) checks on average).
 * Route time: the needed checks only, as with every hint read. Both add the goal and a cost per change of game. */
static void estimate(Challenge *c)
{
    uint8_t needed[512] = {0}, checked[512] = {0};
    for (int i = 0; i < c->n_steps; i++) needed[c->steps[i].loc] = 1;
    int items[1200], ni = smz_assumed_items(&c->seed, items, 600), ids[512];
    double search = 0, routem = 0;
    for (int s = 0; s < 80; s++) {
        int k = smz_reachable(&c->seed, items, ni, ids, 512), pool = 0, r = 0;
        double cost = 0, ncost = 0;
        for (int i = 0; i < k; i++) {
            if (checked[ids[i]]) continue;
            pool++;
            cost += check_cost(ids[i]);
            if (needed[ids[i]]) { r++; ncost += check_cost(ids[i]); }
        }
        if (!r) break;
        search += (cost / pool) * r * (pool + 1.0) / (r + 1.0);
        routem += ncost + 2.5 * r;   /* knowing the way still means walking there */
        for (int i = 0; i < k; i++)
            if (!checked[ids[i]]) {
                checked[ids[i]] = 1;
                if (needed[ids[i]] && ni < 1200) items[ni++] = c->seed.world.item_at[ids[i]];
            }
    }
    int switches = 0, last = RK_SM;   /* a new SMZ3 file starts at Samus's ship */
    for (int i = 0; i < c->n_steps; i++) { if (c->steps[i].game != last) switches++; last = c->steps[i].game; }
    double extra = goal_cost(c) + 2.5 * switches;
    c->est_search = search + extra;
    c->est_route = routem + extra;
}

/* ---------- goal and hints ---------- */

static int item_count(const SmzSeed *sd, int type)
{
    int n = 0;
    for (int i = 0; i < SMZ_LOC_COUNT; i++) n += sd->world.item_at[SMZ_LOCS[i].id] == type;
    return n;
}

static int pick_goal(Challenge *c)
{
    const SmzWorld *w = &c->seed.world;
    Goal *g = &c->goal;
    int roll = rnd_n(100);
    g->count = 0;
    if (roll < 30) {
        int cand[512], nc = 0;
        for (int i = 0; i < SMZ_LOC_COUNT; i++) {
            int l = SMZ_LOCS[i].id;
            if (is_progression_loc(&c->seed, l) && item_count(&c->seed, w->item_at[l]) == 1 && !(SMZ_ITEM_CATS[w->item_at[l]] & ICAT_Bottle)) cand[nc++] = l;
        }
        if (!nc) return -1;
        g->kind = G_ITEM; g->target = cand[rnd_n(nc)];
        snprintf(g->text, sizeof g->text, "Get the %s", item_name(w->item_at[g->target]));
    } else if (roll < 65) {
        static const int B[] = {BOSS_Kraid, BOSS_Phantoon, BOSS_Draygon, BOSS_Ridley, BOSS_ArmosKnights, BOSS_Lanmolas, BOSS_Moldorm, BOSS_HelmasaurKing,
                                BOSS_Arrghus, BOSS_Mothula, BOSS_Blind, BOSS_Kholdstare, BOSS_Vitreous, BOSS_Trinexx, BOSS_Agahnim};
        g->kind = G_BOSS; g->target = B[rnd_n(sizeof B / sizeof B[0])];
        snprintf(g->text, sizeof g->text, "Defeat %s", boss_name(g->target));
    } else if (roll < 75) {
        g->kind = G_CRYSTALS; g->count = 2 + rnd_n(2);
        snprintf(g->text, sizeof g->text, "Collect %d crystals", g->count);
    } else if (roll < 85) {
        g->kind = G_SMBOSSES; g->count = 2;
        snprintf(g->text, sizeof g->text, "Defeat %s Super Metroid bosses", g->count == 2 ? "two" : g->count == 3 ? "three" : "four");
    } else {
        /* only places the game memory tells apart: a dungeon ($040C) or a Super Metroid area ($079F) */
        static const int R[] = {REG_GanonsTower, REG_TurtleRock, REG_MiseryMire, REG_IcePalace, REG_WreckedShip, REG_OuterMaridia, REG_UpperNorfairWest};
        g->kind = G_REACH; g->target = R[rnd_n(sizeof R / sizeof R[0])];
        snprintf(g->text, sizeof g->text, "Reach %s", region_name(g->target));
    }
    return 0;
}

static void add_hint(Challenge *c, const char *fmt, const char *a, const char *b)
{
    if (c->n_hints >= 160) return;
    snprintf(c->hints[c->n_hints++].text, sizeof c->hints[0].text, fmt, a, b);
}

/* Two hints per step, vague then exact, and a last one for the goal; spread so that with all of them read the goal falls
 * by HINTS_DONE_BY (one every 5 to 25 minutes, the first at FIRST_HINT). */
static void make_hints(Challenge *c)
{
    c->n_hints = 0;
    for (int i = 0; i < c->n_steps; i++) {
        const Step *s = &c->steps[i];
        int r = loc_region(s->loc);
        char where[160];
        snprintf(where, sizeof where, "%s (%s)", loc_name(s->loc), region_name(r));
        int nth = 1, total = 0;
        for (int j = 0; j < c->n_steps; j++) if (c->steps[j].item == s->item) { total++; if (j < i) nth++; }
        char what[96];
        static const char *const ORD[] = {"", "first", "second", "third", "fourth"};
        if (total > 1) snprintf(what, sizeof what, "%s %s", nth < 5 ? ORD[nth] : "next", item_name(s->item));
        else snprintf(what, sizeof what, "%s", item_name(s->item));
        add_hint(c, "You will need the %s. Look in %s.", what, r >= 0 ? area_name(r) : "?");
        add_hint(c, "The %s is at: %s.", what, where);
    }
    const SmzWorld *w = &c->seed.world;
    if (c->goal.kind == G_ITEM) {
        add_hint(c, "The goal, the %s, is in %s.", item_name(w->item_at[c->goal.target]), region_name(loc_region(c->goal.target)));
    } else if (c->goal.kind == G_BOSS) {
        int g = boss_region(w, c->goal.target);
        add_hint(c, "%s waits in %s.", boss_name(c->goal.target), g >= 0 ? region_name(g) : "?");
    } else if (c->goal.kind == G_CRYSTALS || c->goal.kind == G_SMBOSSES) {
        /* only the places the route opens */
        int items[1200], ni = smz_assumed_items(&c->seed, items, 600), ids[512];
        for (int i = 0; i < c->n_steps && ni < 1200; i++) items[ni++] = c->steps[i].item;
        SmzProg p;
        smz_reachable_prog(&c->seed, items, ni, ids, 512, &p);
        char list[200] = "";
        size_t n = 0;
        for (int g = 0; g < REG_COUNT; g++) {
            int hit = c->goal.kind == G_CRYSTALS ? (w->reward_of[g] == REWARD_CrystalBlue || w->reward_of[g] == REWARD_CrystalRed) && region_can_retrieve_reward(w, g, &p, false)
                                                 : (w->boss_of[g] >= BOSS_Kraid && w->boss_of[g] <= BOSS_Ridley) && p.bosses[w->boss_of[g]];
            if (hit && n < sizeof list - 48) n += (size_t)snprintf(list + n, sizeof list - n, "%s%s", n ? "; " : "", region_name(g));
        }
        add_hint(c, c->goal.kind == G_CRYSTALS ? "Crystals you can get: %s.%s" : "Bosses you can beat: %s.%s", list, "");
    }
    double last = c->n_steps ? check_cost(c->steps[c->n_steps - 1].loc) + goal_cost(c) : goal_cost(c);
    double step = c->n_hints > 1 ? (HINTS_DONE_BY - FIRST_HINT - last) / (c->n_hints - 1) : 0;
    if (step < 5) step = 5;
    if (step > 25) step = 25;
    for (int i = 0; i < c->n_hints; i++) c->hints[i].at = FIRST_HINT + i * step;
}

/* ---------- candidates ---------- */

static void pick_start(Challenge *c)
{
    smz_settings_defaults(&c->s);
    int groups = 2 + rnd_n(5);
    for (int k = 0; k < groups; k++) {
        int g = rnd_n(SMZ_ITEMOPT_GROUPS);
        int starts[8], ns = 0;
        for (int o = 0; o < SMZ_ITEMOPT_GROUP[g].count && ns < 8; o++)
            if (SMZ_ITEMOPT[SMZ_ITEMOPT_GROUP[g].first + o].has_mem) starts[ns++] = o;
        if (ns) c->s.item_opt[g] = (uint8_t)starts[rnd_n(ns)];
    }
    c->s.seed = (int32_t)(rnd() & 0x7FFFFFFF);
}

static uint64_t hash_str(const char *s)
{
    uint64_t h = 1469598103934665603ull;
    for (; *s; s++) h = (h ^ (uint8_t)*s) * 1099511628211ull;
    return h | 1;
}

/* The boss's bit in the dead mask (the order of smz_boss_mask). */
static int sm_boss_bit(int boss) { return boss == BOSS_Kraid ? 0 : boss == BOSS_Phantoon ? 1 : boss == BOSS_Draygon ? 2 : 3; }

static const char *const DEAD_NAME[4] = {"Kraid", "Phantoon", "Draygon", "Ridley"};

/* 0 when the candidate is accepted. */
static int try_candidate(Challenge *c, int attempt)
{
    char key[96];
    snprintf(key, sizeof key, "%s#%d", c->window, attempt);
    rng_state = hash_str(key);
    pick_start(c);
    {   /* a separate stream, so the rest of the candidate does not depend on it: 40 % start with 1 to 3 bosses dead */
        uint64_t keep = rng_state;
        char dk[112];
        snprintf(dk, sizeof dk, "%s#dead", key);
        rng_state = hash_str(dk);
        c->dead = 0;
        if (rnd_n(100) < 40) for (int k = 1 + rnd_n(3); k > 0;) { int b = 1 << rnd_n(4); if (!(c->dead & b)) { c->dead |= b; k--; } }
        rng_state = keep;
    }
    SmzSettings eff;
    if (smz_fill_valid(&c->seed, &c->s, &eff)) return -1;
    c->n_start_types = smz_start_items(&c->s, c->start_types, SMZ_MAX_START_ITEMS);
    if (pick_goal(c)) return -1;
    if (c->dead && (c->goal.kind == G_SMBOSSES || (c->goal.kind == G_BOSS && c->goal.target <= BOSS_Ridley && (c->dead >> sm_boss_bit(c->goal.target) & 1)))) return -1;
    if (route(c) || c->n_steps < 2) return -1;   /* unreachable, or open from the start */
    estimate(c);
    make_hints(c);
    if (c->n_hints && c->hints[c->n_hints - 1].at + c->est_route / c->n_steps > 180) return 1;   /* too many steps for the hints */
    return c->est_search >= MIN_EST && c->est_search <= MAX_EST ? 0 : 1;
}

/* The goal as the apps read it from memory (smz3_goal.h). */
static void goal_check(const Challenge *c, char *out, size_t cap)
{
    const Goal *g = &c->goal;
    switch (g->kind) {
    case G_ITEM: {
        int idx = -1;
        for (int q = 0; q < SMZ_LOC_NAME_COUNT && idx < 0; q++)
            if (SMZ_LOC_NAMES[q].id == g->target)
                for (unsigned j = 0; j < smz3_location_count; j++)
                    if (!strcmp(SMZ_LOC_NAMES[q].name, smz3_locations[j].id)) { idx = (int)j; break; }
        snprintf(out, cap, "loc:%d", idx);
        break;
    }
    case G_BOSS: {
        static const struct { int boss; const char *check; } B[] = {
            {BOSS_Kraid, "smboss:0"}, {BOSS_Phantoon, "smboss:1"}, {BOSS_Draygon, "smboss:2"}, {BOSS_Ridley, "smboss:3"}, {BOSS_Agahnim, "aga"},
            {BOSS_ArmosKnights, "zboss:c8"}, {BOSS_Lanmolas, "zboss:33"}, {BOSS_Moldorm, "zboss:7"}, {BOSS_HelmasaurKing, "zboss:5a"}, {BOSS_Arrghus, "zboss:6"},
            {BOSS_Mothula, "zboss:29"}, {BOSS_Blind, "zboss:ac"}, {BOSS_Kholdstare, "zboss:de"}, {BOSS_Vitreous, "zboss:90"}, {BOSS_Trinexx, "zboss:a4"}};
        snprintf(out, cap, "?");
        for (unsigned i = 0; i < sizeof B / sizeof B[0]; i++) if (B[i].boss == g->target) snprintf(out, cap, "%s", B[i].check);
        break;
    }
    case G_CRYSTALS: snprintf(out, cap, "crystals:%d", g->count); break;
    case G_SMBOSSES: snprintf(out, cap, "smbosses:%d", g->count); break;
    case G_REACH:
        switch (g->target) {
        case REG_GanonsTower: snprintf(out, cap, "zdungeon:1a"); break;
        case REG_TurtleRock: snprintf(out, cap, "zdungeon:18"); break;
        case REG_MiseryMire: snprintf(out, cap, "zdungeon:e"); break;
        case REG_IcePalace: snprintf(out, cap, "zdungeon:12"); break;
        case REG_WreckedShip: snprintf(out, cap, "smarea:3"); break;
        case REG_OuterMaridia: snprintf(out, cap, "smarea:4"); break;
        default: snprintf(out, cap, "smarea:2"); break;
        }
        break;
    }
}

/* ---------- output ---------- */

static void json_str(FILE *f, const char *s)
{
    fputc('"', f);
    for (; *s; s++) { if (*s == '"' || *s == '\\') fputc('\\', f); fputc(*s, f); }
    fputc('"', f);
}

static const char *const CFG_KEYS[] = {"PreventScrewAttackSoftLock", "PreventFivePowerBombSeed", "LeftSandPitRequiresSpringBall", "LaunchPadRequiresIceBeam",
    "WaterwayNeedsGravitySuit", "EasyEastCrateriaSkyItem", "EasyBlueBrinstarTop", "KholdstareNeedsCaneOfSomaria", "ZoraNeedsRupeeItems", "QuarterMagic",
    "PreventLowResourceDarkWorld", "FireRodDarkRooms", "InfiniteBombJump", "ParlorSpeedBooster", "MoatSpeedBooster", "MockBall", "SwordOnlyDarkRooms",
    "LightWorldSouthFakeFlippers"};

/* The settings in the run JSON format the apps store, so the hosts and smz3check rebuild the same seed from it. */
static int settings_json(const Challenge *c, char *out, size_t cap)
{
    const SmzSettings *s = &c->s;
    char check[32];
    goal_check(c, check, sizeof check);
    int n = snprintf(out, cap, "{\"seed\":\"%d\",\"keysanity\":\"None\",\"itemPlacement\":\"Anywhere\",\"gameMode\":\"Vanilla\",\"ganonCrystals\":%d,"
                     "\"ganonsTowerCrystals\":%d,\"tourianBosses\":%d,\"saveStates\":false,\"defaultRules\":false,\"challenge\":true,\"goal\":\"%s\",\"logic\":{",
                     (int)s->seed, s->ganon_crystals, s->gt_crystals, s->tourian_bosses, check);
    for (unsigned i = 0; i < sizeof CFG_KEYS / sizeof CFG_KEYS[0]; i++)
        n += snprintf(out + n, cap - n, "\"%s\":%s,", CFG_KEYS[i], smz_cfg_get(&s->cfg, CFG_KEYS[i]) ? "true" : "false");
    for (int g = 0; g < SMZ_ITEMOPT_GROUPS; g++)
        if (s->item_opt[g]) n += snprintf(out + n, cap - n, "\"opt:%s\":%d,", SMZ_ITEMOPT_GROUP[g].name, s->item_opt[g]);
    n += snprintf(out + n, cap - n, "\"WallJumpDifficulty\":%d}", s->cfg.WallJumpDifficulty);
    if (c->dead) n += snprintf(out + n, cap - n, ",\"smBossesDead\":%d", c->dead);
    n += snprintf(out + n, cap - n, "}");
    return n;
}

static void print_challenge(const Challenge *c, int attempt)
{
    static const char *const KIND[] = {"item", "boss", "crystals", "smBosses", "reach"};
    char js[8192];
    settings_json(c, js, sizeof js);
    printf("{\"window\":");
    json_str(stdout, c->window);
    printf(",\"attempt\":%d,\"seed\":%d,\"settings\":", attempt, (int)c->s.seed);
    json_str(stdout, js);
    printf(",\"start\":[");
    int first = 1;
    for (int b = 0; b < 4; b++)   /* first, so a short list on the device keeps them */
        if (c->dead >> b & 1) { printf("%s\"%s already defeated\"", first ? "" : ",", DEAD_NAME[b]); first = 0; }
    for (int g = 0; g < SMZ_ITEMOPT_GROUPS; g++)
        if (c->s.item_opt[g]) { printf("%s", first ? "" : ","); json_str(stdout, SMZ_ITEMOPT[SMZ_ITEMOPT_GROUP[g].first + c->s.item_opt[g]].display); first = 0; }
    char check[32];
    goal_check(c, check, sizeof check);
    printf("],\"goal\":{\"kind\":\"%s\",\"target\":%d,\"count\":%d,\"check\":\"%s\",\"text\":", KIND[c->goal.kind], c->goal.target, c->goal.count, check);
    json_str(stdout, c->goal.text);
    printf("},\"estimate\":{\"searchMin\":%.0f,\"routeMin\":%.0f},\"steps\":[", c->est_search, c->est_route);
    for (int i = 0; i < c->n_steps; i++) {
        printf("%s{\"item\":", i ? "," : "");
        json_str(stdout, item_name(c->steps[i].item));
        printf(",\"location\":");
        json_str(stdout, loc_name(c->steps[i].loc));
        printf(",\"region\":");
        json_str(stdout, region_name(loc_region(c->steps[i].loc)));
        printf(",\"game\":\"%s\"}", c->steps[i].game == RK_SM ? "sm" : "z3");
    }
    printf("],\"hints\":[");
    for (int i = 0; i < c->n_hints; i++) {
        printf("%s{\"atMin\":%.0f,\"text\":", i ? "," : "", c->hints[i].at);
        json_str(stdout, c->hints[i].text);
        printf("}");
    }
    printf("]}\n");
}

/* The settings JSON must rebuild the very same seed in the apps (smz_logic_settings is what they call). */
static int same_seed(const Challenge *c)
{
    static char js[8192];
    static SmzSeed again;
    SmzSettings s, eff;
    settings_json(c, js, sizeof js);
    smz_logic_settings(js, &s);
    if (smz_fill_valid(&again, &s, &eff)) return 0;
    return !memcmp(again.world.item_at, c->seed.world.item_at, sizeof again.world.item_at);
}

static int argc_g;
static char **argv_g;
static int result = 1;

static void *work(void *arg)
{
    (void)arg;
    static Challenge c;
    if (argc_g >= 4 && !strcmp(argv_g[1], "--stats")) {
        int n = atoi(argv_g[3]);
        for (int w = 0; w < n; w++) {
            snprintf(c.window, sizeof c.window, "%s-%d", argv_g[2], w);
            int tried = 0, ok = -1;
            for (int a = 0; a < MAX_TRIES && ok != 0; a++, tried++) ok = try_candidate(&c, a);
            printf("%s tries=%d ok=%d kind=%d steps=%d search=%.0f route=%.0f hints=%d last=%.0f goal=\"%s\"\n", c.window, tried, ok == 0, c.goal.kind,
                   c.n_steps, c.est_search, c.est_route, c.n_hints, c.n_hints ? c.hints[c.n_hints - 1].at : 0, c.goal.text);
            fflush(stdout);
        }
        result = 0;
        return NULL;
    }
    snprintf(c.window, sizeof c.window, "%s", argv_g[1]);
    for (int a = 0; a < MAX_TRIES; a++)
        if (try_candidate(&c, a) == 0) {
            if (!same_seed(&c)) { fprintf(stderr, "settings JSON does not rebuild the seed\n"); return NULL; }
            print_challenge(&c, a);
            result = 0;
            return NULL;
        }
    fprintf(stderr, "no candidate in range after %d tries\n", MAX_TRIES);
    return NULL;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: smz3daily <window-id> | --stats <prefix> <n>\n"); return 2; }
    argc_g = argc; argv_g = argv;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 256u << 20);   /* the generator keeps big structures on the stack */
    pthread_t th;
    if (pthread_create(&th, &at, work, NULL)) return 2;
    pthread_join(th, NULL);
    return result;
}
