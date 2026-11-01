/* Plandomizer: loads a plando config (the YAML of the C# SMZ3 Cas' Randomizer, e.g. its Spoiler_Plando files) and builds the
 * seed from it instead of filling. Only the flat YAML the C# serializer writes is understood. */
#ifndef SMZ3_PLANDO_H
#define SMZ3_PLANDO_H

#include <stddef.h>
#include <stdint.h>
#include "smz3_fill.h"
#include "smz3_text.h"

typedef struct SmzPlando {
    char seed[64];
    int game_mode;                         /* MODE_* */
    int gt_crystals, ganon_crystals, tourian_bosses, spazers_required;
    int liftoff_on_goal;
    int keysanity;                         /* KEYS_* */
    int skip_tourian_door, open_pyramid;
    SmzCfg cfg;
    int16_t loc_item[512];                 /* item type per location id, -1 when the config does not set it */
    int16_t region_reward[64];             /* reward per region index, -1 not set */
    int16_t region_prereq[64];             /* medallion per region index, -1 not set */
    int n_enemy_drops, n_tree_pulls;
    uint8_t enemy_drops[64], tree_pulls[8];   /* DropPrize bytes; counts of 0 mean "random" */
    int crab_base, crab_eight, stun, fish;    /* DropPrize byte, or -1 */
    int waterfall_trade, pyramid_trade;       /* bottle item types, or -1 */
    char text[SMZ_TXT_COUNT][256];            /* the Text section: overrides of in-game texts, "" for none */
} SmzPlando;

/* Parses the YAML text. 0 on success; otherwise -1 and err holds the reason (unknown location, bad item name...). */
int smz_plando_parse(SmzPlando *p, const char *text, size_t len, char *err, size_t errcap);
/* The generator's seed number for a plando seed string (a plain number, a hex number, or a hash of any other text). */
int32_t smz_seed_number(const char *seed);
/* The text overrides of a plando as the text patcher takes them. */
void smz_plando_text(const SmzPlando *p, SmzTextOverrides *ov);
/* The settings a plando implies. */
void smz_plando_settings(const SmzPlando *p, SmzSettings *s);
/* Builds the world: every location gets its item, regions their rewards and medallions. Fails (-1, err filled) when the config
 * leaves a location, reward or medallion unset. */
int smz_plando_seed(SmzSeed *out, const SmzPlando *p, char *err, size_t errcap);

#endif
