#include <string.h>
#include "sm_fill.h"

#define MAXI 128
#define MAX_PASSES 4096
typedef struct { SmItem v[MAXI]; int n; } List;
typedef struct { int v[SM_LOC_COUNT]; int n; } LocList;

typedef struct {
    const SmGenSettings *s;
    SmzRng *rnd;
    SmItem *at;      /* item_at of the seed */
    bool filled[SM_LOC_COUNT];
    List placed;     /* world.Items */
} Fill;

void sm_settings_defaults(SmGenSettings *s)
{
    memset(s, 0, sizeof *s);
    s->logic = SM_LOGIC_Tournament;
    s->placement = SM_PLACEMENT_Split;
}

/* ---- lists, kept in the C# List<T> order (Remove is stable, Add appends) */
static uint16_t next_id;
static void add(List *l, int type, int cls, const char *name)
{
    SmItem *i = &l->v[l->n++];
    i->type = (uint8_t)type; i->cls = (uint8_t)cls; i->name = name; i->id = next_id++;
}
static void list_remove_id(List *l, uint16_t id)
{
    for (int i = 0; i < l->n; i++)
        if (l->v[i].id == id) { memmove(&l->v[i], &l->v[i + 1], (l->n - i - 1) * sizeof(SmItem)); l->n--; return; }
}
static int list_find_type(const List *l, int type)
{
    for (int i = 0; i < l->n; i++) if (l->v[i].type == type) return i;
    return -1;
}
static void loc_remove(LocList *l, int idx)
{
    for (int i = 0; i < l->n; i++)
        if (l->v[i] == idx) { memmove(&l->v[i], &l->v[i + 1], (l->n - i - 1) * sizeof(int)); l->n--; return; }
}

/* LocationListExtensions.Shuffle on a copy; the caller only wants the first element */
static SmItem first_shuffled_item(const List *l, SmzRng *r)
{
    static List c;
    c = *l;
    for (int n = c.n; --n > 0;) {
        int k = rng_next_max(r, n + 1);
        SmItem t = c.v[k]; c.v[k] = c.v[n]; c.v[n] = t;
    }
    return c.v[0];
}
static int first_shuffled_loc(LocList *l, SmzRng *r)
{
    for (int n = l->n; --n > 0;) {
        int k = rng_next_max(r, n + 1);
        int t = l->v[k]; l->v[k] = l->v[n]; l->v[n] = t;
    }
    return l->n ? l->v[0] : -1;
}

void sm_prog_of(SmProg *p, const SmItem *items, int n)
{
    memset(p, 0, sizeof *p);
    for (int i = 0; i < n; i++) p->n[items[i].type]++;
}

/* ---- pools (Item.CreateProgressionPool / CreateNicePool / CreateJunkPool) */
static void make_pools(List *prog, List *nice, List *junk, SmzRng *rnd)
{
    static const struct { int type; const char *name; } majors[] = {
        { SM_ITEM_Morph, "Morphing Ball" }, { SM_ITEM_Bombs, "Bombs" }, { SM_ITEM_Ice, "Ice Beam" },
        { SM_ITEM_HiJump, "Hi-Jump Boots" }, { SM_ITEM_SpeedBooster, "Speed Booster" }, { SM_ITEM_ScrewAttack, "Screw Attack" },
        { SM_ITEM_Varia, "Varia Suit" }, { SM_ITEM_Gravity, "Gravity Suit" }, { SM_ITEM_Grapple, "Grappling Beam" },
        { SM_ITEM_SpaceJump, "Space Jump" }, { SM_ITEM_SpringBall, "Spring Ball" }, { SM_ITEM_Wave, "Wave Beam" },
        { SM_ITEM_Plasma, "Plasma Beam" }, { SM_ITEM_Charge, "Charge Beam" },
    };
    next_id = 0;
    prog->n = nice->n = junk->n = 0;
    for (size_t i = 0; i < sizeof majors / sizeof *majors; i++) add(prog, majors[i].type, SM_CLASS_Major, majors[i].name);
    add(prog, SM_ITEM_Missile, SM_CLASS_Minor, "Progression Missile");
    add(prog, SM_ITEM_Super, SM_CLASS_Minor, "Progression Super");
    add(prog, SM_ITEM_PowerBomb, SM_CLASS_Minor, "Progression Power Bomb");
    add(prog, SM_ITEM_PowerBomb, SM_CLASS_Minor, "Progression Power Bomb");
    for (int i = 0; i < 4; i++) add(prog, SM_ITEM_ETank, SM_CLASS_Major, "Progression Energy Tank");

    add(nice, SM_ITEM_Spazer, SM_CLASS_Major, "Spazer");
    add(nice, SM_ITEM_XRay, SM_CLASS_Major, "X-Ray Scope");

    for (int i = 0; i < 10; i++) add(junk, SM_ITEM_ETank, SM_CLASS_Major, "Energy Tank");
    for (int i = 0; i < 4; i++) add(junk, SM_ITEM_ReserveTank, SM_CLASS_Major, "Reserve Tank");
    for (int i = 0; i < 62; i++) {
        int r = rng_next_max(rnd, 7);
        if (r < 3) add(junk, SM_ITEM_Missile, SM_CLASS_Minor, "Missile");
        else if (r < 6) add(junk, SM_ITEM_Super, SM_CLASS_Minor, "Super Missile");
        else add(junk, SM_ITEM_PowerBomb, SM_CLASS_Minor, "Power Bomb");
    }
}

