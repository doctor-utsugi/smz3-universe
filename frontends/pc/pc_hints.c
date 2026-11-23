#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "pc_dungeon.h"
#include "pc_hints.h"
#include "pc_store.h"
#include "smz3_runlogic.h"
#include "smz3_text.h"
#include "smz3_tracker.h"

static int cur_slot, mode, total, used, n_hints, fixed;
static char seed[16], hints[SMZ_HELP_COUNT][SMZ_HELP_TEXT], settings[sizeof ((SmzRun *)0)->settings];
static uint8_t revealed[SMZ_TRK_MAX_LOCS];
static char texts[SMZ_HELP_COUNT][SMZ_HELP_TEXT];
static int have_texts;
static volatile int job;   /* 0 idle, 1 building the texts, 2 done */

static int flag(const char *json, const char *key, int def)
{
    char pat[48];
    snprintf(pat, sizeof pat, "\"%s\":", key);
    const char *p = strstr(json, pat);
    return p ? !strncmp(p + strlen(pat), "true", 4) : def;
}

static void save(void)
{
    static char t[SMZ_HELP_COUNT * (SMZ_HELP_TEXT + 8) + SMZ_TRK_MAX_LOCS * 4 + 96];
    int n = snprintf(t, sizeof t, "seed=%s\nused=%d\nfixed=%d\nrevealed=", seed, used, fixed);
    for (int i = 0; i < SMZ_TRK_MAX_LOCS; i++) if (revealed[i]) n += snprintf(t + n, sizeof t - (size_t)n, "%d,", i);
    n += snprintf(t + n, sizeof t - (size_t)n, "\n");
    for (int i = 0; i < n_hints; i++) n += snprintf(t + n, sizeof t - (size_t)n, "hint=%s\n", hints[i]);
    store_write(store_path("runs/slot%d.help", cur_slot), t, (size_t)n);
}

void hints_open(int slot, const SmzRun *r)
{
    hints_close();
    cur_slot = slot; used = n_hints = have_texts = 0;
    memset(revealed, 0, sizeof revealed);
    snprintf(seed, sizeof seed, "%s", r->seed);
    snprintf(settings, sizeof settings, "%s", r->settings);
    mode = strstr(r->settings, "\"helpMode\":\"limited\"") ? HELP_LIMITED : strstr(r->settings, "\"helpMode\":\"none\"") ? HELP_NONE : HELP_ALL;
    const char *c = strstr(r->settings, "\"helpCount\":");
    total = c ? atoi(c + 12) : 3;
    fixed = flag(r->settings, "showFixedKeys", !strcmp(r->name, "Cozy run"));
    size_t n;
    char *t = (char *)store_read(store_path("runs/slot%d.help", slot), &n);
    if (t) {
        t[n] = 0;
        char *line = strtok(t, "\n");
        if (line && !strncmp(line, "seed=", 5) && !strcmp(line + 5, seed))   /* a file left by an older run of this slot is not this run's */
            while ((line = strtok(NULL, "\n"))) {
                if (!strncmp(line, "used=", 5)) used = atoi(line + 5);
                else if (!strncmp(line, "fixed=", 6)) fixed = atoi(line + 6) != 0;
                else if (!strncmp(line, "hint=", 5) && n_hints < SMZ_HELP_COUNT) snprintf(hints[n_hints++], SMZ_HELP_TEXT, "%s", line + 5);
                else if (!strncmp(line, "revealed=", 9))
                    for (const char *p = line + 9; *p; p = strchr(p, ',') ? strchr(p, ',') + 1 : "") { int i = atoi(p); if (i >= 0 && i < SMZ_TRK_MAX_LOCS) revealed[i] = 1; }
            }
        free(t);
    }
    dungeon_set_fixed_keys(fixed);
}

void hints_close(void) { while (job == 1) SDL_Delay(5); job = 0; }
int hints_mode(void) { return mode; }
int hints_total(void) { return total; }
int hints_left(void) { int l = total - used; return l > 0 ? l : 0; }
int hints_can_ask(void) { return mode == HELP_ALL || (mode == HELP_LIMITED && used < total); }
int hints_count(void) { return n_hints; }
const char *hints_text(int i) { return hints[i]; }
int hints_busy(void) { return job == 1; }
const uint8_t *hints_visible(void) { return mode == HELP_ALL ? NULL : revealed; }
int hints_fixed_keys(void) { return fixed; }
void hints_set_fixed_keys(int on) { fixed = on; dungeon_set_fixed_keys(on); save(); }

/* Rebuilding the seed needs far more stack than a default thread has. */
static void *build_thread(void *arg)
{
    static SmzLogic logic;
    SmzSettings s;
    SmzPatchCfg cfg;
    (void)arg;
    if (smz_logic_build(&logic, settings) == 0) {
        smz_logic_settings(settings, &s);
        smz_patch_cfg_defaults(&cfg, &s);
        cfg.pyramid_open = strstr(settings, "\"pyramidHole\":\"Open\"") != NULL;
        cfg.tourian_door_open = strstr(settings, "\"tourianBossDoor\":\"Open\"") != NULL;
        smz_help_texts(&logic.seed, &cfg, texts);
        have_texts = 1;
    }
    job = 2;
    return NULL;
}

void hints_ask(void)
{
    if (job || !hints_can_ask() || n_hints >= SMZ_HELP_COUNT) return;
    job = 2;
    if (have_texts) return;
    pthread_attr_t at;
    pthread_t th;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 64u << 20);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    job = 1;
    if (pthread_create(&th, &at, build_thread, NULL)) job = 2;
    pthread_attr_destroy(&at);
}

int hints_poll(char *note, size_t cap)
{
    if (job != 2) return 0;
    job = 0;
    const char *text = have_texts && n_hints < SMZ_HELP_COUNT ? texts[n_hints] : "";
    if (!text[0] || !hints_can_ask()) { snprintf(note, cap, "No more hints."); return 1; }
    snprintf(hints[n_hints], SMZ_HELP_TEXT, "%s", text);
    for (char *c = hints[n_hints]; *c; c++) if (*c == '\n') *c = ' ';
    n_hints++;
    if (mode == HELP_LIMITED) used++;
    save();
    snprintf(note, cap, "New hint: %s", hints[n_hints - 1]);
    return 1;
}

void hints_reveal(const int *locations, int n)
{
    if (!hints_can_ask()) return;
    if (mode == HELP_LIMITED) used++;
    for (int i = 0; i < n; i++) if (locations[i] >= 0 && locations[i] < SMZ_TRK_MAX_LOCS) revealed[locations[i]] = 1;
    save();
}
