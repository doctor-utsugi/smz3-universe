/* The whole standalone A Link to the Past generator (port of sporchia's alttp_php_randomizer): seed in, ROM out. */
#ifndef Z3_GENERATE_H
#define Z3_GENERATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "z3_rng.h"

#define Z3_JP_SIZE 0x100000    /* ALttP JP 1.0, no copier header, MD5 03a63945398191337e896e5771f77173 */
#define Z3_ROM_SIZE 0x200000   /* Rom::SIZE: the base patch grows the ROM to 2 MB */

/* The base patch (patches/z3_base.ips, built by tools/z3/build_z3_ips.sh), embedded by tools/z3/gen_z3_ips_c.py. */
extern const uint8_t Z3_BASE_IPS[];
extern const size_t Z3_BASE_IPS_LEN;

/* RandomizerController::prepSeed's request. Enum values are z3_world_gen.h's
   (Z3_MODE_STATE_*, Z3_GOAL_*, ...); crystals -1 means "random". Cosmetics at -1 leave the base ROM's value. */
typedef struct {
    int mode_state;          /* world_state: Z3_MODE_STATE_STANDARD, _OPEN, _INVERTED, _RETRO */
    int goal;                /* Z3_GOAL_*: every goal of alttpr's table */
    int crystals_tower;      /* tower_open: 0..7, -1 random */
    int crystals_ganon;      /* ganon_open: 0..7, -1 random */
    int dungeon_items;       /* Z3_DUNGEONITEMS_* */
    int accessibility;       /* Z3_ACCESSIBILITY_* */
    int item_placement;      /* Z3_ITEMPLACEMENT_* */
    int weapons;             /* Z3_MODE_WEAPONS_* */
    int item_pool;           /* Z3_ITEM_POOL_NORMAL, _HARD, _EXPERT */
    int item_functionality;  /* Z3_ITEM_FUNCTIONALITY_NORMAL, _HARD, _EXPERT */
    bool hints;              /* spoil.Hints: the telepathic tiles' hints (off writes the version sign instead) */
    bool tournament;         /* setTournamentType('standard') and no spoiler */
    int heart_beep;          /* Rom::setHeartBeepSpeed: 0 off, 1 double, 2 normal, 3 half, 4 quarter; -1 keep */
    int menu_speed;          /* Rom::setMenuSpeed: 0 slow, 1 normal, 2 fast, 3 instant; -1 keep */
    int quickswap;           /* Rom::setQuickSwap: 0/1; -1 keep */
    int logic;               /* glitches_required: Z3_LOGIC_NOGLITCHES (0, the default) ... _NOLOGIC */
    int boss_shuffle;        /* boss_shuffle: Z3_ENEMIZER_BOSSSHUFFLE_NONE (0, the default), _SIMPLE, _FULL, _RANDOM */
    bool start_boots;        /* "start_boots": Pegasus Boots from the start, as SahasrahBot's casualboots customizer eq (not an alttpr option) */
} Z3GenSettings;

/* Option table in alttpr's own keys and values (config/alttp.php randomizer.item), made by tools/z3/gen_z3_options.py. */
typedef struct { const char *key, *description; } Z3OptValue;
typedef struct { const char *key, *description, *type; const Z3OptValue *values; int n_values; const char *def; } Z3Option;
extern const Z3Option Z3_OPTIONS[];
extern const int Z3_OPTION_COUNT;

/* alttpr.com's default preset (Original rules). */
void z3_settings_default(Z3GenSettings *s);
/* Sets one option by alttpr's key and value ("world_state" "standard", "tower_open" "random", "tournament" "true"; also "start_boots").
   0 on success, -1 for an unknown key or a value the table does not have. */
int z3_option_set(Z3GenSettings *s, const char *key, const char *value);

/* jp_rom holds the JP 1.0 ROM (jp_len Z3_JP_SIZE); out_rom gets Z3_ROM_SIZE bytes. hash_out, when not NULL, gets
   the 21-character seed string written at 0x7FC0 plus a NUL (22 bytes); icons_out, when not NULL, the five start
   screen hash icons written at 0x180215. Returns 0, or -1 bad sizes, -2 base patch error, -3 fill failed (alttpr's
   exceptions: no location for an item, prizes unplaceable), -4 the seed cannot be beaten, -5 the texts outgrew the
   text table (Text::getByteArray's "Too BIG"), -6 the Enemizer's boss writes failed (never seen). With boss shuffle
   the Enemizer's seed is z3_enemizer_seed(seed). */
int z3_generate(const Z3GenSettings *s, const uint8_t *jp_rom, size_t jp_len, uint8_t *out_rom, size_t out_cap,
                uint32_t seed, char *hash_out, uint8_t *icons_out);
/* The same on a caller's generator (z3_rng_init_mt for a run that replays the oracle's draws); seed still names the
   seed string and the hash icons. */
int z3_generate_rng(const Z3GenSettings *s, const uint8_t *jp_rom, size_t jp_len, uint8_t *out_rom, size_t out_cap,
                    Z3Rng *r, uint32_t seed, char *hash_out, uint8_t *icons_out);

#endif
