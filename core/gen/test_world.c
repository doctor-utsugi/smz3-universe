/* Differential test of the world port: replays `gen-cli worldvectors` (the C# answers) against smz3_world*.c. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smz3_world.h"
#include "smz3_world_gen.h"
#include "smz3_logic_table.h"

static void cfg_set(SmzCfg *cfg, const char *name, int v)
{
    for (size_t i = 0; i < sizeof CFG_TABLE / sizeof *CFG_TABLE; i++)
        if (!strcmp(CFG_TABLE[i].name, name)) {
            if (!strcmp(name, "WallJumpDifficulty")) *(int *)((char *)cfg + CFG_TABLE[i].offset) = v;
            else *(bool *)((char *)cfg + CFG_TABLE[i].offset) = v != 0;
            return;
        }
    fprintf(stderr, "unknown config %s\n", name);
    exit(2);
}

static int bad, checks;
static void mismatch(int cas, const char *what, int id, int expect, int got)
{
    checks++;
    if (expect != got && bad++ < 40) fprintf(stderr, "case %d: %s %d expected %d got %d\n", cas, what, id, expect, got);
}

int main(int argc, char **argv)
{
    FILE *f = fopen(argc > 1 ? argv[1] : "build/world_vectors.txt", "r");
    if (!f) { perror("vectors"); return 2; }
    static char line[1 << 20];
    SmzWorld w; SmzProg p;
    int cases = 0;
    while (fgets(line, sizeof line, f)) {
        char *save, *tok;
        char *key = strtok_r(line, " \n", &save);
        if (!key) continue;
        if (!strcmp(key, "case")) { smz_world_init(&w); memset(&p, 0, sizeof p); cases++; continue; }
        static unsigned char avail[512];
        if (!strcmp(key, "available")) memset(avail, 0, sizeof avail);
        int idx = 0;
        while ((tok = strtok_r(NULL, " \n", &save))) {
            if (!strcmp(key, "cfg")) { char *eq = strchr(tok, '='); *eq = 0; cfg_set(&w.cfg, tok, atoi(eq + 1)); }
            else if (!strcmp(key, "world")) {
                char *eq = strchr(tok, '='); *eq = 0; int v = atoi(eq + 1);
                if (!strcmp(tok, "zk")) w.zelda_keysanity = v; else if (!strcmp(tok, "mk")) w.metroid_keysanity = v;
                else if (!strcmp(tok, "ganon")) w.ganon_crystals = v; else if (!strcmp(tok, "gt")) w.gt_crystals = v;
                else if (!strcmp(tok, "tourian")) w.tourian_bosses = v;
            } else if (!strcmp(key, "rewards")) { int r, v; sscanf(tok, "%d:%d", &r, &v); w.reward_of[r] = v; }
            else if (!strcmp(key, "prereq")) { int r, v; sscanf(tok, "%d:%d", &r, &v); w.prereq[r] = v; }
            else if (!strcmp(key, "placed")) { int l, v; sscanf(tok, "%d:%d", &l, &v); w.item_at[l] = v; }
            else if (!strcmp(key, "items")) { int id, n; sscanf(tok, "%d:%d", &id, &n); p.items[id] = n; }
            else if (!strcmp(key, "progrewards")) { int id, n; sscanf(tok, "%d:%d", &id, &n); p.rewards[id] = n; }
            else if (!strcmp(key, "bosses")) p.bosses[atoi(tok)] = 1;
            else if (!strcmp(key, "available")) avail[atoi(tok)] = 1;
            else if (!strcmp(key, "enter")) {
                mismatch(cases, "enter(true)", idx, tok[0] - '0', region_can_enter(&w, idx, &p, true));
                mismatch(cases, "enter(false)", idx, tok[1] - '0', region_can_enter(&w, idx, &p, false));
                idx++;
            } else if (!strcmp(key, "reward") || !strcmp(key, "boss")) {
                int n = -1;
                for (int r = 0; r < REG_COUNT; r++) {
                    int ok = key[0] == 'r' ? SMZ_REGIONS[r].has_reward : SMZ_REGIONS[r].default_boss != BOSS_None;
                    if (ok && ++n == idx) {
                        if (key[0] == 'r') {
                            mismatch(cases, "reward(false)", r, tok[0] - '0', region_can_retrieve_reward(&w, r, &p, false));
                            /* tok[1] (isTracking) applies tracker-only logic (marked medallions): not part of the generator */
                        } else mismatch(cases, "boss", r, tok[0] - '0', region_can_beat_boss(&w, r, &p, false));
                        break;
                    }
                }
                idx++;
            }
        }
        if (!strcmp(key, "available"))
            for (int i = 0; i < SMZ_LOC_COUNT; i++) mismatch(cases, "location", SMZ_LOCS[i].id, avail[SMZ_LOCS[i].id], loc_available(&w, SMZ_LOCS[i].id, &p));
    }
    printf("%d cases, %d checks, %d mismatches\n", cases, checks, bad);
    return bad != 0;
}
