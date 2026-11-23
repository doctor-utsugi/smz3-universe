/* The playthrough check accepts real fills and rejects a fill whose Silver Arrows went missing. */
#include <stdio.h>
#include <string.h>
#include "smz3_generate.h"

int main(void)
{
    static SmzSeed seed;
    int bad = 0, caught = 0;
    for (int i = 0; i < 40; i++) {
        SmzSettings s, eff;
        smz_settings_defaults(&s);
        s.seed = 1000 + i * 37;
        s.keysanity = i % 4;
        s.placement_rule = i % 5;
        if (smz_fill_valid(&seed, &s, &eff)) { printf("seed %d failed\n", s.seed); bad++; continue; }
        if (eff.seed != s.seed) printf("seed %d needed a retry (%d)\n", s.seed, eff.seed);
        for (int l = 0; l < 512; l++)
            if (seed.world.item_at[l] == ITEM_SilverArrows) { seed.world.item_at[l] = ITEM_Nothing; break; }
        if (!smz_playthrough_ok(&seed, &eff)) caught++;
    }
    printf("%d fills rejected, %d sabotaged fills caught of 40\n", bad, caught);
    return bad || caught != 40;
}
