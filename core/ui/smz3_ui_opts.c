#include <stdio.h>
#include <string.h>
#include "smz3_ui_gen.h"
#include "smz3_itemopts.h"
#include "smz3_ui_items.h"
#include "smz3_plando.h"
#include "smz3_jingles_data.h"
#include "smz3_runlogic.h"

static const char *const N_KEYS[] = {"None", "Both games", "Zelda only", "Metroid only", "Random"};
static const char *const N_PLACE[] = {"Anywhere", "Dungeons + Metroid", "Crystal dungeons + Metroid", "Vanilla game", "Opposite game"};
static const char *const N_DOOR[] = {"Closed", "Open", "Random"};
static const char *const N_MAP[] = {"Whole map", "Discovered only"};
static const char *const N_FILTER[] = {"None (sharp)", "Smooth", "CRT", "LCD"};
static const char *const N_SHAPE[] = {"Original (4:3)", "Wide (16:9)", "Square pixels (8:7)"};
static const char *const N_HELP[] = {"All helps", "Limited helps", "No helps"};
static const char *const N_WALL[] = {"Easy", "Normal", "Medium", "Hard"};
static const char *const N_MODE[] = {"Vanilla", "All Dungeons", "Spazer Hunt"};
/* "Random" then the jingles in SMZ_JINGLES order (tools/gen_jingles.py); the static assert below keeps the lists in step. */
static const char *const N_JINGLE[] = {"Random", "Vanilla", "Blue Badger", "Battletoads Win", "Vampire Killer", "FF Victory", "KQ5 Town", "Kirby Win",
    "Buffed Kirby Win", "Mega Man Boss Selected", "SNAAAKE", "Pizza Time", "Shadowgate", "Sonic 2 Bonus", "SMB1", "Theme of Super Metroid",
    "YI Castle", "Manbo's Mambo", "Saria's Song", "Zelda Secret"};
_Static_assert(sizeof N_JINGLE / sizeof N_JINGLE[0] == SMZ_JINGLE_COUNT + 1, "jingle names");

/* Order matters: ui_settings reads the first block by index (see enum below). */
enum { O_SEED, O_KEYS, O_PLACE, O_GANON, O_GT, O_TOURIAN, O_LINK, O_SAMUS, O_MAP, O_DARK, O_FILTER, O_SHAPE, O_HELP, O_HELPS, O_PYRAMID, O_DOOR, O_WALL, O_MODE, O_PLANDO, O_JINGLE, O_SPAZ_POOL, O_SPAZ_REQ, O_NERF, O_SAVES, O_MORPH, O_MUSIC, O_REACH, O_FKEYS, O_ITEM0, O_LOGIC0 = O_ITEM0 + SMZ_ITEMOPT_GROUP_COUNT };

