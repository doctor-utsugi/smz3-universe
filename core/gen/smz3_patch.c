#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smz3_jingles_data.h"
#include "smz3_plando.h"
#include "smz3_patch.h"
#include "smz3_itemopts.h"
#include "smz3_world_gen.h"
#include "smz3_text.h"

typedef struct {
    uint8_t *rom;
    SmzPatchLog log;
    void *user;
    const char *cls;
    const SmzSeed *seed;
    const SmzSettings *s;
    const SmzPatchCfg *cfg;
    SmzRng *rnd;
    int err;
} P;

static int snes(P *p, uint32_t addr)
{
    uint32_t a;
    if ((addr & 0xFF8000) == 0x308000) a = 0x400000 | (addr & 0x7FFF);
    else a = (addr < 0x800000 ? 0x400000 : 0) | (addr & 0x3FFFFF);
    if (a > 0x600000) { p->err = 1; return 0; }
    return (int)a;
}

static void put(P *p, int off, const void *d, int n)
{
    if (off < 0 || off + n > SMZ_ROM_SIZE) { p->err = 1; return; }
    memcpy(p->rom + off, d, n);
    if (p->log) p->log(p->user, p->cls, off, d, n);
}
static void put1(P *p, int off, int v) { uint8_t b = (uint8_t)v; put(p, off, &b, 1); }
static void put2(P *p, int off, int v) { uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) }; put(p, off, b, 2); }
static void put4(P *p, int off, uint32_t v) { uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) }; put(p, off, b, 4); }
#define SNES(a) snes(p, (a))

static int rnd_next(P *p, int max) { return rng_next_max(p->rnd, max); }

void smz_patch_cfg_defaults(SmzPatchCfg *c, const SmzSettings *s)
{
    memset(c, 0, sizeof *c);
    c->player_name = "Player";
    c->hint_tiles = 15;
    c->heart_color = 0;
    c->low_health_beep = 0x40;
    c->menu_speed = 1;
    c->disable_flashing = c->prevent_scams = c->randomized_bottles = c->infinite_space_jump = c->no_bozo_softlock = 1;
    c->metroid_fast_pause_menu = c->metroid_auto_save = 1;
    c->zelda_drops = 0;
    static const uint16_t controls[7] = { 0x0040, 0x0080, 0x8000, 0x2000, 0x4000, 0x0010, 0x0020 };
    memcpy(c->controls, controls, sizeof controls);
    /* A guid derived from the seed, so the same seed always yields the same ROM */
    SmzRng r;
    rng_init(&r, s->seed);
    for (int g = 0; g < 2; g++) {
        char *dst = g ? c->world_guid : c->seed_guid;
        for (int i = 0; i < 32; i++) dst[i] = "0123456789abcdef"[rng_next_max(&r, 16)];
        dst[32] = 0;
    }
}

/* ---- Metadata ($-9) */
static void metadata(P *p)
{
    p->cls = "MetadataPatch";
    uint8_t block[1024];
    for (int i = 0; i < 1024; i++) block[i] = (uint8_t)rnd_next(p, 0x100);
    put(p, 0x420000, block, 1024);

    const char *names[2] = { p->cfg->player_name, "Tracker" };
    for (int i = 0; i < 2; i++) {
        char n[12], full[16];
        size_t len = strlen(names[i]);
        if (len > 12) len = 12;
        memcpy(n, names[i], len);
        while (len > 0 && n[len - 1] == ' ') len--;
        int pad = (12 - (int)len) / 2;
        memset(full, ' ', 12);
        memcpy(full + pad, n, len);
        memset(full + 12, 0, 4);
        put(p, 0x385000 + i * 16, full, 16);
    }
    int keysanity = p->seed->world.zelda_keysanity || p->seed->world.metroid_keysanity;
    int field = (keysanity << 13) | (1 << 12) | (9 << 4) | 0;
    put2(p, SNES(0x80FF50), 0);
    put2(p, SNES(0x80FF52), field);
    put4(p, SNES(0x80FF54), (uint32_t)p->s->seed);
    static const uint8_t zero8[8] = { 0 };
    put(p, SNES(0x80FF58), zero8, 8);
    put(p, SNES(0x80FF60), p->cfg->seed_guid, 32);
    put(p, SNES(0x80FF80), p->cfg->world_guid, 32);
    char title[32];
    snprintf(title, sizeof title, "SMZ3 Cas' [%08X]", (unsigned)p->s->seed);
    size_t tl = strlen(title);
    while (tl < 21) title[tl++] = ' ';
    put(p, SNES(0x00FFC0), title, 21);
    put(p, SNES(0x80FFC0), title, 21);
    put2(p, SNES(0xF47000), 1);
    if (p->seed->world.metroid_keysanity) put2(p, SNES(0xF47006), 1);
}

