#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smz3_runlogic.h"
#include "smz3_itemnames.h"
#include "smz3_state.h"

static const char *find_key(const char *json, const char *key)
{
    char pat[96];
    snprintf(pat, sizeof pat, "\"%s\":", key);
    const char *p = strstr(json, pat);
    return p ? p + strlen(pat) : NULL;
}

static int json_int(const char *json, const char *key, int def)
{
    const char *p = find_key(json, key);
    if (!p) return def;
    if (*p == '"') p++;
    return atoi(p);
}

static int json_enum(const char *json, const char *key, const char *const *names, int n, int def)
{
    const char *p = find_key(json, key);
    if (!p || *p != '"') return def;
    p++;
    for (int i = 0; i < n; i++) if (!strncmp(p, names[i], strlen(names[i])) && p[strlen(names[i])] == '"') return i;
    return def;
}

int smz_logic_settings(const char *json, SmzSettings *s)
{
    static const char *const KEYS[] = {"None", "Zelda", "SuperMetroid", "Both"};
    static const char *const RULES[] = {"Anywhere", "DungeonsAndMetroid", "CrystalDungeonsAndMetroid", "SameGame", "OppositeGame"};
    smz_settings_defaults(s);
    s->seed = json_int(json, "seed", 0);
    s->keysanity = json_enum(json, "keysanity", KEYS, 4, 0);
    s->placement_rule = json_enum(json, "itemPlacement", RULES, 5, 0);
    s->ganon_crystals = json_int(json, "ganonCrystals", 7);
    s->gt_crystals = json_int(json, "ganonsTowerCrystals", 7);
    s->tourian_bosses = json_int(json, "tourianBosses", 4);
    const char *p = find_key(json, "logic");
    if (!p || *p != '{') return 0;
    for (p++; *p && *p != '}';) {
        if (*p == ',') { p++; continue; }
        if (*p != '"') break;
        const char *q = strchr(p + 1, '"');
        if (!q) break;
        char name[96];
        snprintf(name, sizeof name, "%.*s", (int)(q - p - 1), p + 1);
        p = q + 1;
        if (*p == ':') p++;
        int v = !strncmp(p, "true", 4) ? 1 : !strncmp(p, "false", 5) ? 0 : atoi(p);
        smz_setting_set(s, name, v);
        while (*p && *p != ',' && *p != '}') p++;
    }
    return 0;
}

int smz_logic_build(SmzLogic *l, const char *json)
{
    SmzSettings s, eff;
    l->valid = 0;
    const char *plando = find_key(json, "plandoYaml");
    if (plando && *plando == '"' && plando[1] != '"') return -1;
    smz_logic_settings(json, &s);
    l->valid = smz_fill_valid(&l->seed, &s, &eff) == 0;
    return l->valid ? 0 : -1;
}

int smz_logic_reachable(const SmzLogic *l, const int *items, int n, uint8_t *in_logic, int cap)
{
    if (!l->valid) return 0;
    int all[1100], na = smz_assumed_items(&l->seed, all, 600);
    for (int i = 0; i < n && na < 1100; i++) all[na++] = items[i];
    int ids[512];
    int k = smz_reachable(&l->seed, all, na, ids, 512), count = 0;
    memset(in_logic, 0, (size_t)cap);
    for (int i = 0; i < k; i++)
        for (unsigned j = 0; j < smz3_location_count && (int)j < cap; j++)
            for (int q = 0; q < SMZ_LOC_NAME_COUNT; q++)
                if (SMZ_LOC_NAMES[q].id == ids[i]) {
                    if (!strcmp(SMZ_LOC_NAMES[q].name, smz3_locations[j].id) && !in_logic[j]) { in_logic[j] = 1; count++; }
                    break;
                }
    return count;
}