#define B(k, l, t) {k, l, t, OPT_BOOL, LEVEL_RUN, 0, 1, 0, 0}
#define Q(k, l, t) {k, l, t, OPT_BOOL, LEVEL_RUN, 0, 1, 0, 0}
const UiOptDef UI_OPTS[] = {
    {"Seed", "Seed", "0 picks a random seed. Left/right change it by 1, A adds 1000.", OPT_SEED, LEVEL_RUN, 0, 2147483646, 0, 0},
    {"Keysanity", "Keysanity", "Shuffles keys, maps, compasses and keycards.", OPT_ENUM, LEVEL_RUN, 0, 4, 0, N_KEYS},
    {"ItemPlacement", "Item placement", "Where each game's items may end up.", OPT_ENUM, LEVEL_RUN, 0, 4, 0, N_PLACE},
    {"GanonCrystals", "Crystals for Ganon", "Crystals needed to damage Ganon.", OPT_INT, LEVEL_RUN, 0, 7, 7, 0},
    {"GtCrystals", "Crystals for Tower", "Crystals needed to open Ganon's Tower.", OPT_INT, LEVEL_RUN, 0, 7, 7, 0},
    {"TourianBosses", "Bosses for Tourian", "Metroid bosses to beat before Tourian opens.", OPT_INT, LEVEL_RUN, 0, 4, 4, 0},
    {"LinkSprite", "Link sprite", "Sprite for A Link to the Past.", OPT_SPRITE, LEVEL_RUN, 0, 0, 0, 0},
    {"SamusSprite", "Samus sprite", "Sprite for Super Metroid.", OPT_SPRITE, LEVEL_RUN, 0, 0, 0, 0},
    {"MapMode", "Tracker map", "Whole map shows every check; discovered only shows what you have found.", OPT_ENUM, LEVEL_RUN, 0, 1, 0, N_MAP},
    {"LightDarkRooms", "Light dark rooms", "Light every dark room of A Link to the Past.", OPT_BOOL, LEVEL_RUN, 0, 1, 0, 0},
    {"ScreenFilter", "Screen filter", "Video filter of the game screen.", OPT_ENUM, LEVEL_RUN, 0, 3, 0, N_FILTER},
    {"ScreenShape", "Screen shape", "Wide stretches the picture to 16:9.", OPT_ENUM, LEVEL_RUN, 0, 2, 0, N_SHAPE},
    {"HelpMode", "Helps", "All: tiles, map dots, free hints. Limited: ask, a few times. None: no help.", OPT_ENUM, LEVEL_RUN, 0, 2, 0, N_HELP},
    {"HelpCount", "Helps available", "Limited mode: how many times you can ask.", OPT_INT, LEVEL_RUN, 1, 15, 3, 0},
    {"PyramidHole", "Pyramid hole", "Closed opens after Ganon's Tower is completed.", OPT_ENUM, LEVEL_RUN, 0, 2, 0, N_DOOR},
    {"TourianBossDoor", "Tourian boss door", "Closed needs the Crateria boss keycard.", OPT_ENUM, LEVEL_RUN, 0, 2, 0, N_DOOR},
    {"WallJumpDifficulty", "Wall jump logic", "How hard the wall jumps in logic may be. Easy never asks for one.", OPT_ENUM, LEVEL_RUN, 0, 3, 2, N_WALL},
    {"GameMode", "Game mode", "Vanilla, All Dungeons or Spazer Hunt (collect spazers).", OPT_ENUM, LEVEL_RUN, 0, 2, 0, N_MODE},
    {"Plando", "Plando", "A plando file from the SMZ3 plando folder replaces the generated seed.", OPT_SPRITE, LEVEL_RUN, 0, 0, 0, 0},
    {"EtecoonsJingle", "Etecoons jingle", "The jingle played when you find the Etecoons. Random picks one from the seed.", OPT_ENUM, LEVEL_RUN, 0, SMZ_JINGLE_COUNT, 0, N_JINGLE},
    {"SpazersInPool", "Spazers in pool", "Spazer Hunt: how many spazers are placed.", OPT_INT, LEVEL_RUN, 1, 60, 40, 0},
    {"SpazersRequired", "Spazers required", "Spazer Hunt: how many you need to beat the game.", OPT_INT, LEVEL_RUN, 1, 60, 30, 0},
    {"NerfedCharge", "Nerfed charge", "Charge the beam from the start, at a third of the damage.", OPT_BOOL, LEVEL_RUN, 0, 1, 0, 0},
    {"SaveStates", "Save states", "Allow save states in this run. Off keeps the run honest: only the game's own saves.", OPT_BOOL, LEVEL_RUN, 0, 1, 0, 0},
    {"QuickMorph", "Quick morph", "Super Metroid: the aim-down button turns Samus into a ball; aim up and down share one button.", OPT_BOOL, LEVEL_RUN, 0, 1, 0, 0},
    {"MusicPackage", "Music package", "Original, random, or a pack from /sdcard/SMZ3/music (MSU-1). Android only.", OPT_SPRITE, LEVEL_RUN, 0, 1, 0, 0},
    {"ShowReachable", "Reachable checks", "The tracker map marks the checks you can reach now.", OPT_BOOL, LEVEL_RUN, 0, 1, 0, 0},
    {"ShowFixedKeys", "Fixed keys", "The dungeon maps mark the keys under pots and on enemies until you take them.", OPT_BOOL, LEVEL_RUN, 0, 1, 0, 0},
    ITEM_OPT_DEFS
    Q("PreventScrewAttackSoftLock", "No Screw Attack soft locks", "Screw Attack is not expected without the morph ball."),
    Q("PreventFivePowerBombSeed", "No five power bomb seeds", "Expects at least two power bomb upgrades."),
    Q("LeftSandPitRequiresSpringBall", "Left sand pit needs Spring Ball", "Spring ball is expected for the left sand pit."),
    Q("LaunchPadRequiresIceBeam", "Launchpad needs Ice Beam", "Ice beam is expected to freeze the boyons."),
    Q("WaterwayNeedsGravitySuit", "Waterway needs Gravity Suit", "Gravity suit is expected for the Waterway."),
    Q("EasyEastCrateriaSkyItem", "Easy East Crateria sky item", "Space jump or speed booster is expected."),
    Q("EasyBlueBrinstarTop", "Easy Blue Brinstar top", "Space jump or gravity suit is expected."),
    Q("KholdstareNeedsCaneOfSomaria", "Kholdstare needs Somaria", "Cane of Somaria is expected."),
    Q("ZoraNeedsRupeeItems", "Zora needs rupee items", "Rupee items are expected for Zora."),
    Q("QuarterMagic", "Quarter magic", "Assume quarter magic in logic."),
    Q("PreventLowResourceDarkWorld", "No low resource Dark World", "Avoid starting the Dark World without resources."),
    B("FireRodDarkRooms", "Fire rod dark rooms", "Fire rod may light dark rooms."),
    B("InfiniteBombJump", "Infinite bomb jump", "Allow infinite bomb jumps."),
    B("ParlorSpeedBooster", "Parlor speed booster break in", "Allow the parlor speed booster break in."),
    B("MoatSpeedBooster", "Moat speed booster fly by", "Allow the moat speed booster fly by."),
    B("MockBall", "Mockball", "Allow mockball."),
    B("SwordOnlyDarkRooms", "Sword only dark rooms", "Dark rooms may be crossed with only the sword."),
    B("LightWorldSouthFakeFlippers", "Light World south fake flippers", "Allow fake flippers in the south."),
};
const int UI_OPT_COUNT = sizeof UI_OPTS / sizeof UI_OPTS[0];

