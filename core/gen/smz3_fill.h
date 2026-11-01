/* Item pools and the standard (single world) filler, following the C# generator step by step. */
#ifndef SMZ3_FILL_H
#define SMZ3_FILL_H

#include "smz3_world.h"
#include "smz3_rng.h"

enum { RULE_Anywhere, RULE_DungeonsAndMetroid, RULE_CrystalDungeonsAndMetroid, RULE_SameGame, RULE_OppositeGame };
enum { KEYS_None, KEYS_Zelda, KEYS_SuperMetroid, KEYS_Both };
enum { MODE_Vanilla, MODE_AllDungeons, MODE_SpazerHunt };
#define SMZ_ITEMOPT_GROUP_COUNT 48   /* option groups of ItemSettingOptions.yml (smz3_itemopts_data.h) */
#define SMZ_MAX_START_ITEMS 128

typedef struct {
    int32_t seed;
    int keysanity;               /* KEYS_* */
    int placement_rule;          /* RULE_* */
    int ganon_crystals, gt_crystals, tourian_bosses;
    int gt_bigkey_in_gt;         /* keysanity: Ganon's Tower big key placed inside Ganon's Tower */
    int randomized_bottles;
    int game_mode;               /* MODE_* */
    int spazers_in_pool, spazers_required;   /* Spazer Hunt */
    uint8_t item_opt[SMZ_ITEMOPT_GROUP_COUNT];   /* chosen option of every start-with / early item group (0 = randomized) */
    SmzCfg cfg;
} SmzSettings;

typedef struct { uint8_t type, progression; uint16_t id; } SmzItem;

typedef struct {
    SmzWorld world;
    uint8_t item_prog[512];      /* Item.Progression of the item placed at each location */
    SmzRng rng;                  /* the generator's main random generator, as left after filling */
    int start_items[SMZ_MAX_START_ITEMS];   /* item types the player starts with */
    int n_start;
    int game_mode;               /* MODE_* the seed was filled for */
} SmzSeed;

void smz_settings_defaults(SmzSettings *s);
/* Applies what the game mode forces on the settings (All Dungeons and Spazer Hunt fix the crystal and boss counts). Idempotent. */
void smz_settings_normalize(SmzSettings *s);
/* An empty world for these settings (nothing placed, default rewards): what smz_fill starts from. */
void smz_seed_init(SmzSeed *out, const SmzSettings *s);
/* 0 on success, negative on failure (no placement found, infinite loop). */
int smz_fill(SmzSeed *out, const SmzSettings *s);

#endif
