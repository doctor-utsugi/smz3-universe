/* Prints every change the C patcher makes for a seed, for comparison with the C# patcher. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smz3_patch.h"
#include "test_args.h"

static void log_patch(void *u, const char *cls, int off, const uint8_t *d, int n)
{
    (void)u;
    printf("P\t%s\t%X\t", cls, off);
    for (int i = 0; i < n; i++) printf("%02X", d[i]);
    printf("\n");
}

int main(int argc, char **argv)
{
    SmzSettings s;
    smz_settings_defaults(&s);
    s.seed = argc > 1 ? (int32_t)atol(argv[1]) : 12345;
    if (argc > 2) s.keysanity = atoi(argv[2]);
    if (argc > 3) s.placement_rule = atoi(argv[3]);
    if (argc > 4) s.ganon_crystals = atoi(argv[4]);
    if (argc > 5) s.gt_crystals = atoi(argv[5]);
    if (argc > 6) s.tourian_bosses = atoi(argv[6]);
    apply_extra_args(&s, argc, argv, 7);
    static SmzSeed seed;
    if (smz_fill(&seed, &s)) { fprintf(stderr, "fill failed\n"); return 1; }
    SmzPatchCfg cfg;
    smz_patch_cfg_defaults(&cfg, &s);
    for (int i = 7; i < argc; i++) if (!strncmp(argv[i], "jingle=", 7)) cfg.etecoons_jingle = atoi(argv[i] + 7);
    static uint8_t rom[SMZ_ROM_SIZE];
    int32_t patch_seed = rng_next(&seed.rng);
    cfg.hint_seed = rng_next(&seed.rng);
    if (smz_apply_patches(rom, &seed, &s, &cfg, patch_seed, log_patch, NULL)) { fprintf(stderr, "patch failed\n"); return 1; }
    return 0;
}
