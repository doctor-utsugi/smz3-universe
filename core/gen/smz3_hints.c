#include <string.h>
#include "smz3_hints.h"
#include "smz3_itemcats.h"
#include "smz3_world_gen.h"
#include "smz3_texts_data.h"
#include "smz3_hints_data.h"

#define MAX_ITEMS 1100

typedef struct {
    int cnt[256];                 /* items held, by type */
    SmzProg prog;                 /* rewards and bosses of the last round */
    uint8_t got[512];             /* locations collected */
    int order[512], norder;       /* collected locations in the order the spheres found them */
    bool threw;
} Play;

static int sum_rewards(const SmzProg *p) { int n = 0; for (int i = 0; i < 16; i++) n += p->rewards[i]; return n; }

/* PlaythroughService.GenerateSpheres: collects sphere by sphere until the seed is complete or nothing new opens. */
static void play(const SmzSeed *seed, const SmzPatchCfg *cfg, const int *extra, int nextra, Play *pl)
{
    static int items[MAX_ITEMS], reach[512];
    int n = 0;
    memset(pl, 0, sizeof *pl);
    for (int i = 0; i < nextra; i++) items[n++] = extra[i];
    if (!seed->world.metroid_keysanity)
        for (int t = ITEM_CardCrateriaL1; t <= ITEM_CardLowerNorfairBoss; t++) items[n++] = t;
    for (int i = 0; i < seed->n_start; i++) items[n++] = seed->start_items[i];
    int init = n, prev_rewards = 0;
    for (int sphere = 0; n - init < SMZ_LOC_COUNT; sphere++) {
        SmzProg p;
        int k = smz_reachable_prog(seed, items, n, reach, 512, &p);
        pl->prog = p;
        int added = 0;
        for (int i = 0; i < k; i++) {
            if (pl->got[reach[i]]) continue;
            pl->got[reach[i]] = 1;
            pl->order[pl->norder++] = reach[i];
            int t = seed->world.item_at[reach[i]];
            items[n++] = t;
            added++;
        }
        int rc = sum_rewards(&p);
        if (!added && prev_rewards == rc) {
            int inaccessible = SMZ_LOC_COUNT - pl->norder;
            if (inaccessible >= 15) {
                int have_card = 0, swords = 0, silver = 0;
                for (int i = 0; i < n; i++) {
                    if (items[i] == ITEM_CardCrateriaBoss) have_card = 1;
                    if (items[i] == ITEM_ProgressiveSword) swords++;
                    if (items[i] == ITEM_SilverArrows) silver = 1;
                }
                int crystals = p.rewards[REWARD_CrystalBlue] + p.rewards[REWARD_CrystalRed];
                int tokens = p.rewards[REWARD_KraidToken] + p.rewards[REWARD_PhantoonToken] + p.rewards[REWARD_DraygonToken] + p.rewards[REWARD_RidleyToken];
                const SmzWorld *w = &seed->world;
                int need = cfg->pyramid_open ? w->ganon_crystals : (w->ganon_crystals > w->gt_crystals ? w->ganon_crystals : w->gt_crystals);
                if ((w->metroid_keysanity && !cfg->tourian_door_open && !have_card) || crystals < need || tokens < w->tourian_bosses ||
                    (swords < 2 && !silver))
                    pl->threw = true;
            }
            break;
        }
        prev_rewards = rc;
        if (sphere >= 100) { pl->threw = true; break; }
    }
    for (int i = 0; i < n; i++) pl->cnt[items[i]]++;
}

static int loc_region_of(int loc)
{
    for (int i = 0; i < SMZ_LOC_COUNT; i++) if (SMZ_LOCS[i].id == loc) return SMZ_LOCS[i].region;
    return 0;
}

static int is_keysanity_key(const SmzWorld *w, int type)
{
    uint32_t c = SMZ_ITEM_CATS[type];
    return ((c & (ICAT_SmallKey | ICAT_BigKey)) && w->zelda_keysanity) || ((c & ICAT_Keycard) && w->metroid_keysanity);
}

static int usefulness_of(const SmzSeed *seed, const SmzPatchCfg *cfg, const uint16_t *locs, int n, const int *defaults, int ndefaults);

