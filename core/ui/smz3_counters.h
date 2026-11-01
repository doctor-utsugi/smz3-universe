/* What a run adds to the world goals: enemies and bosses defeated, rupees collected, trips between the games, read from the
 * emulator memory. The host keeps one SmzCounters per run, saves its text with the run and with every save state (loading a
 * state puts the counters back to that point, so repeating a stretch adds nothing), and reports the totals to the server. */
#ifndef SMZ3_COUNTERS_H
#define SMZ3_COUNTERS_H

#include <stddef.h>
#include <stdint.h>

enum {
    SMC_RUPEES, SMC_MOBLIN, SMC_SOLDIER, SMC_OCTOROK, SMC_STALFOS, SMC_HINOX, SMC_LYNEL, SMC_KEESE, SMC_ROPE,
    SMC_ARMOS, SMC_LANMOLAS, SMC_MOLDORM, SMC_AGAHNIM, SMC_HELMASAUR, SMC_ARRGHUS, SMC_MOTHULA, SMC_BLIND, SMC_KHOLDSTARE,
    SMC_VITREOUS, SMC_TRINEXX,
    SMC_METROID, SMC_PIRATE, SMC_ZOOMER, SMC_SIDEHOPPER,
    SMC_KRAID, SMC_PHANTOON, SMC_DRAYGON, SMC_RIDLEY, SMC_SPORE_SPAWN, SMC_CROCOMIRE, SMC_BOTWOON, SMC_GOLDEN_TORIZO,
    SMC_BOMB_TORIZO,
    SMC_SWAPS, SMC_ITEMS, SMC_RUNS,
    SMC_COUNT
};
#define SMC_FIRST_BOSS SMC_ARMOS   /* SMC_ARMOS..SMC_TRINEXX and SMC_KRAID..SMC_BOMB_TORIZO count once per run */

typedef struct {
    uint32_t n[SMC_COUNT];
    uint64_t base;                 /* bit per counter index: bosses already beaten when counting began (a prepared start): they never count */
    int have_base;
    /* last reading */
    int game, have, zmode, smstate, room;
    unsigned rupees;
    uint8_t zstate[16], ztype[16];
    uint16_t sid[32], shp[32];
} SmzCounters;

/* The key the server knows a counter by ("moblin", "kraid", ...). */
const char *smz_counter_key(int i);
void smz_counters_reset(SmzCounters *c);
/* One reading: game is SMZ3_GAME_*, items the locations cleared so far in the run. */
void smz_counters_tick(SmzCounters *c, int game, const uint8_t *wram, size_t wram_len, const uint8_t *sram, size_t sram_len, int items);
/* "key=n,key=n,..." with the counters that are not 0 (and the boss baseline): what the host saves and what it reports. */
int smz_counters_text(const SmzCounters *c, char *out, size_t cap);
/* Restores the counters from that text (a run file or a save state's); NULL or "" starts at 0. */
void smz_counters_load(SmzCounters *c, const char *text);

#endif
