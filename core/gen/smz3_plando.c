#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smz3_plando.h"
#include "smz3_generate.h"
#include "smz3_world.h"
#include "smz3_world_gen.h"
#include "smz3_plando_names.h"

#define E(...) do { if (err && errcap) snprintf(err, errcap, __VA_ARGS__); return -1; } while (0)

/* names compare ignoring case; region names also ignore spaces and punctuation ("Kraid's Lair" is KraidsLair) */
static int same(const char *a, const char *b) { return !strcasecmp(a, b); }
static int same_loose(const char *a, const char *b)
{
    for (;;) {
        while (*a && !isalnum((unsigned char)*a)) a++;
        while (*b && !isalnum((unsigned char)*b)) b++;
        if (!*a || !*b) return *a == *b;
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++; b++;
    }
}

static int region_by_name(const char *name)
{
    for (int r = 0; r < REG_COUNT; r++) if (same_loose(SMZ_REGIONS[r].name, name)) return r;
    return -1;
}
static int loc_by_name(const char *name)
{
    for (int i = 0; i < SMZ_LOC_NAME_COUNT; i++) if (same(SMZ_LOC_NAMES[i].name, name)) return SMZ_LOC_NAMES[i].id;
    return -1;
}
static int reward_by_name(const char *n)
{
    static const char *const names[] = { "None", "Agahnim", "PendantGreen", "PendantRed", "CrystalBlue", "CrystalRed", "PendantBlue",
                                         "MetroidBoss", "KraidToken", "PhantoonToken", "DraygonToken", "RidleyToken" };
    for (int i = 0; i < 12; i++) if (same(names[i], n)) return i;
    return -1;
}
static int prize_by_name(const char *n)
{
    static const char *const names[] = { "Heart", "GreenRupee", "BlueRupee", "RedRupee", "Bomb1", "Bomb4", "Bomb8", "Magic", "FullMagic", "Arrow5", "Arrow10", "Fairy" };
    for (int i = 0; i < 12; i++) if (same(names[i], n)) return 0xD8 + i;
    return -1;
}
static int enum_by_name(const char *n, const char *const *names, int count)
{
    for (int i = 0; i < count; i++) if (same(names[i], n)) return i;
    return -1;
}
static int item_by_name(const char *n)
{
    for (int i = 0; n[i]; i++) if (!isalnum((unsigned char)n[i])) return -1;
    return smz_item_by_name(n);
}
static int truth(const char *v) { return same(v, "true") || same(v, "yes"); }

/* trims, drops one pair of quotes */
static char *clean(char *s)
{
    while (*s == ' ') s++;
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = 0;
    if (n >= 2 && ((s[0] == '"' && s[n - 1] == '"') || (s[0] == '\'' && s[n - 1] == '\''))) { s[n - 1] = 0; s++; }
    return s;
}

int32_t smz_seed_number(const char *seed)
{
    char buf[64];
    snprintf(buf, sizeof buf, "%s", seed);
    char *s = clean(buf);
    char *end;
    long long v = strtoll(s, &end, 10);
    if (*s && !*end && v >= -2147483648LL && v <= 2147483647LL) return (int32_t)v;
    v = strtoll(s, &end, 16);
    if (*s && !*end && v >= 0 && v <= 0xFFFFFFFFLL) return (int32_t)(uint32_t)v;
    uint32_t h = 2166136261u;
    for (const unsigned char *c = (const unsigned char *)s; *c; c++) { h ^= *c; h *= 16777619u; }
    return (int32_t)h;
}

enum { S_NONE, S_LOGIC, S_ITEMS, S_REWARDS, S_MEDALLIONS, S_PRIZES, S_TEXT, S_OTHER };