/* The Create run tabs: the run itself, what only touches one game, and how it is played. */
const char *const UI_TABS[LEVEL_ITEMS][24] = {
    {"Seed", "GameMode", "Keysanity", "ItemPlacement", "HelpMode", "HelpCount", "MapMode", "Plando", NULL},
    {"GanonCrystals", "GtCrystals", "PyramidHole", "LightDarkRooms", "FireRodDarkRooms", "SwordOnlyDarkRooms",
     "InfiniteBombJump", "MoatSpeedBooster", "LightWorldSouthFakeFlippers", "KholdstareNeedsCaneOfSomaria", "ZoraNeedsRupeeItems",
     "QuarterMagic", "PreventLowResourceDarkWorld", NULL},
    {"TourianBosses", "TourianBossDoor", "EtecoonsJingle", "WallJumpDifficulty", "NerfedCharge", "QuickMorph",
     "SpazersInPool", "SpazersRequired", "PreventScrewAttackSoftLock", "PreventFivePowerBombSeed", "LeftSandPitRequiresSpringBall",
     "LaunchPadRequiresIceBeam", "WaterwayNeedsGravitySuit", "EasyEastCrateriaSkyItem", "EasyBlueBrinstarTop", "ParlorSpeedBooster",
     "MockBall", NULL},
    {"ScreenFilter", "ScreenShape", "SaveStates", "ShowReachable", "ShowFixedKeys", "MusicPackage", NULL},
};
_Static_assert((int)O_LINK == UIO_LINK && (int)O_SAMUS == UIO_SAMUS && (int)O_PLANDO == UIO_PLANDO && (int)O_JINGLE == UIO_JINGLE && (int)O_MAP == UIO_MAP && (int)O_SHAPE == UIO_SHAPE && (int)O_HELP == UIO_HELP && (int)O_HELPS == UIO_HELPS && (int)O_MODE == UIO_MODE, "option indices");