/* ---- Filler */
static bool streq(const char *a, const char *b) { return strcmp(a, b) == 0; }

/* Filler.CanPlaceAtLocation, single world */
static bool can_place(int idx, int type)
{
    const SmLocDef *l = &SM_LOCS[idx];
    const char *area = SM_REGIONS[l->region].area;
    bool early = streq(l->name, "Morphing Ball") || streq(l->name, "Missile (blue Brinstar middle)") || streq(l->name, "Energy Tank, Brinstar Ceiling");
    switch (type) {
    case SM_ITEM_Gravity:
        return !(streq(area, "Crateria") || streq(area, "Brinstar")) || streq(l->name, "X-Ray Scope") || streq(l->name, "Energy Tank, Waterway");
    case SM_ITEM_Varia:
        return !(streq(area, "LowerNorfair") || streq(area, "Crateria") || early);
    case SM_ITEM_SpeedBooster: case SM_ITEM_ScrewAttack:
        return !early;
    default:
        return true;
    }
}

static void place(Fill *f, int idx, SmItem item)
{
    f->at[idx] = item;
    f->filled[idx] = true;
    f->placed.v[f->placed.n++] = item;
}

/* Filler.CollectItems: the assumed items plus everything their reachable locations hold, to a fixed point */
static void collect(const Fill *f, const List *assumed, SmProg *p)
{
    bool got[SM_LOC_COUNT] = { 0 };
    sm_prog_of(p, assumed->v, assumed->n);
    for (bool more = true; more;) {
        SmProg snap = *p;
        more = false;
        for (int i = 0; i < SM_LOC_COUNT; i++) {
            if (!f->filled[i] || got[i] || !sm_loc_available(i, f->s->logic, &snap)) continue;
            got[i] = true;
            p->n[f->at[i].type]++;
            more = true;
        }
    }
}

/* LocationListExtensions.CanFillWithinWorld, single world: in logic with the inventory, and with the item added */
static void can_fill_within(const Fill *f, const LocList *in, int type, const SmProg *inv, LocList *out)
{
    SmProg with = *inv;
    with.n[type]++;
    out->n = 0;
    for (int i = 0; i < in->n; i++)
        if (sm_loc_available(in->v[i], f->s->logic, inv) && sm_loc_available(in->v[i], f->s->logic, &with)) out->v[out->n++] = in->v[i];
}

static void empty_locs(const Fill *f, LocList *l)
{
    l->n = 0;
    for (int i = 0; i < SM_LOC_COUNT; i++) if (!f->filled[i]) l->v[l->n++] = i;
}

/* Filler.FrontFillItemInWorld: the first item of that type, at a random location in logic with the items placed so far */
static int front_fill(Fill *f, List *pool, int type)
{
    int k = list_find_type(pool, type);
    if (k < 0) return -1;
    SmItem item = pool->v[k];
    SmProg have;
    sm_prog_of(&have, f->placed.v, f->placed.n);
    LocList cand = { .n = 0 };
    for (int i = 0; i < SM_LOC_COUNT; i++) {
        if (f->filled[i] || !can_place(i, type)) continue;
        if (f->s->placement == SM_PLACEMENT_Split && SM_LOCS[i].cls != item.cls) continue;
        if (sm_loc_available(i, f->s->logic, &have)) cand.v[cand.n++] = i;
    }
    int idx = first_shuffled_loc(&cand, f->rnd);
    if (idx < 0) return -1;
    place(f, idx, item);
    list_remove_id(pool, item.id);
    return 0;
}