static const char *const TEXT_KEYS[SMZ_TXT_COUNT] = {
    "GanonIntro", "GanonIntroGoalsNotMet", "BlindIntro", "TriforceRoom", "KingZora", "BottleMerchant", "GanonSilversHint",
    "GanonsTowerGoalSign", "GanonGoalSign", "SahasrahlaReveal", "BombShopReveal", "TavernMan", "MasterSwordPedestal", "EtherTablet",
    "BombosTablet", "HintTileEasternPalace", "HintTileTowerOfHeraFloor4", "HintTileSpectacleRock", "HintTileSwampEntrance",
    "HintTileThievesTownUpstairs", "HintTileMiseryMire", "HintTilePalaceOfDarkness", "HintTileDesertBonkTorchRoom", "HintTileCastleTower",
    "HintTileIceLargeRoom", "HintTileTurtleRock", "HintTileIceEntrance", "HintTileIceStalfosKnightsRoom", "HintTileTowerOfHeraEntrance",
    "HintTileSouthEastDarkworldCave" };

void smz_plando_text(const SmzPlando *p, SmzTextOverrides *ov)
{
    for (int i = 0; i < SMZ_TXT_COUNT; i++) ov->v[i] = p->text[i][0] ? p->text[i] : NULL;
}

/* A YAML scalar's text: double quoted values take \n, \t, \" and \\; single quoted ones '' for a quote */
static void unquote(const char *raw, char *out, size_t cap)
{
    size_t n = strlen(raw), o = 0;
    while (n && (raw[n - 1] == ' ' || raw[n - 1] == '\r')) n--;
    while (*raw == ' ') { raw++; n--; }
    if (n >= 2 && raw[0] == '"' && raw[n - 1] == '"') {
        for (size_t i = 1; i + 1 < n && o + 1 < cap; i++) {
            char c = raw[i];
            if (c == '\\' && i + 2 < n) {
                char e = raw[++i];
                c = e == 'n' ? '\n' : e == 't' ? '\t' : e;
            }
            out[o++] = c;
        }
    } else if (n >= 2 && raw[0] == '\'' && raw[n - 1] == '\'') {
        for (size_t i = 1; i + 1 < n && o + 1 < cap; i++) {
            if (raw[i] == '\'' && raw[i + 1] == '\'') i++;
            out[o++] = raw[i];
        }
    } else {
        for (size_t i = 0; i < n && o + 1 < cap; i++) out[o++] = raw[i];
    }
    out[o] = 0;
}