/* ---- Rewards ($-10) */
static const int REWARD_ADDR[14][7] = {
    [REG_EasternPalace] = { 0x2A09D, 0xABEF8, 0xABEF9, 0x308052, 0x30807C, 0x1C6FE, 0x30D100 },
    [REG_DesertPalace] = { 0x2A09E, 0xABF1C, 0xABF1D, 0x308053, 0x308078, 0x1C6FF, 0x30D101 },
    [REG_TowerOfHera] = { 0x2A0A5, 0xABF0A, 0xABF0B, 0x30805A, 0x30807A, 0x1C706, 0x30D102 },
    [REG_PalaceOfDarkness] = { 0x2A0A1, 0xABF00, 0xABF01, 0x308056, 0x30807D, 0x1C702, 0x30D103 },
    [REG_SwampPalace] = { 0x2A0A0, 0xABF6C, 0xABF6D, 0x308055, 0x308071, 0x1C701, 0x30D104 },
    [REG_SkullWoods] = { 0x2A0A3, 0xABF12, 0xABF13, 0x308058, 0x30807B, 0x1C704, 0x30D105 },
    [REG_ThievesTown] = { 0x2A0A6, 0xABF36, 0xABF37, 0x30805B, 0x308077, 0x1C707, 0x30D106 },
    [REG_IcePalace] = { 0x2A0A4, 0xABF5A, 0xABF5B, 0x308059, 0x308073, 0x1C705, 0x30D107 },
    [REG_MiseryMire] = { 0x2A0A2, 0xABF48, 0xABF49, 0x308057, 0x308075, 0x1C703, 0x30D108 },
    [REG_TurtleRock] = { 0x2A0A7, 0xABF24, 0xABF25, 0x30805C, 0x308079, 0x1C708, 0x30D109 },
};

static const uint8_t CRYSTAL_VALUES[8][7] = {
    { 0 },
    { 0x02, 0x34, 0x64, 0x40, 0x7F, 0x06, 0x10 }, { 0x10, 0x34, 0x64, 0x40, 0x79, 0x06, 0x10 },
    { 0x40, 0x34, 0x64, 0x40, 0x6C, 0x06, 0x10 }, { 0x20, 0x34, 0x64, 0x40, 0x6D, 0x06, 0x10 },
    { 0x04, 0x32, 0x64, 0x40, 0x6E, 0x06, 0x11 }, { 0x01, 0x32, 0x64, 0x40, 0x6F, 0x06, 0x11 },
    { 0x08, 0x34, 0x64, 0x40, 0x7C, 0x06, 0x10 },
};
static const uint8_t PENDANT_VALUES[4][7] = {
    { 0 },
    { 0x04, 0x38, 0x62, 0x00, 0x69, 0x01, 0x12 },
    { 0x01, 0x32, 0x60, 0x00, 0x69, 0x03, 0x14 },
    { 0x02, 0x34, 0x60, 0x00, 0x69, 0x02, 0x13 },
};

static void shuffle_ints(P *p, int *v, int n)
{
    while ((n -= 1) > 0) {
        int k = rnd_next(p, n + 1);
        int t = v[n]; v[n] = v[k]; v[k] = t;
    }
}

static void reward_patches(P *p, const int *regions, int nregions, const int *rewards, const uint8_t (*values)[7])
{
    for (int i = 0; i < nregions; i++)
        for (int j = 0; j < 7; j++) put1(p, SNES(REWARD_ADDR[regions[i]][j]), values[rewards[i]][j]);
}

static void zelda_rewards(P *p)
{
    p->cls = "ZeldaRewardsPatch";
    int blue[5] = { 1, 2, 3, 4, 7 }, red[2] = { 5, 6 };
    shuffle_ints(p, blue, 5);
    shuffle_ints(p, red, 2);
    int crystal_rewards[7];
    memcpy(crystal_rewards, blue, sizeof blue);
    memcpy(crystal_rewards + 5, red, sizeof red);

    const SmzWorld *w = &p->seed->world;
    int crystal_regions[7], nc = 0, pend[3], np = 0;
    for (int r = 0; r < REG_COUNT; r++) if (SMZ_REGIONS[r].has_reward && w->reward_of[r] == REWARD_CrystalBlue) crystal_regions[nc++] = r;
    for (int r = 0; r < REG_COUNT; r++) if (SMZ_REGIONS[r].has_reward && w->reward_of[r] == REWARD_CrystalRed) crystal_regions[nc++] = r;
    static const int pendant_order[3] = { REWARD_PendantGreen, REWARD_PendantRed, REWARD_PendantBlue };
    for (int t = 0; t < 3; t++)
        for (int r = 0; r < REG_COUNT; r++) if (SMZ_REGIONS[r].has_reward && w->reward_of[r] == pendant_order[t]) pend[np++] = r;
    reward_patches(p, crystal_regions, nc, crystal_rewards, CRYSTAL_VALUES);
    static const int pendant_rewards[3] = { 1, 2, 3 };
    reward_patches(p, pend, np, pendant_rewards, PENDANT_VALUES);
}

/* ---- Digging game, fairy pond ($-8, $-6) */
static void digging(P *p)
{
    p->cls = "DiggingGamePatch";
    int digs = rnd_next(p, 30) + 1;
    put1(p, SNES(0x308020), digs);
    put1(p, SNES(0x1DFD95), digs);
}

static void fairy_pond(P *p)
{
    p->cls = "FairyPondTradePatch";
    if (!p->cfg->randomized_bottles) return;
    int bottles[16], n = 0;
    for (int t = 0; t < 256; t++) if (SMZ_ITEM_CATS[t] & ICAT_Bottle) bottles[n++] = t;
    const SmzPlando *pl = p->cfg->plando;
    int waterfall = pl && pl->waterfall_trade >= 0 && (SMZ_ITEM_CATS[pl->waterfall_trade] & ICAT_Bottle) ? pl->waterfall_trade : bottles[rnd_next(p, n)];
    int pyramid = pl && pl->pyramid_trade >= 0 && (SMZ_ITEM_CATS[pl->pyramid_trade] & ICAT_Bottle) ? pl->pyramid_trade : bottles[rnd_next(p, n)];
    put1(p, SNES(0x6C8FF), waterfall);
    put1(p, SNES(0x6C93B), pyramid);
}