static int initial_fill(Fill *f, List *items)
{
    bool split = f->s->placement == SM_PLACEMENT_Split;
    if (front_fill(f, items, SM_ITEM_Morph)) return -1;
    if (front_fill(f, items, rng_next_max(f->rnd, 2) == 0 ? SM_ITEM_Missile : SM_ITEM_Super)) return -1;
    if (split && f->s->logic == SM_LOGIC_Casual) {
        if (front_fill(f, items, SM_ITEM_PowerBomb)) return -1;
    } else {
        static const int breakers[8] = { SM_ITEM_ScrewAttack, SM_ITEM_SpeedBooster, SM_ITEM_Bombs, SM_ITEM_PowerBomb,
                                         SM_ITEM_PowerBomb, SM_ITEM_PowerBomb, SM_ITEM_PowerBomb, SM_ITEM_PowerBomb };
        if (front_fill(f, items, breakers[rng_next_max(f->rnd, 8)])) return -1;
    }
    if (split) {
        if (list_find_type(&f->placed, SM_ITEM_Super) < 0 && front_fill(f, items, SM_ITEM_Super)) return -1;
        if (list_find_type(&f->placed, SM_ITEM_PowerBomb) < 0 && front_fill(f, items, SM_ITEM_PowerBomb)) return -1;
    }
    return 0;
}

/* Filler.PriorityFill (no CanPlaceAtLocation here, as in the C#) and Filler.AssumedFill share this loop */
static int assumed_loop(Fill *f, List *items, List *pending, const List *assumed_fixed, bool restrict_place)
{
    LocList locs, cls, cand;
    empty_locs(f, &locs);
    for (int pass = 0; pending->n > 0; pass++) {
        if (pass > MAX_PASSES) return -2;
        SmItem item = first_shuffled_item(pending, f->rnd);
        list_remove_id(pending, item.id);
        SmProg inv;
        collect(f, assumed_fixed ? assumed_fixed : pending, &inv);
        cls.n = 0;
        for (int i = 0; i < locs.n; i++) {
            int idx = locs.v[i];
            if (f->s->placement == SM_PLACEMENT_Split && SM_LOCS[idx].cls != item.cls) continue;
            if (restrict_place && !can_place(idx, item.type)) continue;
            cls.v[cls.n++] = idx;
        }
        can_fill_within(f, &cls, item.type, &inv, &cand);
        if (cand.n == 0) {
            pending->v[pending->n++] = item;
            continue;
        }
        int idx = first_shuffled_loc(&cand, f->rnd);
        place(f, idx, item);
        list_remove_id(items, item.id);
        loc_remove(&locs, idx);
    }
    return 0;
}

static int priority_fill(Fill *f, List *items)
{
    static List assumed, prio;
    assumed.n = prio.n = 0;
    for (int i = 0; i < items->n; i++) {
        bool p = items->v[i].type == SM_ITEM_Varia || items->v[i].type == SM_ITEM_Gravity;
        (p ? &prio : &assumed)->v[(p ? &prio : &assumed)->n++] = items->v[i];
    }
    return assumed_loop(f, items, &prio, &assumed, false);
}

static int assumed_fill(Fill *f, List *items)
{
    static List assumed;
    assumed = *items;
    return assumed_loop(f, items, &assumed, NULL, true);
}

static int fast_fill(Fill *f, List *items)
{
    LocList locs;
    while (items->n > 0) {
        SmItem item = first_shuffled_item(items, f->rnd);
        locs.n = 0;
        for (int i = 0; i < SM_LOC_COUNT; i++)
            if (!f->filled[i] && (f->s->placement != SM_PLACEMENT_Split || SM_LOCS[i].cls == item.cls)) locs.v[locs.n++] = i;
        int idx = first_shuffled_loc(&locs, f->rnd);
        if (idx < 0) return -1;
        place(f, idx, item);
        list_remove_id(items, item.id);
    }
    return 0;
}

int sm_fill(SmSeed *out, const SmGenSettings *s)
{
    static List prog, nice, junk;
    SmzRng rnd;
    memset(out, 0, sizeof *out);
    out->s = *s;
    rng_init(&rnd, s->seed);
    if (s->race) rng_init(&rnd, rng_next(&rnd));

    Fill f;
    memset(&f, 0, sizeof f);
    f.s = &out->s;
    f.rnd = &rnd;
    f.at = out->item_at;
    make_pools(&prog, &nice, &junk, &rnd);

    int r;
    if ((r = initial_fill(&f, &prog)) || (r = priority_fill(&f, &prog)) || (r = assumed_fill(&f, &prog)) ||
        (r = fast_fill(&f, &nice)) || (r = fast_fill(&f, &junk)))
        return r;
    out->patch_seed = rng_next(&rnd);
    return 0;
}

int sm_playthrough_ok(const SmSeed *seed)
{
    bool got[SM_LOC_COUNT] = { 0 };
    SmProg p;
    memset(&p, 0, sizeof p);
    for (int n = 0, more = 1; more;) {
        SmProg snap = p;
        more = 0;
        for (int i = 0; i < SM_LOC_COUNT; i++) {
            if (got[i] || !sm_loc_available(i, seed->s.logic, &snap)) continue;
            got[i] = true;
            p.n[seed->item_at[i].type]++;
            more = 1;
            n++;
        }
        if (n == SM_LOC_COUNT) return 1;
    }
    return 0;
}