int smz_plando_parse(SmzPlando *p, const char *text, size_t len, char *err, size_t errcap)
{
    static const char *const MODES[] = { "Vanilla", "AllDungeons", "SpazerHunt" };
    static const char *const KEYS[] = { "None", "Zelda", "SuperMetroid", "Both" };
    SmzSettings d;
    smz_settings_defaults(&d);
    memset(p, 0, sizeof *p);
    p->cfg = d.cfg;
    p->gt_crystals = p->ganon_crystals = 7;
    p->tourian_bosses = 4;
    p->spazers_required = 30;
    memset(p->loc_item, -1, sizeof p->loc_item);
    memset(p->region_reward, -1, sizeof p->region_reward);
    memset(p->region_prereq, -1, sizeof p->region_prereq);
    p->crab_base = p->crab_eight = p->stun = p->fish = -1;
    p->waterfall_trade = p->pyramid_trade = -1;

    int section = S_NONE;
    char listkey[48] = "";
    size_t pos = 0;
    while (pos < len) {
        char line[512];
        size_t n = 0;
        while (pos < len && text[pos] != '\n') { if (n < sizeof line - 1) line[n++] = text[pos]; pos++; }
        pos++;
        line[n] = 0;
        char *s = line;
        int indent = 0;
        while (*s == ' ') { s++; indent++; }
        if (!*s || *s == '#' || *s == '\r') continue;
        if (!strncmp(s, "- ", 2) || !strcmp(s, "-")) {
            char *v = clean(s + 1);
            if (section != S_PRIZES) continue;
            int prize = prize_by_name(v);
            if (prize < 0) E("'%s' is not a valid drop prize.", v);
            if (!strcmp(listkey, "EnemyDrops")) { if (p->n_enemy_drops < 64) p->enemy_drops[p->n_enemy_drops++] = (uint8_t)prize; }
            else if (!strcmp(listkey, "TreePulls")) { if (p->n_tree_pulls < 8) p->tree_pulls[p->n_tree_pulls++] = (uint8_t)prize; }
            continue;
        }
        /* key: value. Keys of the locations hold " - " but never ": ", so the last colon splits them */
        char *colon = NULL;
        for (char *c = s; *c; c++)
            if (*c == ':' && (c[1] == ' ' || c[1] == 0 || c[1] == '\r')) { colon = c; if (section == S_TEXT && indent > 0) break; }   /* texts may hold ": " */
        if (!colon) continue;
        *colon = 0;
        const char *rawval = colon + 1;           /* before clean(): quotes and spaces matter for texts */
        char rawcopy[512];
        snprintf(rawcopy, sizeof rawcopy, "%s", rawval);
        char *key = clean(s), *val = clean(colon + 1);
        rawval = rawcopy;
        if (indent == 0) {
            section = S_OTHER;
            if (!strcmp(key, "Logic")) section = S_LOGIC;
            else if (!strcmp(key, "Items")) section = S_ITEMS;
            else if (!strcmp(key, "Rewards")) section = S_REWARDS;
            else if (!strcmp(key, "Medallions")) section = S_MEDALLIONS;
            else if (!strcmp(key, "ZeldaPrizes")) section = S_PRIZES;
            else if (!strcmp(key, "Text")) section = S_TEXT;
            else if (!strcmp(key, "Seed")) { snprintf(p->seed, sizeof p->seed, "%s", val); section = S_NONE; }
            else if (!strcmp(key, "GameModeType")) { int m = enum_by_name(val, MODES, 3); if (m < 0) E("Unknown game mode '%s'.", val); p->game_mode = m; section = S_NONE; }
            else if (!strcmp(key, "GanonsTowerCrystalCount")) { p->gt_crystals = atoi(val); section = S_NONE; }
            else if (!strcmp(key, "GanonCrystalCount")) { p->ganon_crystals = atoi(val); section = S_NONE; }
            else if (!strcmp(key, "TourianBossCount")) { p->tourian_bosses = atoi(val); section = S_NONE; }
            else if (!strcmp(key, "SpazersRequired")) { p->spazers_required = atoi(val); section = S_NONE; }
            else if (!strcmp(key, "LiftOffOnGoalCompletion")) { p->liftoff_on_goal = truth(val); section = S_NONE; }
            else if (!strcmp(key, "KeysanityMode")) { int k = enum_by_name(val, KEYS, 4); if (k < 0) E("Unknown keysanity mode '%s'.", val); p->keysanity = k; section = S_NONE; }
            else if (!strcmp(key, "SkipTourianBossDoor")) { p->skip_tourian_door = truth(val); section = S_NONE; }
            else if (!strcmp(key, "OpenPyramid")) { p->open_pyramid = truth(val); section = S_NONE; }
            else if (!strcmp(key, "WaterfallFairyTrade") || !strcmp(key, "PyramidFairyTrade")) {
                int t = item_by_name(val);
                if (!strcmp(key, "WaterfallFairyTrade")) p->waterfall_trade = t; else p->pyramid_trade = t;
                section = S_NONE;
            }
            continue;
        }
        switch (section) {
        case S_LOGIC:
            if (!strcmp(key, "WallJumpDifficulty")) {
                static const char *const W[] = { "None", "Easy", "Medium", "Hard", "Insane" };
                int w = enum_by_name(val, W, 5);
                if (w < 0) E("Unknown wall jump difficulty '%s'.", val);
                p->cfg.WallJumpDifficulty = w;
            } else smz_cfg_set(&p->cfg, key, truth(val));
            break;
        case S_ITEMS: {
            int loc = loc_by_name(key);
            if (loc < 0) E("Could not find a location with the specified name.\nName: '%s'", key);
            int item = item_by_name(val);
            if (item < 0) E("'%s' is not a valid item type (location '%s').", val, key);
            p->loc_item[loc] = (int16_t)item;
            break;
        }
        case S_REWARDS: {
            int r = region_by_name(key);
            if (r < 0) E("Could not find a region with the specified name.\nName: '%s'", key);
            if (!SMZ_REGIONS[r].has_reward) E("%s is configured with a reward but that region cannot be configured with rewards.", key);
            int rw = reward_by_name(val);
            if (rw < 0) E("'%s' is not a valid reward type.", val);
            p->region_reward[r] = (int16_t)rw;
            break;
        }
        case S_MEDALLIONS: {
            int r = region_by_name(key);
            if (r < 0) E("Could not find a region with the specified name.\nName: '%s'", key);
            if (r != REG_MiseryMire && r != REG_TurtleRock) E("%s is configured with a medallion but that region cannot be configured with medallions.", key);
            int m = item_by_name(val);
            if (m != ITEM_Bombos && m != ITEM_Ether && m != ITEM_Quake) E("%s is not a valid type of medallion.", val);
            p->region_prereq[r] = (int16_t)m;
            break;
        }
        case S_PRIZES: {
            snprintf(listkey, sizeof listkey, "%s", key);
            if (!*val) break;                       /* a list follows */
            int prize = prize_by_name(val);
            if (prize < 0) E("'%s' is not a valid drop prize.", val);
            if (!strcmp(key, "CrabBaseDrop")) p->crab_base = prize;
            else if (!strcmp(key, "CrabEightDrop")) p->crab_eight = prize;
            else if (!strcmp(key, "StunPrize")) p->stun = prize;
            else if (!strcmp(key, "FishPrize")) p->fish = prize;
            break;
        }
        case S_TEXT:
            for (int k = 0; k < SMZ_TXT_COUNT; k++)
                if (!strcmp(key, TEXT_KEYS[k])) { unquote(rawval, p->text[k], sizeof p->text[k]); break; }
            break;
        default: break;
        }
    }
    return 0;
}