/* ---- Prizes ($-7) */
enum { Heart = 0xD8, GreenRupee, BlueRupee, RedRupee, Bomb1, Bomb4, Bomb8, Magic, FullMagic, Arrow5, Arrow10, Fairy };

static const uint8_t POOL_DEFAULT[63] = {
    Heart, Heart, Heart, Heart, GreenRupee, Heart, Heart, GreenRupee,
    BlueRupee, GreenRupee, BlueRupee, RedRupee, BlueRupee, GreenRupee, BlueRupee, BlueRupee,
    FullMagic, Magic, Magic, BlueRupee, FullMagic, Magic, Heart, Magic,
    Bomb1, Bomb1, Bomb1, Bomb4, Bomb1, Bomb1, Bomb8, Bomb1,
    Arrow5, Heart, Arrow5, Arrow10, Arrow5, Heart, Arrow5, Arrow10,
    Magic, GreenRupee, Heart, Arrow5, Magic, Bomb1, GreenRupee, Heart,
    Heart, Fairy, FullMagic, RedRupee, Bomb8, Heart, RedRupee, Arrow10,
    GreenRupee, BlueRupee, RedRupee, GreenRupee, RedRupee, GreenRupee, RedRupee,
};
static const uint8_t POOL_EASY[63] = {
    Heart, Fairy, FullMagic, RedRupee, Bomb8, Heart, RedRupee, Arrow10, Heart, Fairy, FullMagic, RedRupee, Bomb8, Heart, RedRupee, Arrow10,
    Heart, Fairy, FullMagic, RedRupee, Bomb8, Heart, RedRupee, Arrow10, Heart, Fairy, FullMagic, RedRupee, Bomb8, Heart, RedRupee, Arrow10,
    Heart, Fairy, FullMagic, RedRupee, Bomb8, Heart, RedRupee, Arrow10, Heart, Fairy, FullMagic, RedRupee, Bomb8, Heart, RedRupee, Arrow10,
    Heart, Fairy, FullMagic, RedRupee, Bomb8, Heart, RedRupee, Arrow10,
    Bomb4, Arrow10, FullMagic, RedRupee, RedRupee, Fairy, RedRupee,
};
static const uint8_t POOL_HARD[63] = {
    Heart, Heart, BlueRupee, BlueRupee, GreenRupee, BlueRupee, BlueRupee, GreenRupee,
    BlueRupee, GreenRupee, BlueRupee, RedRupee, BlueRupee, GreenRupee, BlueRupee, BlueRupee,
    Magic, Magic, Magic, BlueRupee, Magic, Magic, Heart, Magic,
    Bomb1, Bomb1, Bomb1, Bomb1, Bomb1, Bomb1, Bomb4, Bomb1,
    Arrow5, Heart, Arrow5, Arrow5, Arrow5, BlueRupee, Arrow5, Arrow5,
    Magic, GreenRupee, Heart, Arrow5, Magic, Bomb1, GreenRupee, BlueRupee,
    Heart, RedRupee, Magic, RedRupee, Bomb4, BlueRupee, RedRupee, Arrow5,
    GreenRupee, BlueRupee, RedRupee, GreenRupee, RedRupee, GreenRupee, RedRupee,
};

typedef struct { int offset; int n; uint8_t bytes[13]; } PrizePack;
#define OFS 0xDB632
static const PrizePack PRIZE_PACKS[] = {
    { 0x6888D, 1, { 0x00 } }, { 0x688A8, 1, { 0x00 } }, { 0x68967, 2, { 0x00, 0x00 } }, { 0x69125, 2, { 0x00, 0x00 } },
    { OFS + 0x01, 1, { 0x90 } }, { OFS + 0x08, 1, { 0x00 } }, { OFS + 0x0A, 1, { 0x00 } }, { OFS + 0x0D, 2, { 0x80, 0x90 } },
    { OFS + 0x11, 3, { 0x90, 0x90, 0x00 } }, { OFS + 0x18, 2, { 0x90, 0x90 } }, { OFS + 0x20, 1, { 0x00 } },
    { OFS + 0x22, 3, { 0x80, 0x00, 0x00 } },
    { OFS + 0x41, 13, { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x10, 0x90, 0x90, 0x80 } },
    { OFS + 0x4F, 1, { 0x80 } }, { OFS + 0x51, 1, { 0x80 } }, { OFS + 0x55, 2, { 0x00, 0x00 } }, { OFS + 0x58, 1, { 0x90 } },
    { OFS + 0x64, 1, { 0x80 } }, { OFS + 0x6A, 2, { 0x90, 0x90 } }, { OFS + 0x6D, 2, { 0x80, 0x80 } }, { OFS + 0x71, 1, { 0x80 } },
    { OFS + 0x7C, 1, { 0x90 } }, { OFS + 0x81, 1, { 0xC0 } }, { OFS + 0x83, 4, { 0x10, 0x10, 0x10, 0x00 } },
    { OFS + 0x8B, 1, { 0x10 } }, { OFS + 0x8E, 2, { 0x00, 0x00 } }, { OFS + 0x91, 1, { 0x10 } }, { OFS + 0x99, 1, { 0x10 } },
    { OFS + 0x9B, 1, { 0x10 } }, { OFS + 0xA5, 6, { 0x10, 0x10, 0x10, 0x80, 0x80, 0x80 } }, { OFS + 0xC7, 1, { 0x10 } },
    { OFS + 0xC9, 1, { 0x10 } }, { OFS + 0xD0, 1, { 0x10 } }, { OFS + 0xD3, 1, { 0x00 } },
};

