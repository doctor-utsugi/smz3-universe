#include <string.h>
#include "smz3_itemopts.h"
#include "smz3_enums.h"
#include "smz3_itemopts_data.h"

_Static_assert(SMZ_ITEMOPT_GROUPS == SMZ_ITEMOPT_GROUP_COUNT, "SmzSettings.item_opt must hold every option group");

int smz_itemopt_group(const char *name)
{
    for (int g = 0; g < SMZ_ITEMOPT_GROUPS; g++) if (!strcmp(SMZ_ITEMOPT_GROUP[g].name, name)) return g;
    return -1;
}

int smz_itemopt_select(SmzSettings *s, const char *group, int option)
{
    int g = smz_itemopt_group(group);
    if (g < 0 || option < 0 || option >= SMZ_ITEMOPT_GROUP[g].count) return -1;
    s->item_opt[g] = (uint8_t)option;
    return 0;
}

static const SmzItemOpt *selected(const SmzSettings *s, int g)
{
    int o = s->item_opt[g];
    return o > 0 && o < SMZ_ITEMOPT_GROUP[g].count ? &SMZ_ITEMOPT[SMZ_ITEMOPT_GROUP[g].first + o] : NULL;
}

int smz_start_items(const SmzSettings *s, int *types, int cap)
{
    int n = 0;
    for (int g = 0; g < SMZ_ITEMOPT_GROUPS; g++) {
        const SmzItemOpt *o = selected(s, g);
        if (!o || !o->has_mem) continue;
        for (int i = 0; i < o->match_count && n < cap; i++) types[n++] = SMZ_ITEMOPT_MATCH[o->match_first + i];
    }
    return n;
}

int smz_early_items(const SmzSettings *s, int *types, int cap)
{
    int n = 0;
    for (int g = 0; g < SMZ_ITEMOPT_GROUPS; g++) {
        const SmzItemOpt *o = selected(s, g);
        if (!o || o->has_mem) continue;
        for (int i = 0; i < o->match_count && n < cap; i++) types[n++] = SMZ_ITEMOPT_MATCH[o->match_first + i];
    }
    return n;
}

int smz_start_memory(const SmzSettings *s, uint8_t zelda[78], int *keys, int *values, int cap)
{
    int n = 0;
    memset(zelda, 0, 78);
    for (int g = 0; g < SMZ_ITEMOPT_GROUPS; g++) {
        const SmzItemOpt *o = selected(s, g);
        if (!o || !o->has_mem) continue;
        for (int m = 0; m < o->mem_count; m++) {
            const SmzItemOptMem *mv = &SMZ_ITEMOPT_MEM[o->mem_first + m];
            if (!SMZ_ITEMOPT_GROUP[g].is_metroid) {
                if (mv->key >= 0 && mv->key < 78) zelda[mv->key] = (uint8_t)(zelda[mv->key] + mv->value);
                continue;
            }
            int k = 0;
            while (k < n && keys[k] != mv->key) k++;
            if (k == n) { if (n == cap) continue; keys[n] = mv->key; values[n] = 0; n++; }
            values[k] |= mv->value;
        }
    }
    return n;
}