static SmzPlando g_plando;
static int g_plando_loaded;

int ui_set_plando(const char *yaml, size_t len, char *err, size_t errcap)
{
    g_plando_loaded = 0;
    if (!yaml) return 0;
    if (smz_plando_parse(&g_plando, yaml, len, err, errcap)) return -1;
    g_plando_loaded = 1;
    return 0;
}
const SmzPlando *ui_plando(void) { return g_plando_loaded ? &g_plando : NULL; }
void ui_plando_name(const SmzUi *u, char name[32])
{
    int i = u->value[O_PLANDO];
    name[0] = 0;
    if (i > 0 && i < u->sprite_count[2]) { strncpy(name, u->sprites[2][i], 31); name[31] = 0; }
}

void ui_settings(const SmzUi *u, int32_t seed_if_random, SmzSettings *s, SmzPatchCfg *pc, char link[32], char samus[32])
{
    smz_settings_defaults(s);
    s->seed = u->value[O_SEED] ? u->value[O_SEED] : seed_if_random;
    smz_patch_cfg_defaults(pc, s);                 /* the guids are derived from the seed */
    int k = u->value[O_KEYS];
    if (k == 4) k = (unsigned)s->seed % 4;                      /* Random */
    static const int KEYMAP[4] = {KEYS_None, KEYS_Both, KEYS_Zelda, KEYS_SuperMetroid};
    s->keysanity = KEYMAP[k];
    s->placement_rule = u->value[O_PLACE];
    s->ganon_crystals = u->value[O_GANON];
    s->gt_crystals = u->value[O_GT];
    s->tourian_bosses = u->value[O_TOURIAN];
    int py = u->value[O_PYRAMID], dr = u->value[O_DOOR];
    pc->pyramid_open = py == 2 ? (s->seed >> 3) & 1 : py;
    pc->tourian_door_open = dr == 2 ? (s->seed >> 5) & 1 : dr;
    smz_cfg_set(&s->cfg, "WallJumpDifficulty", u->value[O_WALL]);
    for (int i = O_LOGIC0; i < UI_OPT_COUNT; i++) smz_cfg_set(&s->cfg, UI_OPTS[i].key, u->value[i]);
    s->game_mode = u->value[O_MODE];
    s->spazers_in_pool = u->value[O_SPAZ_POOL];
    s->spazers_required = u->value[O_SPAZ_REQ] < s->spazers_in_pool ? u->value[O_SPAZ_REQ] : s->spazers_in_pool;
    for (int g = 0; g < SMZ_ITEMOPT_GROUP_COUNT; g++) s->item_opt[g] = (uint8_t)u->value[O_ITEM0 + g];
    smz_settings_normalize(s);
    if (s->game_mode == MODE_AllDungeons) pc->pyramid_open = 0;
    pc->pyramid_open = pc->pyramid_open != 0;
    pc->light_dark_rooms = u->value[O_DARK];
    pc->nerfed_charge = u->value[O_NERF];
    pc->quick_morph = u->value[O_MORPH];
    pc->sm_default_controls = 1;   /* the app's Super Metroid layout; the C# oracle and gen_rom leave it off */
    pc->etecoons_jingle = u->value[O_JINGLE];
    pc->hint_tiles = u->value[O_HELP] == 0 ? 15 : 0;   /* hint tiles are part of "all helps" only */
    int li = u->value[O_LINK], si = u->value[O_SAMUS];
    link[0] = samus[0] = 0;
    if (li > 0 && li < u->sprite_count[0]) { strncpy(link, u->sprites[0][li], 31); link[31] = 0; }
    if (si > 0 && si < u->sprite_count[1]) { strncpy(samus, u->sprites[1][si], 31); samus[31] = 0; }
    if (g_plando_loaded) {                 /* the plando decides the seed, the goals, the logic and the doors */
        smz_plando_settings(&g_plando, s);
        SmzPatchCfg guids;
        smz_patch_cfg_defaults(&guids, s);   /* the guids follow the plando's seed */
        memcpy(pc->seed_guid, guids.seed_guid, sizeof pc->seed_guid);
        memcpy(pc->world_guid, guids.world_guid, sizeof pc->world_guid);
        pc->pyramid_open = g_plando.open_pyramid;
        pc->tourian_door_open = g_plando.skip_tourian_door;
        pc->hint_tiles = 0;
        pc->plando = &g_plando;
    }
}