static void zelda_prizes(P *p)
{
    p->cls = "ZeldaPrizesPatch";
    uint8_t pool[63];
    switch (p->cfg->zelda_drops) {
    case 1: memcpy(pool, POOL_DEFAULT, 63); break;
    case 2: memcpy(pool, POOL_EASY, 63); break;
    case 3: memcpy(pool, POOL_HARD, 63); break;
    default: {
        memcpy(pool, POOL_DEFAULT, 63);
        int n = 63;
        while ((n -= 1) > 0) {
            int k = rnd_next(p, n + 1);
            uint8_t t = pool[n]; pool[n] = pool[k]; pool[k] = t;
        }
    }
    }
    int at = 0;
    const SmzPlando *pl = p->cfg->plando;
    /* a plando list replaces the drops; a short one is completed from the pool */
    uint8_t drops[56];
    int have = pl ? pl->n_enemy_drops : 0;
    if (have > 56) have = 56;
    memcpy(drops, pl ? pl->enemy_drops : pool, have);
    if (have < 56) { memcpy(drops + have, pool + at, 56 - have); at += 56 - have; }
    put(p, SNES(0x6FA78), drops, 56);
    uint8_t trees[3];
    have = pl ? pl->n_tree_pulls : 0;
    if (have > 3) have = 3;
    memcpy(trees, pl ? pl->tree_pulls : pool, have);
    if (have < 3) { memcpy(trees + have, pool + at, 3 - have); at += 3 - have; }
    put(p, SNES(0x1DFBD4), trees, 3);
    static const int single_at[4] = { 0x6A9C8, 0x6A9C4, 0x6F993, 0x1D82CC };
    for (int k = 0; k < 4; k++) {
        int v = pl ? (k == 0 ? pl->crab_base : k == 1 ? pl->crab_eight : k == 2 ? pl->stun : pl->fish) : -1;
        if (v < 0) v = pool[at++];
        put1(p, SNES(single_at[k]), v);
    }

    const int npacks = (int)(sizeof PRIZE_PACKS / sizeof PRIZE_PACKS[0]);
    int total = 0;
    for (int i = 0; i < npacks; i++) total += PRIZE_PACKS[i].n;
    uint8_t rnd_bytes[256];
    {
        int g[7], gn = 7;
        for (int i = 0; i < 7; i++) g[i] = i;
        int x = 7 * total, out = 0;
        while (x > 0) {
            int r = rnd_next(p, x);
            int k = r / 7;
            int v;
            if (k < gn) { v = g[k]; memmove(&g[k], &g[k + 1], (gn - k - 1) * sizeof(int)); gn--; }
            else v = r % 7;
            rnd_bytes[out++] = (uint8_t)(v + 1);
            x -= 7;
        }
    }
    uint8_t out_bytes[40][13];
    int cursor = 0;
    for (int i = 0; i < npacks; i++) {
        for (int j = 0; j < PRIZE_PACKS[i].n; j++) out_bytes[i][j] = PRIZE_PACKS[i].bytes[j] | rnd_bytes[cursor++];
        put(p, SNES(PRIZE_PACKS[i].offset), out_bytes[i], PRIZE_PACKS[i].n);
    }
    /* Popo2 is copied to Popo */
    for (int i = 0; i < npacks; i++)
        if (PRIZE_PACKS[i].offset == OFS + 0x4F) put(p, SNES(OFS + 0x4E), out_bytes[i], PRIZE_PACKS[i].n);
    static const uint8_t probability[7] = { 1, 1, 1, 1, 1, 1, 1 };
    put(p, SNES(0x6FA62), probability, 7);
}

static void text_emit(void *user, uint32_t addr, const uint8_t *d, int n)
{
    P *p = user;
    put(p, snes(p, addr), d, n);
}

static void zelda_texts(P *p)
{
    p->cls = "ZeldaTextsPatch";
    static SmzHints hints;
    hints.n = 0;
    if (p->cfg->hint_tiles > 0) smz_hints_generate(p->seed, p->cfg, p->cfg->hint_seed, p->cfg->hint_tiles, &hints);
    SmzTextOverrides ov;
    if (p->cfg->plando) smz_plando_text(p->cfg->plando, &ov);
    if (smz_text_patches_ex(p->seed, p->s, &hints, p->cfg->plando ? &ov : NULL, text_emit, p)) p->err = 1;
}

/* ---- Small fixed-shape patches */
static void etecoons_jingle(P *p)
{
    int j = p->cfg->etecoons_jingle;
    if (j <= 0 || j > SMZ_JINGLE_COUNT) j = (int)((uint32_t)p->cfg->hint_seed % SMZ_JINGLE_COUNT) + 1;
    p->cls = "EtecoonsJinglePatch";
    for (int i = 0; i < SMZ_JINGLES[j - 1].n; i++) {
        const SmzJinglePatch *g = &SMZ_JINGLES[j - 1].patches[i];
        put(p, g->offset, g->data, g->len);
    }
}

