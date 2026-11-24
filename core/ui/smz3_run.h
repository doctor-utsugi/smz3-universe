/* One saved run as a small text file, readable and writable with plain stdio on every device. */
#ifndef SMZ3_RUN_H
#define SMZ3_RUN_H

#include <stdint.h>

/* Which randomizer made a run. SMZ3 is the default everywhere: a run file, settings JSON or server call without a game
 * is SMZ3, so the ids only appear for the other two. */
enum { SMZ_MODE_SMZ3, SMZ_MODE_Z3, SMZ_MODE_SM, SMZ_MODE_COUNT };
const char *smz_mode_id(int mode);          /* "smz3", "z3", "sm" */
int smz_mode_of(const char *id);            /* NULL or "" is SMZ3; -1 for an unknown id */
int smz_settings_mode(const char *json);    /* the "game" of a settings JSON; SMZ3 when it has none, -1 when unknown */

typedef struct {
    char name[24];
    char seed[16];
    char rom_path[256];
    char settings[4096];         /* the JSON from ui_settings_json */
    int64_t created, last_played, play_ms;
    int mode;                    /* SMZ_MODE_*: which randomizer */
    int game;                    /* 0 not started, 1 Zelda, 2 Metroid: the game on screen */
    int zelda_cleared, zelda_total, metroid_cleared, metroid_total;
    int64_t save_ms; int save_zelda, save_metroid;   /* the game's own save: play time and checks done (0: not known) */
    int boss_mask, crystal_mask;  /* bit 0 Kraid, 1 Phantoon, 2 Draygon, 3 Ridley; bit n-1 is crystal n */
    char goals[640];             /* the world goal counters (smz_counters_text) */
} SmzRun;

/* 0 on success, -1 when the file is missing or unreadable. */
int smz_run_save(const char *path, const SmzRun *r);
int smz_run_load(const char *path, SmzRun *r);
/* "Zelda 3/216 · Metroid 0/100 · 1:02:03" (ASCII separators), at most cap bytes. */
void smz_run_summary(const SmzRun *r, char *out, int cap);

/* Stable ROM file name "<mode>_<seed>_<hash>.sfc" (smz3_, z3_ or sm_ from the settings' game): the hash covers the settings JSON without the tracker-only keys (seed,
 * mapMode, screenFilter), so the same run is the same file on every device. Returns the length or -1 if cap is too small. */
int smz_run_rom_name(const char *settings_json, const char *seed, char *out, int cap);

#endif