static int mandatory_check(const SmzSeed *seed, const SmzPatchCfg *cfg, const uint16_t *locs, int n)
{
    const SmzWorld *w = &seed->world;
    int keys[64], nk = 0;
    uint16_t rest[64];
    int nr = 0;
    for (int i = 0; i < n; i++) {
        int t = w->item_at[locs[i]];
        if (is_keysanity_key(w, t) && nk < 64) keys[nk++] = t;
        else rest[nr++] = locs[i];
    }
    if (!nk) return USE_Mandatory;
    return usefulness_of(seed, cfg, rest, nr, keys, nk) == USE_Mandatory ? USE_Mandatory : USE_Key;
}

/* CheckIfLocationsAreImportant for a single (vanilla game mode) world. */
static int usefulness_of(const SmzSeed *seed, const SmzPatchCfg *cfg, const uint16_t *locs, int n, const int *defaults, int ndefaults)
{
    const SmzWorld *w0 = &seed->world;
    int seed_mode = seed->game_mode;
    static SmzSeed copy;
    copy = *seed;
    for (int i = 0; i < n; i++) {
        int region = loc_region_of(locs[i]);
        int t = w0->item_at[locs[i]];
        bool keep = !w0->zelda_keysanity && region_has_item(region, t) && region == REG_GanonsTower;
        if (!keep) copy.world.item_at[locs[i]] = ITEM_TwentyRupees;
    }
    static Play pl;
    play(&copy, cfg, defaults, ndefaults, &pl);
    if (pl.threw) return mandatory_check(seed, cfg, locs, n);

    int use = USE_None;
    int crystals = pl.prog.rewards[REWARD_CrystalBlue] + pl.prog.rewards[REWARD_CrystalRed];
    int tokens = pl.prog.rewards[REWARD_KraidToken] + pl.prog.rewards[REWARD_PhantoonToken] + pl.prog.rewards[REWARD_DraygonToken] + pl.prog.rewards[REWARD_RidleyToken];
    if (pl.cnt[ITEM_ProgressiveSword] < 2 || !pl.cnt[ITEM_SilverArrows]) use = USE_Mandatory;
    else if (w0->metroid_keysanity && !cfg->tourian_door_open && !pl.cnt[ITEM_CardCrateriaBoss]) use = USE_Mandatory;
    else if (!cfg->pyramid_open && !pl.got[LOC_GanonsTowerMoldormChest]) use = USE_Mandatory;
    else if (seed_mode == MODE_Vanilla && (crystals < w0->ganon_crystals || tokens < w0->tourian_bosses)) use = USE_Mandatory;
    else if (seed_mode == MODE_AllDungeons) {
        int pendants = pl.prog.rewards[REWARD_PendantGreen] + pl.prog.rewards[REWARD_PendantBlue] + pl.prog.rewards[REWARD_PendantRed];
        if (crystals < 7 || pendants < 3 || tokens < 4 || !pl.got[LOC_GanonsTowerMoldormChest]) use = USE_Mandatory;
    }
    if (use == USE_None && seed_mode == MODE_SpazerHunt) {
        /* the goal locations are the ones holding a spazer: goal blocking when one is unreachable or among the checked locations */
        for (int l = 0; l < SMZ_LOC_COUNT && use == USE_None; l++) {
            int id = SMZ_LOCS[l].id;
            if (w0->item_at[id] != ITEM_Spazer) continue;
            bool checked = false;
            for (int i = 0; i < n; i++) if (locs[i] == id) checked = true;
            if (checked || !loc_available(w0, id, &pl.prog)) use = USE_Goal;
        }
    }
    if (use == USE_None) {
        use = USE_Useless;
        bool sword = false;
        for (int i = 0; i < n; i++) if (w0->item_at[locs[i]] == ITEM_ProgressiveSword) sword = true;
        if (sword) use = USE_Sword;
        else for (int i = 0; i < n; i++) {
            uint32_t c = SMZ_ITEM_CATS[w0->item_at[locs[i]]];
            if (((c & ICAT_PossibleProgression) && !(c & ICAT_Junk)) || (c & ICAT_Nice)) { use = USE_NiceToHave; break; }
        }
    }
    if (use == USE_Mandatory && (w0->zelda_keysanity || w0->metroid_keysanity) && mandatory_check(seed, cfg, locs, n) == USE_Key)
        use = USE_Key;
    return use;
}

int smz_locations_usefulness(const SmzSeed *seed, const SmzPatchCfg *cfg, const uint16_t *locs, int n)
{
    return usefulness_of(seed, cfg, locs, n, NULL, 0);
}

int32_t smz_hint_seed(const SmzSeed *seed)
{
    SmzRng r = seed->rng;
    rng_next(&r);                 /* the patch seed */
    return rng_next(&r);
}