static void simple_patches(P *p)
{
    const SmzPatchCfg *c = p->cfg;
    p->cls = "LowHealthPatch";
    put1(p, 0x400033, c->low_health_beep);
    if (c->disable_low_energy_beep) {
        put1(p, SNES(0x90EA9B), 0x80); put1(p, SNES(0x90F337), 0x80); put1(p, SNES(0x91E6D5), 0x80);
    }
    p->cls = "NoBozoSoftlock";
    put1(p, SNES(0x84BA54), c->no_bozo_softlock ? 0x28 : 0x50);
    p->cls = "SaveAndQuitFromBossRoomPatch";
    put1(p, SNES(0x308042), 1);
    p->cls = "InfiniteSpaceJumpPatch";
    if (c->infinite_space_jump) {
        static const uint8_t b[2] = { 0x80, 0x0D };
        put(p, 0x82493 + (0x82493 / 0x8000 + 1) * 0x8000, b, 2);
    }
    p->cls = "MetroidAutoSavePatch";
    put2(p, SNES(0xF4700C), c->metroid_auto_save ? 1 : 0);

    p->cls = "MenuSpeedPatch";
    static const uint8_t speed[4] = { 0x04, 0x08, 0x10, 0xE8 };
    int instant = c->menu_speed == 3;
    put1(p, SNES(0x30804B), 1);
    put1(p, SNES(0x308048), speed[c->menu_speed]);
    put1(p, SNES(0x0DDD9A), instant ? 0x20 : 0x11);
    put1(p, SNES(0x0DDF2A), instant ? 0x20 : 0x12);
    put1(p, SNES(0x0DE0E9), instant ? 0x20 : 0x12);

    p->cls = "MetroidFastPauseMenuPatch";
    if (c->metroid_fast_pause_menu) {
        static const uint32_t a[6] = { 0x828D30, 0x8291CB, 0x8291EE, 0x82937D, 0x82A5CA, 0x90EA6F };
        for (int i = 0; i < 6; i++) put1(p, SNES(a[i]), 0);
    }

    p->cls = "FlashRemovalPatch";
    if (c->disable_flashing) {
        #define ZO(x) SNES((((x) / 0x8000) + 1) * 0x8000 + (x))
        put1(p, ZO(0x17E07), 0x06);
        static const uint8_t b[6] = { 0xD0, 0x03, 0xA9, 0x40, 0x29, 0x60 };
        put(p, ZO(0x17EAB), b, 6);
        put1(p, ZO(0x123FE), 0x72);
        static const uint8_t c1[2] = { 0x80, 0xAC - 0x7B };
        put(p, ZO(0x3FA7B), c1, 2);
        put1(p, ZO(0x3FAB6), 0x80);
        put1(p, ZO(0x3FAC2), 0x80);
        #undef ZO
    }

    p->cls = "HeartColorPatch";
    static const uint8_t hud[4] = { 0x24, 0x28, 0x3C, 0x2C };
    static const uint8_t file_select[4][2] = { { 0x18, 0x00 }, { 0xBC, 0x02 }, { 0x04, 0x17 }, { 0xC9, 0x69 } };
    for (int i = 0; i < 20; i += 2) put1(p, SNES(0xDFA1E + i), hud[c->heart_color]);
    put(p, SNES(0x1BD6AA), file_select[0], 2);

    p->cls = "MetroidControlsPatch";
    static const uint32_t ca[7] = { 0x81B331, 0x81B325, 0x81B32B, 0x81B33D, 0x81B337, 0x81B343, 0x81B349 };
    for (int i = 0; i < 7; i++) put2(p, SNES(ca[i]), c->controls[i]);
    put2(p, SNES(0x81EE80), c->moonwalk ? 1 : 0);
    put2(p, SNES(0x90FF50), c->item_cancel_hold ? 1 : 0);

    p->cls = "ZeldaKeysanityPatch";
    const SmzWorld *w = &p->seed->world;
    if (w->zelda_keysanity) {
        put1(p, SNES(0x40003B), 1);
        put1(p, SNES(0x400045), 0x0f);
    }
    if (w->zelda_keysanity || w->metroid_keysanity) put1(p, SNES(0x40016A), 1);

    p->cls = "StartingEquipmentPatch";
    uint8_t zelda[78];
    int keys[SMZ_ITEMOPT_GROUP_COUNT], values[SMZ_ITEMOPT_GROUP_COUNT];
    int n = smz_start_memory(p->s, zelda, keys, values, SMZ_ITEMOPT_GROUP_COUNT);
    for (int i = 0; i < n; i++) put2(p, SNES(0x81EF90 + keys[i]), values[i]);
    put(p, SNES(0x30B000), zelda, 78);
}

static void goals(P *p)
{
    p->cls = "GoalsPatch";
    const SmzSettings *s = p->s;
    if (p->cfg->pyramid_open && s->game_mode != MODE_AllDungeons) put1(p, 0x40008B, 1);
    put1(p, 0x40005E, s->gt_crystals);
    put1(p, 0x40008C, s->gt_crystals == 0 ? 1 : 0);
    put1(p, 0x40005F, s->ganon_crystals);
    put2(p, SNES(0xF47012), s->ganon_crystals);
    put2(p, SNES(0xF47008), s->tourian_bosses);
    uint8_t d[2] = { 0, (uint8_t)(s->tourian_bosses == 0 ? 4 : 1) };
    put(p, SNES(0xF4700B), d, 2);
    put2(p, SNES(0xF4700E), 0);
    if (s->game_mode == MODE_Vanilla) put1(p, SNES(0x30803E), 3);
    else if (s->game_mode == MODE_AllDungeons) put1(p, SNES(0x30803E), 2);
    else {
        put1(p, SNES(0x30803E), 5);
        put2(p, SNES(0xF47010), 1);
    }
}

