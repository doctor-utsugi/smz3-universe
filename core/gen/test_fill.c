/* Checks the C filler against the C# one: same seed and settings must give the same item at every location. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smz3_fill.h"
#include "test_args.h"
#include "smz3_world_gen.h"

int main(int argc, char **argv)
{
    long seed = argc > 1 ? atol(argv[1]) : 12345;
    SmzSettings s;
    smz_settings_defaults(&s);
    s.seed = (int32_t)seed;
    if (argc > 2) s.keysanity = atoi(argv[2]);
    if (argc > 3) s.placement_rule = atoi(argv[3]);
    if (argc > 4) s.ganon_crystals = atoi(argv[4]);
    if (argc > 5) s.gt_crystals = atoi(argv[5]);
    if (argc > 6) s.tourian_bosses = atoi(argv[6]);
    apply_extra_args(&s, argc, argv, 7);
    static SmzSeed out;
    int rc = smz_fill(&out, &s);
    if (rc) { fprintf(stderr, "fill failed: %d\n", rc); return 1; }
    printf("seed %ld\nplacement", seed);
    for (int i = 0; i < SMZ_LOC_COUNT; i++) printf(" %d:%d", SMZ_LOCS[i].id, out.world.item_at[SMZ_LOCS[i].id]);
    printf("\nrewards");
    for (int r = 0; r < REG_COUNT; r++) if (SMZ_REGIONS[r].has_reward) printf(" %d:%d", r, out.world.reward_of[r]);
    printf("\nprereq");
    for (int r = 0; r < REG_COUNT; r++) if (r == REG_MiseryMire || r == REG_TurtleRock) printf(" %d:%d", r, out.world.prereq[r]);
    printf("\n");
    return 0;
}
