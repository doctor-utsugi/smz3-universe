#include <stdlib.h>
#include <string.h>
#include "sm_generate.h"

int sm_option_set(SmGenSettings *s, const char *key, const char *value)
{
    if (strcmp(key, "seed") == 0) {
        char *end;
        long v = strtol(value, &end, 10);
        if (*value == 0 || *end || v < 0 || v > 2147483647L) return -1;   /* the C# rejects negative seeds */
        s->seed = (int32_t)v;
        return 0;
    }
    if (strcmp(key, "race") == 0) {
        if (strcmp(value, "true") && strcmp(value, "false")) return -1;
        s->race = strcmp(value, "true") == 0;
        return 0;
    }
    if (strcmp(key, "goal") == 0) return strcmp(value, "defeatmb") ? -1 : 0;
    for (int o = 0; o < SM_OPTION_COUNT; o++) {
        if (strcmp(SM_OPTIONS[o].key, key) || !SM_OPTIONS[o].values) continue;
        for (int v = 0; v < SM_OPTIONS[o].n_values; v++) {
            if (strcmp(SM_OPTIONS[o].values[v].key, value)) continue;
            if (strcmp(key, "logic") == 0) s->logic = v;          /* enum order: Casual, Tournament */
            else if (strcmp(key, "placement") == 0) s->placement = v;   /* Full, Split */
            else return -1;
            return 0;
        }
    }
    return -1;
}

int sm_generate(const SmGenSettings *s, const uint8_t *sm_rom, uint8_t *out_rom, size_t out_cap,
                const SmPatchIds *ids, SmSeed *seed_out, uint8_t *hash_out)
{
    static SmSeed seed;
    if (out_cap < SM_ROM_SIZE) return -1;
    memcpy(out_rom, sm_rom, SM_JU_SIZE);
    memset(out_rom + SM_JU_SIZE, 0, SM_ROM_SIZE - SM_JU_SIZE);
    if (sm_apply_ips(out_rom, SM_ROM_SIZE, SM_BASE_IPS, SM_BASE_IPS_LEN)) return -2;
    if (sm_fill(&seed, s)) return -3;
    /* The C# checks this only when a spoiler is made (not Race); the filler should never fail it either way. */
    if (!sm_playthrough_ok(&seed)) return -4;
    sm_patch(out_rom, &seed, ids, hash_out);
    if (seed_out) *seed_out = seed;
    return 0;
}