static void medallions(P *p)
{
    p->cls = "MedallionPatch";
    const SmzWorld *w = &p->seed->world;
    static const uint8_t tr[3][4] = { { 0x00, 0x51, 0x10, 0x00 }, { 0x01, 0x51, 0x18, 0x00 }, { 0x02, 0x14, 0xEF, 0xC4 } };
    static const uint8_t mm[3][4] = { { 0x00, 0x51, 0x00, 0x00 }, { 0x01, 0x13, 0x9F, 0xF1 }, { 0x02, 0x51, 0x08, 0x00 } };
    int ti = w->prereq[REG_TurtleRock] == ITEM_Bombos ? 0 : w->prereq[REG_TurtleRock] == ITEM_Ether ? 1 : 2;
    int mi = w->prereq[REG_MiseryMire] == ITEM_Bombos ? 0 : w->prereq[REG_MiseryMire] == ITEM_Ether ? 1 : 2;
    int tra[4] = { SNES(0x308023), SNES(0xD020), SNES(0xD0FF), SNES(0xD1DE) };
    int mma[4] = { SNES(0x308022), SNES(0xCFF2), SNES(0xD0D1), SNES(0xD1B0) };
    for (int i = 0; i < 4; i++) put1(p, tra[i], tr[ti][i]);
    for (int i = 0; i < 4; i++) put1(p, mma[i], mm[mi][i]);
}

static void dungeon_music(P *p)
{
    p->cls = "DungeonMusicPatch";
    const SmzWorld *w = &p->seed->world;
    if (w->zelda_keysanity) return;
    static const struct { int region; int n; uint32_t a[8]; } M[] = {
        { REG_EasternPalace, 1, { 0x2D59A } },
        { REG_DesertPalace, 4, { 0x2D59B, 0x2D59C, 0x2D59D, 0x2D59E } },
        { REG_TowerOfHera, 3, { 0x2D5C5, 0x2907A, 0x28B8C } },
        { REG_PalaceOfDarkness, 1, { 0x2D5B8 } },
        { REG_SwampPalace, 1, { 0x2D5B7 } },
        { REG_SkullWoods, 8, { 0x2D5BA, 0x2D5BB, 0x2D5BC, 0x2D5BD, 0x2D608, 0x2D609, 0x2D60A, 0x2D60B } },
        { REG_ThievesTown, 1, { 0x2D5C6 } },
        { REG_IcePalace, 1, { 0x2D5BF } },
        { REG_MiseryMire, 1, { 0x2D5B9 } },
        { REG_TurtleRock, 4, { 0x2D5C7, 0x2D5A7, 0x2D5AA, 0x2D5AB } },
    };
    for (size_t i = 0; i < sizeof M / sizeof M[0]; i++) {
        int rw = w->reward_of[M[i].region];
        int pendant = rw == REWARD_PendantBlue || rw == REWARD_PendantGreen || rw == REWARD_PendantRed;
        for (int j = 0; j < M[i].n; j++) put1(p, SNES(M[i].a[j]), pendant ? 0x11 : 0x16);
    }
}

static void uncle_equipment(P *p)
{
    p->cls = "UncleEquipmentPatch";
    int item = p->seed->world.item_at[LOC_LinksUncle];
    static const uint8_t b0[6] = { 0x00, 0x00, 0xF6, 0xFF, 0x00, 0x0E }, b1[6] = { 0x00, 0x00, 0xF7, 0xFF, 0x00, 0x0E };
    static const uint8_t b2[6] = { 0x00, 0x00, 0xF6, 0xFF, 0x02, 0x0E }, b3[6] = { 0x00, 0x00, 0xF7, 0xFF, 0x02, 0x0E };
    static const uint8_t b4[6] = { 0x00, 0x00, 0xE4, 0xFF, 0x08, 0x0E };
    if (item != ITEM_ProgressiveSword) {
        put(p, SNES(0xDD263), b0, 6); put(p, SNES(0xDD26B), b0, 6); put(p, SNES(0xDD293), b0, 6); put(p, SNES(0xDD29B), b1, 6);
        put(p, SNES(0xDD2B3), b2, 6); put(p, SNES(0xDD2BB), b2, 6); put(p, SNES(0xDD2E3), b3, 6); put(p, SNES(0xDD2EB), b3, 6);
        put(p, SNES(0xDD31B), b4, 6); put(p, SNES(0xDD323), b4, 6);
    }
    if (item != ITEM_ProgressiveShield) {
        put(p, SNES(0xDD253), b0, 6); put(p, SNES(0xDD25B), b0, 6); put(p, SNES(0xDD283), b0, 6); put(p, SNES(0xDD28B), b1, 6);
        put(p, SNES(0xDD2CB), b2, 6); put(p, SNES(0xDD2FB), b3, 6); put(p, SNES(0xDD313), b4, 6);
    }
}