void ui_apply_run_settings(SmzUi *u, const char *json)
{
    static const int KEYIDX[4] = {0, 2, 3, 1};   /* KEYS_None, KEYS_Zelda, KEYS_SuperMetroid, KEYS_Both as the Keysanity option */
    SmzSettings s;
    smz_logic_settings(json, &s);
    u->value[O_SEED] = s.seed;
    u->value[O_KEYS] = KEYIDX[s.keysanity & 3];
    u->value[O_PLACE] = s.placement_rule;
    u->value[O_GANON] = s.ganon_crystals;
    u->value[O_GT] = s.gt_crystals;
    u->value[O_TOURIAN] = s.tourian_bosses;
    u->value[O_MODE] = s.game_mode;
    u->value[O_SPAZ_POOL] = s.spazers_in_pool;
    u->value[O_SPAZ_REQ] = s.spazers_required;
    u->value[O_WALL] = smz_cfg_get(&s.cfg, "WallJumpDifficulty");
    for (int i = O_LOGIC0; i < UI_OPT_COUNT; i++) u->value[i] = smz_cfg_get(&s.cfg, UI_OPTS[i].key);
    for (int g = 0; g < SMZ_ITEMOPT_GROUP_COUNT; g++) u->value[O_ITEM0 + g] = s.item_opt[g];
    if (strstr(json, "\"saveStates\":false")) u->value[O_SAVES] = 0;
}

/* 1 when every option that changes the run's rules is at its default; the seed, sprites, screen, map view and jingle are cosmetic. */
static int ui_is_default(const SmzUi *u)
{
    int want[UI_MAX_OPTS];
    ui_expected_values(u, want);
    for (int i = 0; i < UI_OPT_COUNT; i++) {
        if (i == O_SEED || i == O_LINK || i == O_SAMUS || i == O_MAP || i == O_FILTER || i == O_SHAPE || i == O_JINGLE || i == O_MORPH || i == O_MUSIC || i == O_REACH || i == O_FKEYS) continue;
        if (UI_OPTS[i].kind == OPT_SPRITE) { if (u->value[i] != 0) return 0; continue; }
        if (u->value[i] != want[i]) return 0;
    }
    return 1;
}


