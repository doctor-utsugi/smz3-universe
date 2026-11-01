/* One saved run as a small text file, readable and writable with plain stdio on every device. */
#ifndef SMZ3_RUN_H
#define SMZ3_RUN_H

#include <stdint.h>

typedef struct {
    char name[24];
    char seed[16];
    char rom_path[256];
    char settings[4096];         /* the JSON from ui_settings_json */
    int64_t created, last_played, play_ms;
    int game;                    /* 0 not started, 1 Zelda, 2 Metroid */
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

/* Stable ROM file name "smz3_<seed>_<hash>.sfc": the hash covers the settings JSON without the tracker-only keys (seed,
 * mapMode, screenFilter), so the same run is the same file on every device. Returns the length or -1 if cap is too small. */
int smz_run_rom_name(const char *settings_json, const char *seed, char *out, int cap);

#endif