/* ---- Locations */
static int z3_item_id(const SmzWorld *w, int loc, const SmzLocDef *d)
{
    int t = w->item_at[loc];
    if (d->type == LTYPE_NotInDungeon || !(item_is_dungeon(t) && region_has_item(d->region, t))) return t;
    uint32_t cat = SMZ_ITEM_CATS[t];
    if (cat & ICAT_SmallKey) return ITEM_Key;
    if (cat & ICAT_BigKey) return ITEM_BigKey;
    if (cat & ICAT_Map) return ITEM_Map;
    return ITEM_Compass;
}

static void locations(P *p)
{
    p->cls = "LocationsPatch";
    const SmzWorld *w = &p->seed->world;
    for (int i = 0; i < SMZ_LOC_COUNT; i++) {
        const SmzLocDef *d = &SMZ_LOCS[i];
        int sm = SMZ_REGIONS[d->region].kind == RK_SM;
        int item_id = z3_item_id(w, d->id, d);
        if (sm) {
            int plm = 0xEFE0 + (d->type == LTYPE_Chozo ? 4 : d->type == LTYPE_Hidden ? 8 : 0);
            put2(p, SNES(d->rom), plm);
        } else {
            if (d->type == LTYPE_HeraStandingKey) put1(p, SNES(0x9E3BB), w->item_at[d->id] == ITEM_KeyTH ? 0xE4 : 0xEB);
            put1(p, SNES(d->rom), d->id - 256);
        }
        uint8_t row[8] = { 0, 0, (uint8_t)item_id, 0, 0, 0, 0, 0 };
        put(p, 0x386000 + d->id * 8, row, 8);
    }
}

/* ---- Metroid keycards */
enum { KD_Left = 0xd414, KD_Right = 0xd41a, KD_Up = 0xd420, KD_Down = 0xd426, KD_BossLeft = 0xc842 };
enum { KC_CrateriaLevel1 = 0x0000, KC_CrateriaLevel2 = 0x0100, KC_CrateriaBoss = 0x0200, KC_BrinstarLevel1 = 0x0300, KC_BrinstarLevel2 = 0x0400,
       KC_BrinstarBoss = 0x0500, KC_NorfairLevel1 = 0x0600, KC_NorfairLevel2 = 0x0700, KC_NorfairBoss = 0x0800, KC_MaridiaLevel1 = 0x0900,
       KC_MaridiaLevel2 = 0x0a00, KC_MaridiaBoss = 0x0b00, KC_WreckedShipLevel1 = 0x0c00, KC_WreckedShipBoss = 0x0d00,
       KC_LowerNorfairLevel1 = 0x0e00, KC_LowerNorfairBoss = 0x0f00 };
enum { PL_Level1 = 0xe0, PL_Level2 = 0xe1, PL_Boss = 0xe2, PL_None = 0x00, PL_Zero = 0xe3 };

static const uint16_t DOORS[][7] = {
    { 0x91F8, KD_Right, 0x2601, KC_CrateriaLevel1, PL_Level1, 0x2400, 0x0000 },
    { 0x91F8, KD_Left, 0x168E, KC_CrateriaLevel1, PL_Level1, 0x148F, 0x801E },
    { 0x948C, KD_Left, 0x062E, KC_CrateriaLevel2, PL_Level2, 0x042F, 0x8222 },
    { 0x99BD, KD_Left, 0x660E, KC_CrateriaBoss, PL_Boss, 0x640F, 0x8470 },
    { 0x9879, KD_Left, 0x062E, KC_CrateriaBoss, PL_Boss, 0x042F, 0x8420 },
    { 0x9F11, KD_Left, 0x060E, KC_BrinstarLevel1, PL_Level1, 0x040F, 0x8784 },
    { 0x9AD9, KD_Right, 0xA601, KC_BrinstarLevel2, PL_Level2, 0xA400, 0x0000 },
    { 0x9D9C, KD_Down, 0x0336, KC_BrinstarBoss, PL_Boss, 0x0234, 0x863A },
    { 0xA130, KD_Left, 0x161E, KC_BrinstarLevel2, PL_Level2, 0x141F, 0x881C },
    { 0xA0A4, KD_Left, 0x062E, KC_BrinstarLevel2, PL_Level2, 0x042F, 0x0000 },
    { 0xA56B, KD_Left, 0x161E, KC_BrinstarBoss, PL_Boss, 0x141F, 0x8A1A },
    { 0xA7DE, KD_Right, 0x3601, KC_NorfairLevel1, PL_Level1, 0x3400, 0x8B00 },
    { 0xA923, KD_Right, 0x0601, KC_NorfairLevel1, PL_Level1, 0x0400, 0x0000 },
    { 0xA788, KD_Left, 0x162E, KC_NorfairLevel2, PL_Level2, 0x142F, 0x8AEA },
    { 0xAF72, KD_Left, 0x061E, KC_NorfairLevel2, PL_Level2, 0x041F, 0x0000 },
    { 0xAEDF, KD_Down, 0x0206, KC_NorfairLevel2, PL_Level2, 0x0204, 0x0000 },
    { 0xAD5E, KD_Right, 0x0601, KC_NorfairLevel2, PL_Level2, 0x0400, 0x0000 },
    { 0xA923, KD_Up, 0x2DC6, KC_NorfairBoss, PL_Boss, 0x2EC4, 0x8B96 },
    { 0xB4AD, KD_Left, 0x160E, KC_LowerNorfairLevel1, PL_Level1, 0x140F, 0x0000 },
    { 0xAD5E, KD_Left, 0x065E, KC_LowerNorfairLevel1, PL_Level1, 0x045F, 0x0000 },
    { 0xB37A, KD_Right, 0x0601, KC_LowerNorfairBoss, PL_Boss, 0x0400, 0x8EA6 },
    { 0xD0B9, KD_Left, 0x065E, KC_MaridiaLevel1, PL_Level1, 0x045F, 0x0000 },
    { 0xD5A7, KD_Right, 0x1601, KC_MaridiaLevel1, PL_Level1, 0x1400, 0x0000 },
    { 0xD617, KD_Left, 0x063E, KC_MaridiaLevel2, PL_Level2, 0x043F, 0x0000 },
    { 0xD913, KD_Right, 0x2601, KC_MaridiaLevel2, PL_Level2, 0x2400, 0x0000 },
    { 0xD78F, KD_Right, 0x2601, KC_MaridiaBoss, PL_Boss, 0x2400, 0xC73B },
    { 0xDA2B, KD_BossLeft, 0x164E, 0x00f0, PL_None, 0x144F, 0x0000 },
    { 0x93FE, KD_Left, 0x167E, KC_WreckedShipLevel1, PL_Level1, 0x147F, 0x0000 },
    { 0x968F, KD_Left, 0x060E, KC_WreckedShipLevel1, PL_Level1, 0x040F, 0x0000 },
    { 0xCE40, KD_Left, 0x060E, KC_WreckedShipLevel1, PL_Level1, 0x040F, 0x0000 },
    { 0xCC6F, KD_Left, 0x064E, KC_WreckedShipBoss, PL_Boss, 0x044F, 0xC29D },
};

