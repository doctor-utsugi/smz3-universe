/* Differential test of the logic port: replays the vectors written by `gen-cli vectors` (the C# answers) against smz3_logic.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smz3_logic_table.h"

static int cfg_set(SmzCfg *cfg, const char *name, int v)
{
    for (size_t i = 0; i < sizeof CFG_TABLE / sizeof *CFG_TABLE; i++)
        if (!strcmp(CFG_TABLE[i].name, name)) {
            if (!strcmp(name, "WallJumpDifficulty")) *(int *)((char *)cfg + CFG_TABLE[i].offset) = v;
            else *(bool *)((char *)cfg + CFG_TABLE[i].offset) = v != 0;
            return 1;
        }
    return 0;
}

int main(int argc, char **argv)
{
    FILE *f = fopen(argc > 1 ? argv[1] : "build/logic_vectors.txt", "r");
    if (!f) { perror("vectors"); return 2; }
    static char line[1 << 20];
    SmzProg p; SmzCfg cfg;
    int cases = 0, checks = 0, bad = 0;
    while (fgets(line, sizeof line, f)) {
        char *tok, *save;
        if (!strncmp(line, "case", 4)) { memset(&p, 0, sizeof p); memset(&cfg, 0, sizeof cfg); cases++; continue; }
        char *key = strtok_r(line, " \n", &save);
        if (!key) continue;
        while ((tok = strtok_r(NULL, " \n", &save))) {
            if (!strcmp(key, "cfg")) {
                char *eq = strchr(tok, '='); *eq = 0;
                if (!cfg_set(&cfg, tok, atoi(eq + 1))) { fprintf(stderr, "unknown config %s\n", tok); return 2; }
            } else if (!strcmp(key, "items") || !strcmp(key, "rewards")) {
                int id, n; sscanf(tok, "%d:%d", &id, &n);
                if (key[0] == 'i') p.items[id] = n; else p.rewards[id] = n;
            } else if (!strcmp(key, "bosses")) {
                p.bosses[atoi(tok)] = 1;
            } else if (!strcmp(key, "res")) {
                char *eq = strchr(tok, '='); *eq = 0;
                int expect = atoi(eq + 1);
                int a[4] = {0};
                char *slash = strchr(tok, '/');
                int na = 0;
                if (slash) { *slash = 0; char *s2; for (char *t = strtok_r(slash + 1, "/", &s2); t; t = strtok_r(NULL, "/", &s2)) a[na++] = atoi(t); }
                int found = 0;
                for (size_t i = 0; i < sizeof LOGIC_TABLE / sizeof *LOGIC_TABLE; i++)
                    if (!strcmp(LOGIC_TABLE[i].name, tok)) {
                        int got = LOGIC_TABLE[i].fn(&p, &cfg, a);
                        if (!strncmp(tok, "P.", 2)) { if (got != expect && !(expect == 1 && got == 1)) { /* ints compare exactly */ } }
                        checks++;
                        if (got != expect) { if (bad++ < 20) fprintf(stderr, "case %d: %s expected %d got %d\n", cases, tok, expect, got); }
                        found = 1;
                    }
                if (!found) { static int warned; if (!warned++) fprintf(stderr, "no C function for %s (and others)\n", tok); }
            }
        }
    }
    printf("%d cases, %d checks, %d mismatches\n", cases, checks, bad);
    return bad != 0;
}
