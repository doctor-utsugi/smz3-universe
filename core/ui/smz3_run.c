#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smz3_run.h"

int smz_run_save(const char *path, const SmzRun *r)
{
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "SMZRUN1\nname=%s\nseed=%s\nrom=%s\ncreated=%lld\nplayed=%lld\nplay_ms=%lld\ngame=%d\nzc=%d\nzt=%d\nmc=%d\nmt=%d\nbm=%d\ncm=%d\nsms=%lld\nsz=%d\nsm=%d\ngoals=%s\nsettings=%s\n",
            r->name, r->seed, r->rom_path, (long long)r->created, (long long)r->last_played, (long long)r->play_ms, r->game,
            r->zelda_cleared, r->zelda_total, r->metroid_cleared, r->metroid_total, r->boss_mask, r->crystal_mask, (long long)r->save_ms, r->save_zelda, r->save_metroid, r->goals, r->settings);
    return fclose(f) ? -1 : 0;
}

static void copy(char *dst, size_t cap, const char *v) { snprintf(dst, cap, "%s", v); }

int smz_run_load(const char *path, SmzRun *r)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    static char line[4200];
    memset(r, 0, sizeof *r);
    if (!fgets(line, sizeof line, f) || strncmp(line, "SMZRUN1", 7)) { fclose(f); return -1; }
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *v = strchr(line, '=');
        if (!v) continue;
        *v++ = 0;
        if (!strcmp(line, "name")) copy(r->name, sizeof r->name, v);
        else if (!strcmp(line, "seed")) copy(r->seed, sizeof r->seed, v);
        else if (!strcmp(line, "rom")) copy(r->rom_path, sizeof r->rom_path, v);
        else if (!strcmp(line, "settings")) copy(r->settings, sizeof r->settings, v);
        else if (!strcmp(line, "created")) r->created = atoll(v);
        else if (!strcmp(line, "played")) r->last_played = atoll(v);
        else if (!strcmp(line, "play_ms")) r->play_ms = atoll(v);
        else if (!strcmp(line, "sms")) r->save_ms = atoll(v);
        else if (!strcmp(line, "sz")) r->save_zelda = atoi(v);
        else if (!strcmp(line, "sm")) r->save_metroid = atoi(v);
        else if (!strcmp(line, "game")) r->game = atoi(v);
        else if (!strcmp(line, "zc")) r->zelda_cleared = atoi(v);
        else if (!strcmp(line, "zt")) r->zelda_total = atoi(v);
        else if (!strcmp(line, "mc")) r->metroid_cleared = atoi(v);
        else if (!strcmp(line, "mt")) r->metroid_total = atoi(v);
        else if (!strcmp(line, "bm")) r->boss_mask = atoi(v);
        else if (!strcmp(line, "cm")) r->crystal_mask = atoi(v);
        else if (!strcmp(line, "goals")) copy(r->goals, sizeof r->goals, v);
    }
    fclose(f);
    return r->name[0] ? 0 : -1;
}

void smz_run_summary(const SmzRun *r, char *out, int cap)
{
    long long s = r->play_ms / 1000;
    snprintf(out, cap, "Zelda %d/%d - Metroid %d/%d - %lld:%02lld:%02lld", r->zelda_cleared, r->zelda_total,
             r->metroid_cleared, r->metroid_total, s / 3600, s / 60 % 60, s % 60);
}

int smz_run_rom_name(const char *json, const char *seed, char *out, int cap)
{
    static const char *const SKIP[] = {"\"seed\":\"", "\"mapMode\":\"", "\"screenFilter\":\"", "\"screenShape\":\""};
    uint32_t h = 0x811C9DC5u;
    for (const char *p = json; *p;) {
        int skipped = 0;
        for (int i = 0; i < (int)(sizeof SKIP / sizeof SKIP[0]) && !skipped; i++) {
            size_t n = strlen(SKIP[i]);
            if (!strncmp(p, SKIP[i], n)) {
                const char *q = strchr(p + n, '"');
                if (!q) break;
                p = q + 1;
                if (*p == ',') p++;
                skipped = 1;
            }
        }
        if (!skipped && !strncmp(p, "\"helpCount\":", 12)) {       /* how many helps the player may ask for is a tracker option */
            p += 12;
            while (*p >= '0' && *p <= '9') p++;
            if (*p == ',') p++;
            skipped = 1;
        }
        if (skipped) continue;
        h = (h ^ (uint8_t)*p++) * 0x01000193u;
    }
    int n = snprintf(out, cap, "smz3_%s_%08x.sfc", seed, h);
    return n < cap ? n : -1;
}