void smz_plando_settings(const SmzPlando *p, SmzSettings *s)
{
    smz_settings_defaults(s);
    s->seed = smz_seed_number(p->seed);
    s->keysanity = p->keysanity;
    s->ganon_crystals = p->ganon_crystals;
    s->gt_crystals = p->gt_crystals;
    s->tourian_bosses = p->tourian_bosses;
    s->game_mode = p->game_mode;
    s->spazers_required = p->spazers_required;
    s->cfg = p->cfg;
    smz_settings_normalize(s);
}

int smz_plando_seed(SmzSeed *out, const SmzPlando *p, char *err, size_t errcap)
{
    SmzSettings s;
    smz_plando_settings(p, &s);
    smz_seed_init(out, &s);
    SmzWorld *w = &out->world;
    rng_init(&out->rng, s.seed);
    for (int i = 0; i < SMZ_LOC_COUNT; i++) {
        int id = SMZ_LOCS[i].id;
        if (p->loc_item[id] < 0) {
            const char *name = "?";
            for (int k = 0; k < SMZ_LOC_NAME_COUNT; k++) if (SMZ_LOC_NAMES[k].id == id) name = SMZ_LOC_NAMES[k].name;
            E("Not all locations have been filled. Missing:\n%s", name);
        }
        w->item_at[id] = (uint8_t)p->loc_item[id];
    }
    for (int r = 0; r < REG_COUNT; r++) {
        if (p->region_reward[r] >= 0) w->reward_of[r] = (uint8_t)p->region_reward[r];
        else if (SMZ_REGIONS[r].has_reward && SMZ_REGIONS[r].shuffled == 1) E("Not all dungeons have had their rewards set. Missing:\n%s", SMZ_REGIONS[r].name);
        if (p->region_prereq[r] >= 0) w->prereq[r] = p->region_prereq[r];
        else if (r == REG_MiseryMire || r == REG_TurtleRock) E("Not all dungeons have had their medallions set. Missing:\n%s", SMZ_REGIONS[r].name);
    }
    return 0;
}