/* The run's settings as JSON, in the format both frontends store with a run (keys and values as the generator's schema names them). */
int ui_settings_json(const SmzUi *u, int32_t seed_if_random, char *out, size_t cap)
{
    static const char *const KEYS[] = {"None", "Zelda", "SuperMetroid", "Both"};
    static const char *const PLACE[] = {"Anywhere", "DungeonsAndMetroid", "CrystalDungeonsAndMetroid", "SameGame", "OppositeGame"};
    static const char *const FILTER[] = {"sharp", "smooth", "crt", "lcd"};
    static const char *const SHAPE[] = {"original", "wide", "pixel"};
    static const char *const HELP[] = {"all", "limited", "none"};
    static const char *const MODE[] = {"Vanilla", "AllDungeons", "SpazerHunt"};
    SmzSettings s; SmzPatchCfg pc; char link[32], samus[32], plando[32], music[32];
    ui_settings(u, seed_if_random, &s, &pc, link, samus);
    {   /* "" keeps the game's music, "*" draws from every package, anything else is a package folder */
        int m = u->value[O_MUSIC];
        if (m <= 0 || m >= u->sprite_count[3]) music[0] = 0;
        else if (m == 1) strcpy(music, "*");
        else { snprintf(music, sizeof music, "%s", u->sprites[3][m]); for (char *c = music; *c; c++) if (*c == '"' || *c == '\\') *c = '_'; }
    }
    ui_plando_name(u, plando);
    int n = snprintf(out, cap,
        "{\"seed\":\"%d\",\"keysanity\":\"%s\",\"itemPlacement\":\"%s\",\"gameMode\":\"%s\",\"ganonCrystals\":%d,"
        "\"ganonsTowerCrystals\":%d,\"tourianBosses\":%d,\"pyramidHole\":\"%s\",\"tourianBossDoor\":\"%s\","
        "\"mapMode\":\"%s\",\"lightDarkRooms\":%s,\"nerfedCharge\":%s,\"saveStates\":%s,\"quickMorph\":%s,\"musicPackage\":\"%s\",\"defaultRules\":%s,\"screenFilter\":\"%s\",\"screenShape\":\"%s\",\"helpMode\":\"%s\",\"helpCount\":%d,\"linkSprite\":\"%s\",\"samusSprite\":\"%s\",\"plando\":\"%s\",\"etecoonsJingle\":%d,\"showReachable\":%s,\"showFixedKeys\":%s,",
        s.seed, KEYS[s.keysanity], PLACE[s.placement_rule], MODE[s.game_mode], s.ganon_crystals, s.gt_crystals, s.tourian_bosses,
        pc.pyramid_open ? "Open" : "Closed", pc.tourian_door_open ? "Open" : "Closed",
        u->value[O_MAP] ? "discovered" : "all", u->value[O_DARK] ? "true" : "false", u->value[O_NERF] ? "true" : "false", u->value[O_SAVES] ? "true" : "false", u->value[O_MORPH] ? "true" : "false", music, ui_is_default(u) ? "true" : "false", FILTER[u->value[O_FILTER]], SHAPE[u->value[O_SHAPE]], HELP[u->value[O_HELP]], u->value[O_HELPS], link, samus, g_plando_loaded ? plando : "", u->value[O_JINGLE], u->value[O_REACH] ? "true" : "false", u->value[O_FKEYS] ? "true" : "false");
    /* options that differ from the defaults, as "Label: value" lines for the run's info on the bottom screen */
    n += snprintf(out + n, n < (int)cap ? cap - n : 0, "\"diff\":[");
    for (int i = 0, first = 1; i < UI_OPT_COUNT && n < (int)cap; i++) {
        if (i == O_SEED || i == O_LINK || i == O_SAMUS || i == O_MAP || i == O_FILTER || i == O_SHAPE || i == O_JINGLE || i == O_MORPH || i == O_MUSIC || i == O_HELPS) continue;
        if (UI_OPTS[i].kind == OPT_SPRITE ? u->value[i] == 0 : u->value[i] == UI_OPTS[i].def) continue;
        char v[44], line[96];
        ui_value_text(u, i, v, sizeof v);
        snprintf(line, sizeof line, "%s: %s", UI_OPTS[i].label, v);
        for (char *c = line; *c; c++) if (*c == '"' || *c == '\\') *c = '\'';
        n += snprintf(out + n, cap - n, "%s\"%s\"", first ? "" : ",", line);
        first = 0;
    }
    n += snprintf(out + n, n < (int)cap ? cap - n : 0, "],\"logic\":{");
    for (int i = O_LOGIC0; i < UI_OPT_COUNT && n < (int)cap; i++)
        n += snprintf(out + n, cap - n, "\"%s\":%s,", UI_OPTS[i].key, smz_cfg_get(&s.cfg, UI_OPTS[i].key) ? "true" : "false");
    /* the frontends hand every integer in "logic" to the generator by name (smz_setting_set) */
    n += snprintf(out + n, n < (int)cap ? cap - n : 0, "\"GameMode\":%d,\"SpazersInPool\":%d,\"SpazersRequired\":%d,", s.game_mode, s.spazers_in_pool, s.spazers_required);
    for (int g = 0; g < SMZ_ITEMOPT_GROUP_COUNT && n < (int)cap; g++)
        if (s.item_opt[g]) n += snprintf(out + n, cap - n, "\"%s\":%d,", UI_OPTS[O_ITEM0 + g].key, s.item_opt[g]);
    if (n < (int)cap) n += snprintf(out + n, cap - n, "\"WallJumpDifficulty\":%d}}", s.cfg.WallJumpDifficulty);
    return n < (int)cap ? n : -1;
}