static void le16(uint8_t *d, int *n, uint16_t v) { d[(*n)++] = (uint8_t)v; d[(*n)++] = (uint8_t)(v >> 8); }

static void metroid_keysanity(P *p)
{
    p->cls = "MetroidKeysanityPatch";
    const uint16_t plaque_plm = 0xd410;
    int table = 0xf800;
    if (p->seed->world.metroid_keysanity) {
        uint16_t door_id = 0;
        for (size_t i = 0; i < sizeof DOORS / sizeof DOORS[0]; i++) {
            const uint16_t *d = DOORS[i];
            if (d[0] == 0x99BD && p->cfg->tourian_door_open) continue;
            uint16_t args = d[4] != PL_None ? (uint16_t)(door_id | d[3]) : d[3];
            uint8_t buf[8]; int n = 0;
            if (d[6] == 0) {
                le16(buf, &n, d[0]); le16(buf, &n, d[1]); le16(buf, &n, d[2]); le16(buf, &n, args);
                put(p, SNES(0x8f0000 + table), buf, n);
                table += 8;
            } else {
                le16(buf, &n, d[1]); le16(buf, &n, d[2]); le16(buf, &n, args);
                put(p, SNES(0x8f0000 + d[6]), buf, n);
                if ((d[3] == KC_BrinstarBoss && d[0] != 0x9D9C) || d[3] == KC_LowerNorfairBoss || d[3] == KC_MaridiaBoss || d[3] == KC_WreckedShipBoss) {
                    static const uint8_t gadora[12] = { 0x2F, 0xB6, 0, 0, 0, 0, 0x2F, 0xB6, 0, 0, 0, 0 };
                    put(p, SNES(0x8f0000 + d[6] + 6), gadora, 12);
                }
            }
            if (d[4] != PL_None) {
                n = 0;
                le16(buf, &n, d[0]); le16(buf, &n, plaque_plm); le16(buf, &n, d[5]); le16(buf, &n, d[4]);
                put(p, SNES(0x8f0000 + table), buf, n);
                table += 8;
            }
            door_id++;
        }
    }
    if (p->s->game_mode == MODE_Vanilla && p->cfg->plando) {
        /* plaque above the golden bosses' room with the number of bosses needed to enter Tourian */
        uint8_t buf[8]; int n = 0;
        le16(buf, &n, 0xA5ED); le16(buf, &n, plaque_plm); le16(buf, &n, 0x044F); le16(buf, &n, PL_Zero + p->s->tourian_bosses);
        put(p, SNES(0x8f0000 + table), buf, n);
        table += 8;
    }
    static const uint8_t end[8] = { 0 };
    put(p, SNES(0x8f0000 + table), end, 8);
}

int smz_apply_patches(uint8_t *rom, const SmzSeed *seed, const SmzSettings *s, const SmzPatchCfg *cfg,
                      int32_t patch_seed, SmzPatchLog log, void *user)
{
    SmzRng rnd;
    rng_init(&rnd, patch_seed);
    rng_sanitize(&rnd);
    SmzSettings eff = *s;
    smz_settings_normalize(&eff);
    P p = { rom, log, user, "", seed, &eff, cfg, &rnd, 0 };
    zelda_rewards(&p);   /* order -10 */
    metadata(&p);        /* -9 */
    digging(&p);         /* -8 */
    zelda_prizes(&p);    /* -7 */
    fairy_pond(&p);      /* -6 */
    zelda_texts(&p);     /* -5 */
    etecoons_jingle(&p);
    simple_patches(&p);
    goals(&p);
    medallions(&p);
    dungeon_music(&p);
    uncle_equipment(&p);
    locations(&p);
    metroid_keysanity(&p);
    return p.err ? -1 : 0;
}