enum { C_PROG, C_DUNGEON, C_LOCATION, C_MEDALLION };
typedef struct { int kind, idx; } Cand;

static void shuffle(SmzRng *r, Cand *a, int count)
{
    for (int n = count - 1; n > 0; n--) {
        int k = rng_next_max(r, n + 1);
        Cand t = a[n]; a[n] = a[k]; a[k] = t;
    }
}

int smz_hints_generate(const SmzSeed *seed, const SmzPatchCfg *cfg, int32_t hint_seed, int count, SmzHints *out)
{
    const SmzWorld *w = &seed->world;
    out->n = 0;
    if (count <= 0) return 0;
    if (count > SMZ_HINT_TILE_COUNT) count = SMZ_HINT_TILE_COUNT;
    SmzRng rnd;
    rng_init(&rnd, hint_seed);
    rng_sanitize(&rnd);

    static Play pl;
    play(seed, cfg, NULL, 0, &pl);

    /* progression item hints: the last third of the playthrough's progression locations, shuffled, 5 of them */
    Cand prog[512], pool[256];
    int np = 0, npool = 0;
    for (int i = 0; i < pl.norder; i++) {
        uint32_t c = SMZ_ITEM_CATS[w->item_at[pl.order[i]]];
        if ((c & ICAT_PossibleProgression) && !(c & ICAT_Scam)) { prog[np].kind = C_PROG; prog[np].idx = pl.order[i]; np++; }
    }
    int third = np / 3;
    Cand last[512];
    memcpy(last, prog + (np - third), third * sizeof(Cand));
    shuffle(&rnd, last, third);
    for (int i = 0; i < third && i < 5; i++) pool[npool++] = last[i];
    for (int i = 0; i < SMZ_HINT_DUNGEON_COUNT; i++) {
        bool reward = i < SMZ_HINT_REWARD_DUNGEONS;
        int reg = SMZ_HINT_DUNGEONS[i].region;
        if (reward && !(w->reward_of[reg] == REWARD_PendantBlue || w->reward_of[reg] == REWARD_PendantGreen || w->reward_of[reg] == REWARD_PendantRed)) continue;
        pool[npool].kind = C_DUNGEON; pool[npool++].idx = i;
    }
    for (int i = 0; i < SMZ_HINT_LOCATION_COUNT; i++) { pool[npool].kind = C_LOCATION; pool[npool++].idx = i; }
    for (int i = 0; i < SMZ_HINT_MEDALLION_COUNT; i++) { pool[npool].kind = C_MEDALLION; pool[npool++].idx = i; }
    shuffle(&rnd, pool, npool);
    if (npool > count) npool = count;

    SmzHint hints[SMZ_MAX_HINTS];
    for (int i = 0; i < npool; i++) {
        SmzHint *h = &hints[i];
        h->tile = -1; h->loc = -1; h->region = -1; h->def = -1; h->usefulness = USE_None; h->medallion = -1;
        Cand c = pool[i];
        if (c.kind == C_PROG) {
            h->type = HINT_Location; h->loc = c.idx;
        } else if (c.kind == C_DUNGEON) {
            const SmzHintDungeon *d = &SMZ_HINT_DUNGEONS[c.idx];
            h->type = HINT_Dungeon; h->region = d->region; h->def = c.idx;
            h->usefulness = smz_locations_usefulness(seed, cfg, d->locs, d->nloc);
        } else if (c.kind == C_LOCATION) {
            const SmzHintDef *d = &SMZ_HINT_LOCATIONS[c.idx];
            h->type = d->type; h->def = c.idx; h->region = d->region;
            if (d->type == HINT_Location) h->loc = d->locs[0];
            else h->usefulness = smz_locations_usefulness(seed, cfg, d->locs, d->nloc);
        } else {
            h->type = HINT_Requirement; h->region = SMZ_HINT_MEDALLION_REGIONS[c.idx];
            h->medallion = w->prereq[h->region];
        }
    }

    /* tiles are dealt in a shuffled order, one hint each, wrapping around */
    Cand tiles[SMZ_HINT_TILE_COUNT];
    for (int i = 0; i < SMZ_HINT_TILE_COUNT; i++) { tiles[i].kind = 0; tiles[i].idx = i; }
    shuffle(&rnd, tiles, SMZ_HINT_TILE_COUNT);
    for (int i = 0; i < SMZ_HINT_TILE_COUNT; i++) {
        out->h[i] = hints[i % npool];
        out->h[i].tile = tiles[i].idx;
    }
    out->n = SMZ_HINT_TILE_COUNT;
    return out->n;
}
